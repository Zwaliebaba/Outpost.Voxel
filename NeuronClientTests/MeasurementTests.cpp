#include "pch.h"

#include "CoveragePass.h"
#include "DescriptorHeap.h"
#include "ExplosionConstants.h"
#include "FrameQueries.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "SplatPass.h"
#include "TestSupport.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "Box.h"
#include "Explosion.h"
#include "Float3.h"
#include "PerspectiveView.h"
#include "SplatBounds.h"
#include "TraceHit.h"
#include "VoxModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float3;

constexpr std::uint32_t WIDTH_PIXELS = 161;
constexpr std::uint32_t HEIGHT_PIXELS = 91;

// A pixel centre this far inside a rectangle's edges is covered for certain, and this far outside them for certain not:
// the GPU's rectangle agrees with the twin's to rounding, and the rasterizer snaps its corners to 1/256 of a pixel.
constexpr float COVERAGE_EPSILON_PIXELS = 1.0f / 128.0f;

// The explosion's time for the oriented case: early in the first flight, while the station is still in view.
constexpr float EXPLOSION_SECONDS = 1.5f;

// The view splat tests' three-quarter camera: the application's default view of the station (§3).
[[nodiscard]] NeuronCore::PerspectiveView ThreeQuarterView() noexcept
{
  const Float3 center{0.5f, 0.5f, 127.5f};
  return NeuronCore::MakePerspectiveView(center + Float3{-318.43f, -318.43f, 260.0f}, center, {0.0f, 0.0f, 1.0f}, TEST_FOV_Y_RADIANS,
                                         TEST_NEAR_PLANE, WIDTH_PIXELS, HEIGHT_PIXELS);
}

// The boxes the view splat draws: intact, or posed by the explosion's twin at _timeSeconds.
[[nodiscard]] std::vector<NeuronCore::Box> DrawnBoxes(const NeuronCore::VoxModel& _model,
                                                      const std::optional<NeuronCore::ExplosionParameters>& _explosion, float _timeSeconds)
{
  std::vector<NeuronCore::Box> boxes;
  boxes.reserve(_model.records.size());
  for (std::uint32_t record = 0; record < _model.records.size(); ++record)
  {
    const NeuronCore::Box box = RecordBox(_model, record);
    if (!_explosion)
    {
      boxes.push_back(box);
      continue;
    }
    const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(record, box.center, *_explosion, _timeSeconds);
    boxes.push_back(NeuronCore::MakeOrientedBox(pose.center, box.radius, pose.axisX, pose.axisY, pose.axisZ));
  }
  return boxes;
}

// Per pixel, how many of the twin's rectangles (§9.2) cover its centre for certain, and how many might.
struct RectangleCoverage
{
  std::vector<std::uint32_t> surely;
  std::vector<std::uint32_t> possibly;
};

[[nodiscard]] RectangleCoverage CoverageOf(const std::vector<NeuronCore::Box>& _boxes, const NeuronCore::PerspectiveView& _view)
{
  const std::size_t pixels = static_cast<std::size_t>(_view.widthPixels) * _view.heightPixels;
  RectangleCoverage coverage{std::vector<std::uint32_t>(pixels), std::vector<std::uint32_t>(pixels)};
  const auto width = static_cast<float>(_view.widthPixels);
  const auto height = static_cast<float>(_view.heightPixels);
  for (const NeuronCore::Box& box : _boxes)
  {
    const NeuronCore::SplatBounds bounds = NeuronCore::PerspectiveSplatBounds(box, _view);
    if (!bounds.visible)
    {
      continue;
    }
    // Normalized device coordinates to pixels, y down.
    const float left = (bounds.minNdc.x + 1.0f) * 0.5f * width;
    const float right = (bounds.maxNdc.x + 1.0f) * 0.5f * width;
    const float top = (1.0f - bounds.maxNdc.y) * 0.5f * height;
    const float bottom = (1.0f - bounds.minNdc.y) * 0.5f * height;
    const auto first = [](float _edge) { return static_cast<std::int32_t>(std::floor(_edge - COVERAGE_EPSILON_PIXELS - 0.5f)); };
    const auto last = [](float _edge) { return static_cast<std::int32_t>(std::ceil(_edge + COVERAGE_EPSILON_PIXELS - 0.5f)); };
    for (std::int32_t y = std::max(first(top), 0); y <= std::min(last(bottom), static_cast<std::int32_t>(_view.heightPixels) - 1); ++y)
    {
      const float centerY = static_cast<float>(y) + 0.5f;
      for (std::int32_t x = std::max(first(left), 0); x <= std::min(last(right), static_cast<std::int32_t>(_view.widthPixels) - 1); ++x)
      {
        const float centerX = static_cast<float>(x) + 0.5f;
        const std::size_t pixel = static_cast<std::size_t>(y) * _view.widthPixels + static_cast<std::size_t>(x);
        const bool inside = centerX > left + COVERAGE_EPSILON_PIXELS && centerX < right - COVERAGE_EPSILON_PIXELS &&
                            centerY > top + COVERAGE_EPSILON_PIXELS && centerY < bottom - COVERAGE_EPSILON_PIXELS;
        const bool touching = centerX >= left - COVERAGE_EPSILON_PIXELS && centerX <= right + COVERAGE_EPSILON_PIXELS &&
                              centerY >= top - COVERAGE_EPSILON_PIXELS && centerY <= bottom + COVERAGE_EPSILON_PIXELS;
        coverage.surely[pixel] += inside ? 1u : 0u;
        coverage.possibly[pixel] += touching ? 1u : 0u;
      }
    }
  }
  return coverage;
}

// The vertex shader's least work: every vertex of every rectangle of every instance the draws cover, once (§9.1).
[[nodiscard]] std::uint64_t LeastVertexInvocations(const NeuronClient::VoxelScene& _scene) noexcept
{
  std::uint64_t vertices = 0;
  for (const NeuronClient::SceneInstance& instance : _scene.Instances())
  {
    const std::uint64_t drawInstances =
      (instance.recordCount + NeuronClient::SplatPass::RECTANGLES_PER_INSTANCE - 1) / NeuronClient::SplatPass::RECTANGLES_PER_INSTANCE;
    vertices += drawInstances * NeuronClient::SplatPass::RECTANGLES_PER_INSTANCE * 4;
  }
  return vertices;
}

} // namespace

// The measurements of Design/SampleRenderer.md §8, §11 and §14: the overdraw count, the timestamps, the view splat's
// pipeline statistics and the covered pixels, on WARP.
TEST_CLASS(MeasurementTests)
{
public:
  // §11: a pixel shader with a UAV side effect runs for every fragment, before any depth test, so the overdraw variant
  // counts every rectangle over each pixel's centre. The twin's rectangles bound the count from both sides, for the
  // intact station and for the explosion.
  TEST_METHOD(OverdrawCountsEveryRectangleOverThePixel)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronClient::VoxelScene scene(_device, model);
        const NeuronCore::PerspectiveView view = ThreeQuarterView();
        const NeuronCore::ExplosionParameters explosion = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
        const NeuronClient::SplatPass aligned(_device, NeuronClient::SplatPass::Kind::View, NeuronClient::SplatPass::Permutation::Aligned,
                                              NeuronClient::SplatPass::Variant::Overdraw);
        const NeuronClient::SplatPass oriented(_device, NeuronClient::SplatPass::Kind::View, NeuronClient::SplatPass::Permutation::Oriented,
                                               NeuronClient::SplatPass::Variant::Overdraw);
        for (const bool exploded : {false, true})
        {
          const SplatImage image =
            exploded ? RenderSplat(_device, scene, oriented, view, NeuronClient::MakeExplosionConstants(explosion, EXPLOSION_SECONDS))
                     : RenderSplat(_device, scene, aligned, view);
          const RectangleCoverage coverage =
            CoverageOf(DrawnBoxes(model, exploded ? std::optional(explosion) : std::nullopt, EXPLOSION_SECONDS), view);
          std::uint64_t counted = 0;
          std::uint64_t surely = 0;
          std::uint64_t possibly = 0;
          std::uint32_t failures = 0;
          for (std::size_t pixel = 0; pixel < image.overdraw.size(); ++pixel)
          {
            counted += image.overdraw[pixel];
            surely += coverage.surely[pixel];
            possibly += coverage.possibly[pixel];
            if ((image.overdraw[pixel] < coverage.surely[pixel] || image.overdraw[pixel] > coverage.possibly[pixel]) && failures++ < 10)
            {
              Logger::WriteMessage(std::format(L"({}, {}): {} invocations, the twin's rectangles {} to {}\n", pixel % WIDTH_PIXELS,
                                               pixel / WIDTH_PIXELS, image.overdraw[pixel], coverage.surely[pixel],
                                               coverage.possibly[pixel])
                                     .c_str());
            }
          }
          const wchar_t* name = exploded ? L"exploded" : L"intact";
          Logger::WriteMessage(
            std::format(L"{}: {} invocations counted, the twin's rectangles {} to {}\n", name, counted, surely, possibly).c_str());
          Assert::IsTrue(counted > 0, std::format(L"{}: something is counted", name).c_str());
          Assert::AreEqual(0u, failures, std::format(L"{}: pixels whose count the rectangles do not bound", name).c_str());
        }
      });
  }

  // §8, §14: one frame's view splat and coverage count in each variant, measured by the frame queries. The coverage
  // count is exactly the pixels a voxel wrote, the pixel shader ran at least that often, the vertex shader at least once
  // for every vertex, and the overdraw variant's own count agrees with its statistics.
  TEST_METHOD(QueriesMeasureTheViewSplat)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronClient::VoxelScene scene(_device, model);
        const NeuronCore::PerspectiveView view = ThreeQuarterView();
        const NeuronClient::SplatPass standard(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronClient::SplatPass plainDepth(_device, NeuronClient::SplatPass::Kind::View,
                                                 NeuronClient::SplatPass::Permutation::Aligned,
                                                 NeuronClient::SplatPass::Variant::PlainDepth);
        const NeuronClient::SplatPass overdraw(_device, NeuronClient::SplatPass::Kind::View, NeuronClient::SplatPass::Permutation::Aligned,
                                               NeuronClient::SplatPass::Variant::Overdraw);
        const std::array<const NeuronClient::SplatPass*, 3> passes{&standard, &plainDepth, &overdraw};
        constexpr std::array<const wchar_t*, 3> NAMES{L"conservative depth", L"plain depth", L"overdraw"};
        const NeuronClient::CoveragePass coveragePass(_device);
        NeuronClient::FrameQueries queries(_device, static_cast<std::uint32_t>(passes.size()));

        NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, L"Test render target views");
        NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, L"Test depth stencil views");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 12, true, L"Test shader views");
        NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false, L"Test CPU-only views");
        NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
        targets.Resize(_device, view.widthPixels, view.heightPixels);
        NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Test constants");
        const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeViewConstants(view));

        std::array<std::uint64_t, 3> invocations{};
        for (std::uint32_t slot = 0; slot < passes.size(); ++slot)
        {
          const NeuronClient::SplatPass& pass = *passes[slot];
          _device.Execute(
            [&](ID3D12GraphicsCommandList* _list)
            {
              std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
              _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
              queries.Begin(_list, slot, 100 + slot);
              targets.BeginSplat(_list);
              if (pass.CountsOverdraw())
              {
                targets.BeginOverdraw(_list);
              }
              queries.BeginStatistics(_list, slot);
              pass.Record(_list, scene, viewConstants, 0, targets.OverdrawWriteTable());
              queries.EndStatistics(_list, slot);
              queries.EndPass(_list, slot, NeuronClient::GpuPass::ViewSplat);
              if (pass.CountsOverdraw())
              {
                targets.EndOverdraw(_list);
              }
              queries.BeginCoverage(_list, slot);
              coveragePass.Record(_list, targets);
              queries.EndCoverage(_list, slot);
              queries.EndPass(_list, slot, NeuronClient::GpuPass::Coverage);
              targets.EndSplat(_list);
              queries.Resolve(_list, slot);
            });
          const std::optional<NeuronClient::FrameStatistics> statistics = queries.Read(slot);
          Assert::IsTrue(statistics.has_value(), L"a resolved slot reads");
          Assert::IsFalse(queries.Read(slot).has_value(), L"and reads once");

          const std::vector<std::byte> visibilityBytes = NeuronClient::ReadTexture2D(
            _device, targets.Visibility(), NeuronClient::ViewTargets::READABLE, NeuronClient::ViewTargets::VISIBILITY_BYTES_PER_PIXEL);
          const std::vector<std::byte> depthBytes =
            NeuronClient::ReadTexture2D(_device, targets.Depth(), NeuronClient::ViewTargets::READABLE, sizeof(float));
          std::vector<std::uint32_t> visibility(visibilityBytes.size() / sizeof(std::uint32_t));
          std::vector<float> depth(depthBytes.size() / sizeof(float));
          std::memcpy(visibility.data(), visibilityBytes.data(), visibilityBytes.size());
          std::memcpy(depth.data(), depthBytes.data(), depthBytes.size());
          std::uint64_t voxelPixels = 0;
          std::uint64_t depthPixels = 0;
          for (std::size_t pixel = 0; pixel < depth.size(); ++pixel)
          {
            voxelPixels += visibility[2 * pixel] != NeuronCore::NO_VOXEL ? 1u : 0u;
            depthPixels += depth[pixel] > NeuronCore::PERSPECTIVE_FAR_DEPTH ? 1u : 0u;
          }

          const NeuronClient::FrameStatistics& measured = *statistics;
          const std::optional<float> viewSplat = measured.passMilliseconds[static_cast<std::size_t>(NeuronClient::GpuPass::ViewSplat)];
          const std::optional<float> coverage = measured.passMilliseconds[static_cast<std::size_t>(NeuronClient::GpuPass::Coverage)];
          Logger::WriteMessage(
            std::format(L"{}: view splat {} ms, coverage {} ms, {} vertex and {} pixel-shader invocations, {} primitives, "
                        L"{} pixels covered of {} with a voxel\n",
                        NAMES[slot], viewSplat.value_or(-1.0f), coverage.value_or(-1.0f), measured.vertexShaderInvocations,
                        measured.pixelShaderInvocations, measured.primitives, measured.coveredPixels.value_or(0), voxelPixels)
              .c_str());
          Assert::AreEqual(std::uint64_t{100} + slot, measured.frame, L"the frame the slot measured");
          Assert::IsTrue(viewSplat.has_value() && *viewSplat >= 0.0f && coverage.has_value() && *coverage >= 0.0f,
                         L"both recorded passes are timed");
          Assert::IsFalse(measured.passMilliseconds[static_cast<std::size_t>(NeuronClient::GpuPass::Lighting)].has_value(),
                          L"a pass the frame did not run has no time");
          Assert::IsTrue(std::abs(measured.gpuMilliseconds - (*viewSplat + *coverage)) <= 1.0e-3f + 1.0e-4f * measured.gpuMilliseconds,
                         L"the frame's time is its passes'");
          Assert::AreEqual(voxelPixels, depthPixels, L"a pixel with a voxel is a pixel with a depth");
          Assert::IsTrue(measured.coveredPixels.has_value(), L"the frame counted coverage");
          Assert::AreEqual(voxelPixels, *measured.coveredPixels, L"the occlusion query counts exactly the covered pixels");
          Assert::IsTrue(measured.pixelShaderInvocations >= voxelPixels, L"every covered pixel ran the pixel shader");
          Assert::IsTrue(measured.vertexShaderInvocations >= LeastVertexInvocations(scene), L"every vertex ran the vertex shader");
          Assert::IsTrue(measured.primitives > 0, L"rectangles reached the rasterizer");
          if (pass.CountsOverdraw())
          {
            const std::vector<std::byte> overdrawBytes = NeuronClient::ReadTexture2D(
              _device, targets.Overdraw(), NeuronClient::ViewTargets::READABLE, NeuronClient::ViewTargets::OVERDRAW_BYTES_PER_PIXEL);
            std::vector<std::uint32_t> counts(overdrawBytes.size() / sizeof(std::uint32_t));
            std::memcpy(counts.data(), overdrawBytes.data(), overdrawBytes.size());
            std::uint64_t counted = 0;
            for (const std::uint32_t count : counts)
            {
              counted += count;
            }
            Logger::WriteMessage(std::format(L"overdraw: {} invocations counted in the texture\n", counted).c_str());
            Assert::IsTrue(counted <= measured.pixelShaderInvocations, L"the pixel shader counted no more than it ran");
          }
          invocations[slot] = measured.pixelShaderInvocations;
        }
        // Neither plain depth nor a UAV side effect lets the depth test run before the shader, so each runs the pixel shader
        // at least as often as conservative depth does. How much more is what M5 measures on hardware (§9.3).
        Logger::WriteMessage(
          std::format(L"pixel-shader invocations, plain depth over conservative: {:.3f}\n",
                      static_cast<double>(invocations[1]) / static_cast<double>(std::max<std::uint64_t>(invocations[0], 1)))
            .c_str());
        Assert::IsTrue(invocations[1] >= invocations[0], L"plain depth rejects nothing early");
        Assert::IsTrue(invocations[2] >= invocations[0], L"the overdraw variant rejects nothing early");
      });
  }
};

} // namespace NeuronClientTests
