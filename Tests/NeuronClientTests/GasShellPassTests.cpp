#include "pch.h"

#include "DescriptorHeap.h"
#include "GasShellPass.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "LightingConstants.h"
#include "LightingPass.h"
#include "ShadowMap.h"
#include "ShadowViewConstants.h"
#include "SplatPass.h"
#include "TestSupport.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "Blast.h"
#include "Explosion.h"
#include "Float3.h"
#include "Half.h"
#include "Lighting.h"
#include "PerspectiveView.h"
#include "Placement.h"
#include "VoxModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float3;

constexpr std::uint32_t SHADOW_MAP_PIXELS = 512;

// The HDR color is stored in half precision, which keeps 11 significant bits, and the shell's exp, sqrt and noise are
// the GPU's own: within these the GPU agrees with the twin.
constexpr float HDR_RELATIVE_TOLERANCE = 2.0e-3f;
constexpr float HDR_ABSOLUTE_TOLERANCE = 4.0e-3f;

// Pixels allowed beyond the tolerance. The shell's density is continuous, so rounding moves a pixel's sum a little and
// never flips it; none is expected, and four is headroom for a toolchain update.
constexpr std::uint32_t MISMATCH_LIMIT = 4;

[[nodiscard]] bool Close(float _expected, float _actual) noexcept
{
  return std::abs(_actual - _expected) <= HDR_RELATIVE_TOLERANCE * std::abs(_expected) + HDR_ABSOLUTE_TOLERANCE;
}

[[nodiscard]] std::vector<std::uint16_t> ReadHdr(NeuronClient::GraphicsDevice& _device, const NeuronClient::ViewTargets& _targets,
                                                 std::size_t _pixels)
{
  std::vector<std::uint16_t> hdr(_pixels * 4);
  const std::vector<std::byte> bytes = NeuronClient::ReadTexture2D(_device, _targets.HdrColor(), NeuronClient::ViewTargets::READABLE,
                                                                   NeuronClient::ViewTargets::HDR_BYTES_PER_PIXEL);
  std::memcpy(hdr.data(), bytes.data(), bytes.size());
  return hdr;
}

} // namespace

// The gas shell pass against its twin (Design/ADR/ADR-025, R15): over the lit station, what the pass adds to every pixel
// is what GasShellPixel gives on the depth the view splat wrote, in front of the station and through the background.
TEST_CLASS(GasShellPassTests)
{
public:
  TEST_METHOD(AddsTheTwinsShells)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronClient::VoxelScene scene(_device, {&model, 1});
        const std::vector<NeuronCore::Placement> placements = WholePlacements(model);
        const NeuronClient::SplatPass viewSplat(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronClient::SplatPass shadowSplat(_device, NeuronClient::SplatPass::Kind::Shadow);
        const NeuronClient::LightingPass lighting(_device);
        const NeuronClient::GasShellPass gasShells(_device);

        const Float3 center{0.5f, 127.5f, 0.5f};
        const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView(
          center + Float3{-420.0f, 300.0f, -420.0f}, center, {0.0f, 1.0f, 0.0f}, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE, 161, 91);
        const NeuronCore::LightingParameters parameters = NeuronCore::MakeLightingParameters(TestWorld(), 1.0f);
        const NeuronCore::OrthographicView shadowView = TestShadowView(model, parameters.toSun, 256.0f, SHADOW_MAP_PIXELS);
        const Float3 centroid = NeuronCore::VoxelCentroid(model);
        // A large shell about the station, early and bright, and a small one in front of it, late and faint.
        const std::vector<NeuronCore::Blast> blasts{
          {.origin = centroid, .drift = {0.0f, 0.0f, 0.0f}, .drag = 1.0f, .extent = 160.0f, .seed = 3u, .timeSeconds = 0.35f},
          {.origin = centroid + Float3{-150.0f, 60.0f, -150.0f},
           .drift = {5.0f, 0.0f, 0.0f},
           .drag = 1.0f,
           .extent = 25.0f,
           .seed = 9u,
           .timeSeconds = 1.1f}};
        const NeuronCore::GasShells shells = NeuronCore::MakeGasShells(blasts, view.position);
        Assert::AreEqual(2u, shells.count, L"both shells are lit");

        NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false, L"Test render target views");
        NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 3, false, L"Test depth stencil views");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 12, true, L"Test shader views");
        NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false, L"Test CPU-only views");
        NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
        targets.Resize(_device, view.widthPixels, view.heightPixels);
        const NeuronClient::ShadowMap shadowMap(_device, dsvHeap, shaderHeap, SHADOW_MAP_PIXELS);
        NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Test constants");
        const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeViewConstants(view));
        const D3D12_GPU_VIRTUAL_ADDRESS shadowViewConstants = constants.Push(NeuronClient::MakeShadowViewConstants(shadowView));
        const D3D12_GPU_VIRTUAL_ADDRESS lightingConstants =
          constants.Push(NeuronClient::MakeLightingConstants(parameters, shadowView, static_cast<std::uint32_t>(placements.size())));
        const D3D12_GPU_VIRTUAL_ADDRESS blastConstants = constants.Push(NeuronCore::MakeBlastLighting({}, view.position));
        const D3D12_GPU_VIRTUAL_ADDRESS shellConstants = constants.Push(shells);
        const NeuronClient::SplatPlacements pushed = PushTestPlacements(constants, placements);
        const D3D12_GPU_VIRTUAL_ADDRESS placementHeat = NeuronClient::PushPlacementHeat(constants, placements);

        // The lit station first, read back, and then the shells added over it.
        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            shadowMap.BeginSplat(_list);
            shadowSplat.Record(_list, scene, shadowViewConstants, pushed.constants, pushed.draws);
            shadowMap.EndSplat(_list);
            targets.BeginSplat(_list);
            viewSplat.Record(_list, scene, viewConstants, pushed.constants, pushed.draws);
            targets.EndSplat(_list);
            targets.BeginLighting(_list);
            lighting.Record(_list, targets, shadowMap, scene, viewConstants, shadowViewConstants, lightingConstants, pushed.constants,
                            blastConstants, placementHeat);
            targets.EndLighting(_list);
          });
        const std::size_t pixels = static_cast<std::size_t>(view.widthPixels) * view.heightPixels;
        const std::vector<std::uint16_t> before = ReadHdr(_device, targets, pixels);
        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            targets.BeginLighting(_list);
            gasShells.Record(_list, targets, viewConstants, shellConstants);
            targets.EndLighting(_list);
          });
        const std::vector<std::uint16_t> after = ReadHdr(_device, targets, pixels);
        std::vector<float> depth(pixels);
        const std::vector<std::byte> depthBytes =
          NeuronClient::ReadTexture2D(_device, targets.Depth(), NeuronClient::ViewTargets::READABLE, sizeof(float));
        std::memcpy(depth.data(), depthBytes.data(), depthBytes.size());

        std::uint32_t lit = 0;
        std::uint32_t overVoxels = 0;
        std::uint32_t mismatches = 0;
        std::vector<std::wstring> mismatched;
        for (std::uint32_t y = 0; y < view.heightPixels; ++y)
        {
          for (std::uint32_t x = 0; x < view.widthPixels; ++x)
          {
            const std::size_t pixel = static_cast<std::size_t>(y) * view.widthPixels + x;
            const Float3 added = NeuronCore::GasShellPixel(view, x, y, depth[pixel], shells);
            const Float3 expected{NeuronCore::HalfToFloat(before[4 * pixel]) + added.x,
                                  NeuronCore::HalfToFloat(before[4 * pixel + 1]) + added.y,
                                  NeuronCore::HalfToFloat(before[4 * pixel + 2]) + added.z};
            const Float3 actual{NeuronCore::HalfToFloat(after[4 * pixel]), NeuronCore::HalfToFloat(after[4 * pixel + 1]),
                                NeuronCore::HalfToFloat(after[4 * pixel + 2])};
            const bool shown = added.x + added.y + added.z > 1.0e-3f;
            lit += shown ? 1u : 0u;
            overVoxels += shown && !NeuronCore::IsFarPerspectiveDepth(depth[pixel]) ? 1u : 0u;
            if (!Close(expected.x, actual.x) || !Close(expected.y, actual.y) || !Close(expected.z, actual.z))
            {
              ++mismatches;
              mismatched.push_back(std::format(L"({}, {}): ({}, {}, {}), the twin's ({}, {}, {})", x, y, actual.x, actual.y, actual.z,
                                               expected.x, expected.y, expected.z));
            }
          }
        }
        Logger::WriteMessage(std::format(L"{} of {} pixels show a shell, {} of them over the station; {} beyond the tolerance\n", lit,
                                         pixels, overVoxels, mismatches)
                               .c_str());
        for (std::size_t i = 0; i < std::min<std::size_t>(mismatched.size(), 10); ++i)
        {
          Logger::WriteMessage((mismatched[i] + L"\n").c_str());
        }
        Assert::IsTrue(lit > pixels / 4 && overVoxels > 0, L"the shells cover much of the view, the station too");
        Assert::IsTrue(mismatches <= MISMATCH_LIMIT,
                       std::format(L"{} pixels disagree with the gas shell twin, more than {}", mismatches, MISMATCH_LIMIT).c_str());
      });
  }
};

} // namespace NeuronClientTests
