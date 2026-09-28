#include "pch.h"

#include "DebugViewPass.h"
#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "PaletteConstants.h"
#include "ShadowMap.h"
#include "ShadowViewConstants.h"
#include "SplatPass.h"
#include "TestSupport.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "DebugView.h"
#include "Float3.h"
#include "Lighting.h"
#include "OctahedralNormal.h"
#include "PerspectiveView.h"
#include "Placement.h"
#include "RenderSettings.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

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

constexpr DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr std::uint32_t COLOR_BYTES_PER_PIXEL = 16;
// The shader and the twin compute the same few operations; they agree to rounding.
constexpr float COLOR_TOLERANCE = 1.0e-5f;
constexpr std::uint32_t SHADOW_MAP_PIXELS = 256;

} // namespace

// The debug view pass against its twin on the buffers the GPU wrote (Design/Archive/SampleRenderer.md §11, R15).
TEST_CLASS(DebugViewPassTests)
{
public:
  TEST_METHOD(ShowsWhatTheTwinComputes)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronClient::VoxelScene scene(_device, {&model, 1});
        const std::vector<NeuronCore::Placement> placements = WholePlacements(model);
        const NeuronClient::SplatPass viewSplat(_device, NeuronClient::SplatPass::Kind::View);
        // The overdraw view shows what the overdraw variant counts (§11).
        const NeuronClient::SplatPass overdrawSplat(_device, NeuronClient::SplatPass::Kind::View,
                                                    NeuronClient::SplatPass::Variant::Overdraw);
        const NeuronClient::SplatPass shadowSplat(_device, NeuronClient::SplatPass::Kind::Shadow);
        const NeuronClient::DebugViewPass debugView(_device, COLOR_FORMAT);
        const Float3 center{0.5f, 127.5f, 0.5f};
        const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView(
          center + Float3{-318.43f, 260.0f, -318.43f}, center, {0.0f, 1.0f, 0.0f}, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE, 161, 91);
        const NeuronCore::RenderSettings settings = NeuronCore::DefaultRenderSettings();
        const NeuronCore::OrthographicView shadowView = TestShadowView(
          model, NeuronCore::SunDirection(settings.sunElevationRadians, settings.sunAzimuthRadians), 256.0f, SHADOW_MAP_PIXELS);

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
        const NeuronClient::SplatPlacements pushed = PushTestPlacements(constants, placements);

        const std::size_t pixels = static_cast<std::size_t>(view.widthPixels) * view.heightPixels;
        std::vector<std::uint32_t> visibility(pixels * 2);
        std::vector<float> shadowDepth(static_cast<std::size_t>(SHADOW_MAP_PIXELS) * SHADOW_MAP_PIXELS);
        std::vector<std::uint32_t> overdraw(pixels);
        for (std::uint32_t mode = 0; mode < NeuronCore::DEBUG_VIEW_COUNT; ++mode)
        {
          const auto debug = static_cast<NeuronCore::DebugView>(mode);
          const NeuronClient::SplatPass& splat = debug == NeuronCore::DebugView::Overdraw ? overdrawSplat : viewSplat;
          _device.Execute(
            [&](ID3D12GraphicsCommandList* _list)
            {
              std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
              _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
              shadowMap.BeginSplat(_list);
              shadowSplat.Record(_list, scene, shadowViewConstants, pushed.constants, pushed.draws);
              shadowMap.EndSplat(_list);
              targets.BeginSplat(_list);
              if (splat.CountsOverdraw())
              {
                targets.BeginOverdraw(_list);
              }
              splat.Record(_list, scene, viewConstants, pushed.constants, pushed.draws, targets.OverdrawWriteTable());
              if (splat.CountsOverdraw())
              {
                targets.EndOverdraw(_list);
              }
              targets.EndSplat(_list);
              const D3D12_CPU_DESCRIPTOR_HANDLE target = rtvHeap.Cpu(colorView);
              _list->OMSetRenderTargets(1, &target, FALSE, nullptr);
              debugView.Record(_list, targets, shadowMap, scene, viewConstants, pushed.constants,
                               static_cast<std::uint32_t>(placements.size()), debug);
            });
          const std::vector<std::byte> visibilityBytes = NeuronClient::ReadTexture2D(
            _device, targets.Visibility(), NeuronClient::ViewTargets::READABLE, NeuronClient::ViewTargets::VISIBILITY_BYTES_PER_PIXEL);
          std::memcpy(visibility.data(), visibilityBytes.data(), visibilityBytes.size());
          const std::vector<std::byte> shadowBytes = NeuronClient::ReadTexture2D(
            _device, shadowMap.Depth(), NeuronClient::ShadowMap::READABLE, NeuronClient::ShadowMap::BYTES_PER_TEXEL);
          std::memcpy(shadowDepth.data(), shadowBytes.data(), shadowBytes.size());
          if (splat.CountsOverdraw())
          {
            const std::vector<std::byte> overdrawBytes = NeuronClient::ReadTexture2D(
              _device, targets.Overdraw(), NeuronClient::ViewTargets::READABLE, NeuronClient::ViewTargets::OVERDRAW_BYTES_PER_PIXEL);
            std::memcpy(overdraw.data(), overdrawBytes.data(), overdrawBytes.size());
          }
          const std::vector<std::byte> colorBytes =
            NeuronClient::ReadTexture2D(_device, color.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, COLOR_BYTES_PER_PIXEL);
          std::vector<float> colors(pixels * 4);
          std::memcpy(colors.data(), colorBytes.data(), colorBytes.size());

          std::uint32_t lit = 0;
          for (std::uint32_t y = 0; y < view.heightPixels; ++y)
          {
            for (std::uint32_t x = 0; x < view.widthPixels; ++x)
            {
              const std::size_t pixel = static_cast<std::size_t>(y) * view.widthPixels + x;
              Float3 expected{0.0f, 0.0f, 0.0f};
              if (debug == NeuronCore::DebugView::ShadowMap)
              {
                std::uint32_t texelX = 0;
                std::uint32_t texelY = 0;
                if (NeuronCore::ShadowMapViewTexel(x, y, view.widthPixels, view.heightPixels, SHADOW_MAP_PIXELS, SHADOW_MAP_PIXELS, texelX,
                                                   texelY))
                {
                  expected = NeuronCore::ShadowMapViewColor(shadowDepth[static_cast<std::size_t>(texelY) * SHADOW_MAP_PIXELS + texelX]);
                }
              }
              else if (debug == NeuronCore::DebugView::Overdraw)
              {
                expected = NeuronCore::OverdrawViewColor(overdraw[pixel]);
              }
              else
              {
                const std::uint32_t voxel = visibility[2 * pixel];
                Float3 albedo{};
                if (const std::optional<NeuronCore::PlacedVoxel> placed = NeuronCore::FindVoxel(placements, voxel))
                {
                  albedo = scene.PaletteValues(placed->paletteIndex)
                             .materials[NeuronCore::UnpackVoxelRecord(model.records[placed->record]).color]
                             .albedo;
                }
                expected = NeuronCore::DebugViewColor(debug, voxel, NeuronCore::UnpackOctahedralNormal(visibility[2 * pixel + 1]), albedo);
              }
              const Float3 actual{colors[4 * pixel], colors[4 * pixel + 1], colors[4 * pixel + 2]};
              const bool close = std::abs(actual.x - expected.x) <= COLOR_TOLERANCE && std::abs(actual.y - expected.y) <= COLOR_TOLERANCE &&
                                 std::abs(actual.z - expected.z) <= COLOR_TOLERANCE;
              Assert::IsTrue(close, std::format(L"view {}, pixel ({}, {}): ({}, {}, {}), the twin's ({}, {}, {})", mode, x, y, actual.x,
                                                actual.y, actual.z, expected.x, expected.y, expected.z)
                                      .c_str());
              lit += actual.x > 0.0f || actual.y > 0.0f || actual.z > 0.0f ? 1u : 0u;
            }
          }
          Assert::IsTrue(lit > 0, std::format(L"view {} shows something", mode).c_str());
        }
      });
  }
};

} // namespace NeuronClientTests
