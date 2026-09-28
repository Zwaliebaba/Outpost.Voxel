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
#include "ToneMapPass.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "Float3.h"
#include "Lighting.h"
#include "OctahedralNormal.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "RenderSettings.h"
#include "ToneMap.h"
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
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float3;

constexpr DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr std::uint32_t COLOR_BYTES_PER_PIXEL = 16;
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

// The tone map reads the HDR color the GPU wrote and computes a few operations on it; the result is single precision.
constexpr float DISPLAY_TOLERANCE = 1.0e-5f;

[[nodiscard]] bool Close(float _expected, float _actual) noexcept
{
  return std::abs(_actual - _expected) <= HDR_RELATIVE_TOLERANCE * std::abs(_expected) + HDR_ABSOLUTE_TOLERANCE;
}

} // namespace

// The lighting and tone map passes against their twins, each on the inputs the GPU itself wrote (Design/Archive/SampleRenderer.md
// §11, R15): the lighting from the view splat's depth and visibility and the shadow splat's map, the tone map from the
// lighting's HDR color.
TEST_CLASS(LightingPassTests)
{
public:
  TEST_METHOD(LightsAndToneMapsAsTheTwinsDo)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronClient::VoxelScene scene(_device, model);
        const NeuronClient::SplatPass viewSplat(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronClient::SplatPass shadowSplat(_device, NeuronClient::SplatPass::Kind::Shadow);
        const NeuronClient::LightingPass lighting(_device);
        const NeuronClient::ToneMapPass toneMap(_device, COLOR_FORMAT);

        // The application's default view, lit as the station's file asks, with the emissive gain above one so that
        // the gain shows.
        const Float3 center{0.5f, 127.5f, 0.5f};
        const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView(
          center + Float3{-318.43f, 260.0f, -318.43f}, center, {0.0f, 1.0f, 0.0f}, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE, 161, 91);
        const NeuronCore::RenderSettings settings = NeuronCore::ReadRenderSettings(model.renderObjects);
        const NeuronCore::LightingParameters parameters = NeuronCore::MakeLightingParameters(settings, 1.5f);
        const NeuronCore::OrthographicView shadowView = TestShadowView(model, parameters.toSun, 256.0f, SHADOW_MAP_PIXELS);

        NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false, L"Test render target views");
        NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false, L"Test depth stencil views");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 12, true, L"Test shader views");
        NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false, L"Test CPU-only views");
        NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
        targets.Resize(_device, view.widthPixels, view.heightPixels);
        const NeuronClient::ShadowMap shadowMap(_device, dsvHeap, shaderHeap, SHADOW_MAP_PIXELS);
        const winrt::com_ptr<ID3D12Resource> color =
          NeuronClient::CreateTexture2D(_device, COLOR_FORMAT, view.widthPixels, view.heightPixels, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, L"Test color");
        const std::uint32_t colorView = rtvHeap.Allocate();
        _device.Device()->CreateRenderTargetView(color.get(), nullptr, rtvHeap.Cpu(colorView));
        NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Test constants");
        const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeViewConstants(view));
        const D3D12_GPU_VIRTUAL_ADDRESS shadowViewConstants = constants.Push(NeuronClient::MakeShadowViewConstants(shadowView));
        const D3D12_GPU_VIRTUAL_ADDRESS lightingConstants = constants.Push(NeuronClient::MakeLightingConstants(parameters, shadowView));

        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            shadowMap.BeginSplat(_list);
            shadowSplat.Record(_list, scene, shadowViewConstants);
            shadowMap.EndSplat(_list);
            targets.BeginSplat(_list);
            viewSplat.Record(_list, scene, viewConstants);
            targets.EndSplat(_list);
            targets.BeginLighting(_list);
            lighting.Record(_list, targets, shadowMap, scene, viewConstants, shadowViewConstants, lightingConstants);
            targets.EndLighting(_list);
            const D3D12_CPU_DESCRIPTOR_HANDLE target = rtvHeap.Cpu(colorView);
            _list->OMSetRenderTargets(1, &target, FALSE, nullptr);
            toneMap.Record(_list, targets, settings.exposure);
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
          _device, targets.HdrColor(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NeuronClient::ViewTargets::HDR_BYTES_PER_PIXEL);
        std::memcpy(hdr.data(), hdrBytes.data(), hdrBytes.size());
        std::vector<float> display(pixels * 4);
        const std::vector<std::byte> displayBytes =
          NeuronClient::ReadTexture2D(_device, color.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, COLOR_BYTES_PER_PIXEL);
        std::memcpy(display.data(), displayBytes.data(), displayBytes.size());

        const NeuronCore::ShadowMapImage shadowImage{SHADOW_MAP_PIXELS, SHADOW_MAP_PIXELS, shadowDepth};
        std::uint32_t voxelPixels = 0;
        std::uint32_t groundPixels = 0;
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
            if (voxel != NeuronCore::NO_VOXEL)
            {
              const NeuronClient::PaletteMaterial& material =
                scene.PaletteValues().materials[NeuronCore::UnpackVoxelRecord(model.records[voxel]).color];
              albedo = material.albedo;
              emissiveScale = material.emissiveScale;
              ++voxelPixels;
            }
            const Float3 expected = NeuronCore::LightPixel(view, x, y, voxel, NeuronCore::UnpackOctahedralNormal(visibility[2 * pixel + 1]),
                                                           depth[pixel], albedo, emissiveScale, shadowImage, shadowView, parameters);
            const Float3 actual{HalfToFloat(hdr[4 * pixel]), HalfToFloat(hdr[4 * pixel + 1]), HalfToFloat(hdr[4 * pixel + 2])};
            if (voxel == NeuronCore::NO_VOXEL && actual.x != parameters.background.x)
            {
              ++groundPixels;
            }
            if (!Close(expected.x, actual.x) || !Close(expected.y, actual.y) || !Close(expected.z, actual.z))
            {
              ++flips;
              flipped.push_back(std::format(L"({}, {}): voxel {}, ({}, {}, {}), the twin's ({}, {}, {})", x, y, voxel, actual.x, actual.y,
                                            actual.z, expected.x, expected.y, expected.z));
            }

            // The tone map, from what the lighting wrote.
            const Float3 mapped = NeuronCore::ToneMap(actual, settings.exposure);
            const bool displayClose = std::abs(display[4 * pixel] - mapped.x) <= DISPLAY_TOLERANCE &&
                                      std::abs(display[4 * pixel + 1] - mapped.y) <= DISPLAY_TOLERANCE &&
                                      std::abs(display[4 * pixel + 2] - mapped.z) <= DISPLAY_TOLERANCE;
            Assert::IsTrue(displayClose,
                           std::format(L"({}, {}): displayed ({}, {}, {}), the tone map twin's ({}, {}, {})", x, y, display[4 * pixel],
                                       display[4 * pixel + 1], display[4 * pixel + 2], mapped.x, mapped.y, mapped.z)
                             .c_str());
          }
        }
        Logger::WriteMessage(
          std::format(L"{} voxel pixels, {} ground pixels, {} beyond the tolerance\n", voxelPixels, groundPixels, flips).c_str());
        for (std::size_t i = 0; i < std::min<std::size_t>(flipped.size(), 10); ++i)
        {
          Logger::WriteMessage((flipped[i] + L"\n").c_str());
        }
        Assert::IsTrue(voxelPixels > 0 && groundPixels > 0, L"the view shows both the station and the ground");
        Assert::IsTrue(flips <= SHADOW_FLIP_LIMIT,
                       std::format(L"{} pixels disagree with the lighting twin, more than {}", flips, SHADOW_FLIP_LIMIT).c_str());
      });
  }
};

} // namespace NeuronClientTests
