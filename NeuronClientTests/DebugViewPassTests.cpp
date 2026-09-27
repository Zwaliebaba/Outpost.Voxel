#include "pch.h"

#include "DebugViewPass.h"
#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "PaletteConstants.h"
#include "TestSupport.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewSplatPass.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "DebugView.h"
#include "Float3.h"
#include "OctahedralNormal.h"
#include "PerspectiveView.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float3;

constexpr DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr std::uint32_t COLOR_BYTES_PER_PIXEL = 16;
// The shader and the twin agree to rounding: the headlight's dot product may contract differently on each side.
constexpr float COLOR_TOLERANCE = 1.0e-5f;

} // namespace

// The debug view pass against its twin, DebugViewColor, on the visibility buffer the GPU wrote (§11, R15).
TEST_CLASS(DebugViewPassTests)
{
public:
  TEST_METHOD(ShowsWhatTheTwinComputes)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronClient::VoxelScene scene(_device, model);
        const NeuronClient::ViewSplatPass splat(_device);
        const NeuronClient::DebugViewPass debugView(_device, COLOR_FORMAT);
        const Float3 center{0.5f, 0.5f, 127.5f};
        const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView(
          center + Float3{-318.43f, -318.43f, 260.0f}, center, {0.0f, 0.0f, 1.0f}, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE, 161, 91);

        NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false, L"Test render target views");
        NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, L"Test depth stencil views");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true, L"Test shader views");
        NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false, L"Test CPU-only views");
        NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
        targets.Resize(_device, view.widthPixels, view.heightPixels);
        const winrt::com_ptr<ID3D12Resource> color =
          NeuronClient::CreateTexture2D(_device, COLOR_FORMAT, view.widthPixels, view.heightPixels, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, L"Test color");
        const std::uint32_t colorView = rtvHeap.Allocate();
        _device.Device()->CreateRenderTargetView(color.get(), nullptr, rtvHeap.Cpu(colorView));
        NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Test constants");
        const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeViewConstants(view));

        const std::size_t pixels = static_cast<std::size_t>(view.widthPixels) * view.heightPixels;
        std::vector<std::uint32_t> visibility(pixels * 2);
        for (std::uint32_t mode = 0; mode < NeuronCore::DEBUG_VIEW_COUNT; ++mode)
        {
          const auto debug = static_cast<NeuronCore::DebugView>(mode);
          _device.Execute(
            [&](ID3D12GraphicsCommandList* _list)
            {
              std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
              _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
              targets.BeginSplat(_list);
              splat.Record(_list, scene, viewConstants);
              targets.EndSplat(_list);
              const D3D12_CPU_DESCRIPTOR_HANDLE target = rtvHeap.Cpu(colorView);
              _list->OMSetRenderTargets(1, &target, FALSE, nullptr);
              debugView.Record(_list, targets, scene, viewConstants, debug);
            });
          const std::vector<std::byte> visibilityBytes =
            NeuronClient::ReadTexture2D(_device, targets.Visibility(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                        NeuronClient::ViewTargets::VISIBILITY_BYTES_PER_PIXEL);
          std::memcpy(visibility.data(), visibilityBytes.data(), visibilityBytes.size());
          const std::vector<std::byte> colorBytes =
            NeuronClient::ReadTexture2D(_device, color.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, COLOR_BYTES_PER_PIXEL);
          std::vector<float> colors(pixels * 4);
          std::memcpy(colors.data(), colorBytes.data(), colorBytes.size());

          std::uint32_t covered = 0;
          for (std::size_t pixel = 0; pixel < pixels; ++pixel)
          {
            const std::uint32_t voxel = visibility[2 * pixel];
            Float3 albedo{};
            if (voxel != NeuronCore::NO_VOXEL)
            {
              ++covered;
              albedo = scene.PaletteValues().materials[NeuronCore::UnpackVoxelRecord(model.records[voxel]).color].albedo;
            }
            const Float3 expected =
              NeuronCore::DebugViewColor(debug, voxel, NeuronCore::UnpackOctahedralNormal(visibility[2 * pixel + 1]), albedo, view.forward);
            const Float3 actual{colors[4 * pixel], colors[4 * pixel + 1], colors[4 * pixel + 2]};
            const bool close = std::abs(actual.x - expected.x) <= COLOR_TOLERANCE && std::abs(actual.y - expected.y) <= COLOR_TOLERANCE &&
                               std::abs(actual.z - expected.z) <= COLOR_TOLERANCE;
            Assert::IsTrue(close, std::format(L"view {}, pixel {}: ({}, {}, {}), the twin's ({}, {}, {})", mode, pixel, actual.x, actual.y,
                                              actual.z, expected.x, expected.y, expected.z)
                                    .c_str());
          }
          Assert::IsTrue(covered > 0, L"the station covers some of the image");
        }
      });
  }
};

} // namespace NeuronClientTests
