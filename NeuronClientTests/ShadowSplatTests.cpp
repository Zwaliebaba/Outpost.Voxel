#include "pch.h"

#include "GraphicsDevice.h"
#include "SplatPass.h"
#include "TestSupport.h"
#include "VoxelScene.h"

#include "Box.h"
#include "Float3.h"
#include "Lighting.h"
#include "OrthographicView.h"
#include "Ray.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelGrid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float3;

constexpr float RADIANS_PER_DEGREE = 0.0174532925f;

// The splat's t / range against the tracer's, both in [0, 1]: a few units in the last place, which is what a GPU's
// division may differ by (D3D allows 2.5).
constexpr float DEPTH_TOLERANCE = 1.0e-6f;

// How far inside or outside a box a ray may pass and still be answered differently by the GPU and the tracer, in voxel
// units; the view splat's tests use the same sliver (Design/Archive/SampleRenderer.md §14).
constexpr float EDGE_EPSILON = 1.0f / 256.0f;

// Mismatches on an edge allowed per map, which §14 sets from the first measured run and gives a reason for. That run, on
// WARP in CI on 2026-09-27, found none in either map. The bound is not zero because §14 assumes no bit equality between
// CPU and GPU: an MSVC or WARP update that contracts or rounds differently can move a ray that lands within rounding of
// an edge to its other side. Such a ray is rare, so four is headroom.
constexpr std::uint32_t EDGE_MISMATCH_LIMIT = 4;

struct Comparison
{
  std::uint32_t hitTexels = 0;
  std::uint32_t emptyTexels = 0;
  std::uint32_t edgeMismatches = 0;
  std::vector<std::wstring> failures;
};

void Report(const wchar_t* _map, const Comparison& _comparison)
{
  Logger::WriteMessage(std::format(L"{}: {} texels hit, {} empty, {} mismatches on an edge, {} failures\n", _map, _comparison.hitTexels,
                                   _comparison.emptyTexels, _comparison.edgeMismatches, _comparison.failures.size())
                         .c_str());
  for (std::size_t i = 0; i < std::min<std::size_t>(_comparison.failures.size(), 10); ++i)
  {
    Logger::WriteMessage((_comparison.failures[i] + L"\n").c_str());
  }
  Assert::IsTrue(_comparison.hitTexels > 0, std::format(L"{}: the model casts a shadow", _map).c_str());
  Assert::IsTrue(_comparison.failures.empty(),
                 std::format(L"{}: {} texels disagree with the tracer", _map, _comparison.failures.size()).c_str());
  Assert::IsTrue(_comparison.edgeMismatches <= EDGE_MISMATCH_LIMIT,
                 std::format(L"{}: {} mismatches on an edge, more than {}", _map, _comparison.edgeMismatches, EDGE_MISMATCH_LIMIT).c_str());
}

// The depth the map should hold for a hit at _distance, or the far plane for none.
[[nodiscard]] float ExpectedDepth(const NeuronCore::OrthographicView& _view, const NeuronCore::TraceHit& _hit) noexcept
{
  return _hit.voxel == NeuronCore::NO_VOXEL ? NeuronCore::ORTHOGRAPHIC_FAR_DEPTH : NeuronCore::OrthographicDepth(_view, _hit.distance);
}

// Every record's box, with its radius changed by _change: grown boxes hit wherever a ray passes near an edge, shrunk
// ones only well inside.
[[nodiscard]] std::vector<NeuronCore::Box> Boxes(const NeuronCore::VoxModel& _model, float _change)
{
  std::vector<NeuronCore::Box> boxes;
  boxes.reserve(_model.records.size());
  for (std::uint32_t record = 0; record < _model.records.size(); ++record)
  {
    const NeuronCore::Box box = RecordBox(_model, record);
    boxes.push_back(NeuronCore::MakeAxisAlignedBox(box.center, box.radius + Float3{_change, _change, _change}));
  }
  return boxes;
}

} // namespace

// The shadow splat against the reference tracer, texel for texel (Design/Archive/SampleRenderer.md §10, §14).
TEST_CLASS(ShadowSplatTests)
{
public:
  // §14: the sun straight overhead makes every ray axis-parallel, with two exactly-zero direction components. A texel
  // is exactly a unit across and the square is centred on the station's box, so that texel centres fall on the integer
  // lines in x, the seams between voxels, and halfway between them in y. The arithmetic is exact, so the map must be.
  TEST_METHOD(SunOverheadMatchesTheTracer)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronCore::VoxelGrid grid(model);
        const NeuronClient::VoxelScene scene(_device, {&model, 1});
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::Shadow);
        const NeuronCore::OrthographicView view = TestShadowView(model, {0.0f, 1.0f, 0.0f}, 128.0f, 256);
        const std::vector<float> depth = RenderShadowSplat(_device, scene, WholePlacements(model), pass, view);

        Comparison comparison;
        for (std::uint32_t y = 0; y < view.heightPixels; ++y)
        {
          for (std::uint32_t x = 0; x < view.widthPixels; ++x)
          {
            const NeuronCore::Ray ray = NeuronCore::OrthographicRay(view, x, y);
            const NeuronCore::TraceHit hit = grid.Trace(ray, 0.0f);
            const float expected = ExpectedDepth(view, hit);
            const float actual = depth[static_cast<std::size_t>(y) * view.widthPixels + x];
            ++(hit.voxel == NeuronCore::NO_VOXEL ? comparison.emptyTexels : comparison.hitTexels);
            if (std::abs(actual - expected) > DEPTH_TOLERANCE)
            {
              comparison.failures.push_back(std::format(L"({}, {}): depth {}, the tracer's {}", x, y, actual, expected));
            }
          }
        }
        Report(L"sun overhead", comparison);
      });
  }

  // §14's synthetic map: a random block under the station's sun, eight texels to a voxel's edge. Where the GPU and the
  // tracer differ, the GPU's depth must lie between the answers for boxes grown and shrunk by EDGE_EPSILON: the ray
  // passed that near an edge.
  TEST_METHOD(TiltedSunOverABlockMatchesTheTracer)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = RandomBlock();
        const NeuronClient::VoxelScene scene(_device, {&model, 1});
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::Shadow);
        const Float3 toSun = NeuronCore::SunDirection(50.0f * RADIANS_PER_DEGREE, 50.0f * RADIANS_PER_DEGREE);
        const NeuronCore::OrthographicView view = TestShadowView(model, toSun, 8.0f, 128);
        const std::vector<float> depth = RenderShadowSplat(_device, scene, WholePlacements(model), pass, view);

        const std::vector<NeuronCore::Box> exact = Boxes(model, 0.0f);
        const std::vector<NeuronCore::Box> grown = Boxes(model, EDGE_EPSILON);
        const std::vector<NeuronCore::Box> shrunk = Boxes(model, -EDGE_EPSILON);
        Comparison comparison;
        for (std::uint32_t y = 0; y < view.heightPixels; ++y)
        {
          for (std::uint32_t x = 0; x < view.widthPixels; ++x)
          {
            const NeuronCore::Ray ray = NeuronCore::OrthographicRay(view, x, y);
            const NeuronCore::TraceHit hit = NeuronCore::TraceBoxes<false>(exact, ray, 0.0f);
            const float expected = ExpectedDepth(view, hit);
            const float actual = depth[static_cast<std::size_t>(y) * view.widthPixels + x];
            ++(hit.voxel == NeuronCore::NO_VOXEL ? comparison.emptyTexels : comparison.hitTexels);
            if (std::abs(actual - expected) <= DEPTH_TOLERANCE)
            {
              continue;
            }
            const float nearest = ExpectedDepth(view, NeuronCore::TraceBoxes<false>(grown, ray, 0.0f));
            const float farthest = ExpectedDepth(view, NeuronCore::TraceBoxes<false>(shrunk, ray, 0.0f));
            if (actual >= nearest - DEPTH_TOLERANCE && actual <= farthest + DEPTH_TOLERANCE)
            {
              ++comparison.edgeMismatches;
              continue;
            }
            comparison.failures.push_back(std::format(L"({}, {}): depth {}, the tracer's {} (between {} and {} near an edge)", x, y, actual,
                                                      expected, nearest, farthest));
          }
        }
        Report(L"block, tilted sun", comparison);
      });
  }
};

} // namespace NeuronClientTests
