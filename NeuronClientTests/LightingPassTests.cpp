#include "pch.h"

#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "LightingConstants.h"
#include "LightingPass.h"
#include "PaletteConstants.h"
#include "ShadowMap.h"
#include "ShadowViewConstants.h"
#include "SplatPass.h"
#include "TestSupport.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "Float3.h"
#include "Half.h"
#include "Lighting.h"
#include "OctahedralNormal.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "Placement.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float3;

constexpr std::uint32_t SHADOW_MAP_PIXELS = 1024;

// The HDR color is stored in half precision, which keeps 11 significant bits; and hardware may filter a comparison
// sampler's taps with bilinear weights of 8 fractional bits, which moves a shadow factor by up to 1/256 and the sun's
// share of a color with it. Within these the GPU agrees with the twin.
constexpr float HDR_RELATIVE_TOLERANCE = 1.0e-3f;
constexpr float HDR_ABSOLUTE_TOLERANCE = 4.0e-3f;

// Pixels allowed beyond the tolerance, where a shadow tap's depth comparison falls the other way on the GPU, which §14
// sets from the first measured run. That run, on WARP in CI on 2026-09-27, found none among its 14,651 pixels. The bound
// is not zero for the reason the splat tests give: a toolchain update can round a comparison made within rounding of the
// map's depth the other way. Four is headroom.
constexpr std::uint32_t SHADOW_FLIP_LIMIT = 4;

[[nodiscard]] bool Close(float _expected, float _actual) noexcept
{
  return std::abs(_actual - _expected) <= HDR_RELATIVE_TOLERANCE * std::abs(_expected) + HDR_ABSOLUTE_TOLERANCE;
}

} // namespace

// The lighting pass against its twin, on the inputs the GPU itself wrote (Design/Archive/SampleRenderer.md §11, R15): the view
// splat's depth and visibility and the shadow splat's map. The tone map, which now mixes bloom in, is tested with bloom
// (BloomPassTests).
TEST_CLASS(LightingPassTests)
{
public:
  TEST_METHOD(LightsAsTheTwinDoes)
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

        // The application's default view, lit as the station's file lit it, with the emissive gain above one so that
        // the gain shows.
        const Float3 center{0.5f, 127.5f, 0.5f};
        const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView(
          center + Float3{-318.43f, 260.0f, -318.43f}, center, {0.0f, 1.0f, 0.0f}, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE, 161, 91);
        const NeuronCore::LightingParameters parameters = NeuronCore::MakeLightingParameters(TestWorld(), 1.5f);
        const NeuronCore::OrthographicView shadowView = TestShadowView(model, parameters.toSun, 256.0f, SHADOW_MAP_PIXELS);

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
        const NeuronClient::SplatPlacements pushed = PushTestPlacements(constants, placements);

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
            lighting.Record(_list, targets, shadowMap, scene, viewConstants, shadowViewConstants, lightingConstants, pushed.constants);
            targets.EndLighting(_list);
          });

        const std::size_t pixels = static_cast<std::size_t>(view.widthPixels) * view.heightPixels;
        std::vector<std::uint32_t> visibility(pixels * 2);
        const std::vector<std::byte> visibilityBytes = NeuronClient::ReadTexture2D(
          _device, targets.Visibility(), NeuronClient::ViewTargets::READABLE, NeuronClient::ViewTargets::VISIBILITY_BYTES_PER_PIXEL);
        std::memcpy(visibility.data(), visibilityBytes.data(), visibilityBytes.size());
        std::vector<float> depth(pixels);
        const std::vector<std::byte> depthBytes =
          NeuronClient::ReadTexture2D(_device, targets.Depth(), NeuronClient::ViewTargets::READABLE, sizeof(float));
        std::memcpy(depth.data(), depthBytes.data(), depthBytes.size());
        std::vector<float> shadowDepth(static_cast<std::size_t>(SHADOW_MAP_PIXELS) * SHADOW_MAP_PIXELS);
        const std::vector<std::byte> shadowBytes = NeuronClient::ReadTexture2D(
          _device, shadowMap.Depth(), NeuronClient::ShadowMap::READABLE, NeuronClient::ShadowMap::BYTES_PER_TEXEL);
        std::memcpy(shadowDepth.data(), shadowBytes.data(), shadowBytes.size());
        std::vector<std::uint16_t> hdr(pixels * 4);
        const std::vector<std::byte> hdrBytes = NeuronClient::ReadTexture2D(
          _device, targets.HdrColor(), NeuronClient::ViewTargets::READABLE, NeuronClient::ViewTargets::HDR_BYTES_PER_PIXEL);
        std::memcpy(hdr.data(), hdrBytes.data(), hdrBytes.size());

        const NeuronCore::ShadowMapImage shadowImage{SHADOW_MAP_PIXELS, SHADOW_MAP_PIXELS, shadowDepth};
        std::uint32_t voxelPixels = 0;
        std::uint32_t backgroundPixels = 0;
        std::uint32_t flips = 0;
        std::vector<std::wstring> flipped;
        for (std::uint32_t y = 0; y < view.heightPixels; ++y)
        {
          for (std::uint32_t x = 0; x < view.widthPixels; ++x)
          {
            const std::size_t pixel = static_cast<std::size_t>(y) * view.widthPixels + x;
            const std::uint32_t voxel = visibility[2 * pixel];
            Float3 albedo{};
            float emissiveScale = 0.0f;
            if (const std::optional<NeuronCore::PlacedVoxel> placed = NeuronCore::FindVoxel(placements, voxel))
            {
              const NeuronClient::PaletteMaterial& material =
                scene.PaletteValues(placed->paletteIndex).materials[NeuronCore::UnpackVoxelRecord(model.records[placed->record]).color];
              albedo = material.albedo;
              emissiveScale = material.emissiveScale;
              ++voxelPixels;
            }
            const Float3 expected = NeuronCore::LightPixel(view, x, y, voxel, NeuronCore::UnpackOctahedralNormal(visibility[2 * pixel + 1]),
                                                           depth[pixel], albedo, emissiveScale, shadowImage, shadowView, parameters);
            const Float3 actual{NeuronCore::HalfToFloat(hdr[4 * pixel]), NeuronCore::HalfToFloat(hdr[4 * pixel + 1]),
                                NeuronCore::HalfToFloat(hdr[4 * pixel + 2])};
            if (voxel == NeuronCore::NO_VOXEL)
            {
              ++backgroundPixels;
            }
            if (!Close(expected.x, actual.x) || !Close(expected.y, actual.y) || !Close(expected.z, actual.z))
            {
              ++flips;
              flipped.push_back(std::format(L"({}, {}): voxel {}, ({}, {}, {}), the twin's ({}, {}, {})", x, y, voxel, actual.x, actual.y,
                                            actual.z, expected.x, expected.y, expected.z));
            }
          }
        }
        Logger::WriteMessage(
          std::format(L"{} voxel pixels, {} background pixels, {} beyond the tolerance\n", voxelPixels, backgroundPixels, flips).c_str());
        for (std::size_t i = 0; i < std::min<std::size_t>(flipped.size(), 10); ++i)
        {
          Logger::WriteMessage((flipped[i] + L"\n").c_str());
        }
        Assert::IsTrue(voxelPixels > 0 && backgroundPixels > 0, L"the view shows both the station and the background");
        Assert::IsTrue(flips <= SHADOW_FLIP_LIMIT,
                       std::format(L"{} pixels disagree with the lighting twin, more than {}", flips, SHADOW_FLIP_LIMIT).c_str());
      });
  }
};

} // namespace NeuronClientTests
