#include "pch.h"

#include "TestSupport.h"

#include "DescriptorHeap.h"
#include "FailureReport.h"
#include "GpuResources.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

// _relative in _start or the nearest directory above it that holds it.
[[nodiscard]] std::optional<std::filesystem::path> FindAbove(const std::filesystem::path& _start, const std::filesystem::path& _relative)
{
  for (std::filesystem::path directory = _start; !directory.empty(); directory = directory.parent_path())
  {
    if (std::filesystem::exists(directory / _relative))
    {
      return directory / _relative;
    }
    if (directory == directory.parent_path())
    {
      break;
    }
  }
  return std::nullopt;
}

} // namespace

NeuronCore::VoxModel LoadMilitaryStation()
{
  const std::filesystem::path relative = std::filesystem::path("GameData") / "MilitaryStation.vox";
  std::optional<std::filesystem::path> found = FindAbove(std::filesystem::current_path(), relative);
  if (!found)
  {
    found = FindAbove(std::filesystem::path(std::source_location::current().file_name()).parent_path(), relative);
  }
  Assert::IsTrue(found.has_value(), L"GameData/MilitaryStation.vox is not above the working directory or the test sources");
  auto model = NeuronCore::LoadVoxModel(found.value_or(std::filesystem::path()));
  Assert::IsTrue(model.has_value(), L"MilitaryStation.vox was refused");
  return std::move(*model);
}

void RunGpuTest(const std::function<void(NeuronClient::GraphicsDevice&)>& _body)
{
  NeuronClient::InstallFailureReport();
  std::unique_ptr<NeuronClient::GraphicsDevice> device;
  std::string failure;
  try
  {
    device = std::make_unique<NeuronClient::GraphicsDevice>(NeuronClient::GraphicsDeviceDesc{true, std::nullopt, true, false});
    Logger::WriteMessage(std::format(L"{}, debug layer {}\n", device->AdapterName(),
                                     device->DebugLayer() == NeuronClient::DebugLayerState::On ? L"on" : L"unavailable")
                           .c_str());
    _body(*device);
  }
  catch (const winrt::hresult_error&)
  {
    failure = NeuronClient::DescribeCurrentException();
  }
  if (device)
  {
    const std::string report = device->DescribeRemoval();
    if (!report.empty())
    {
      failure += (failure.empty() ? "" : "\n") + report;
    }
  }
  if (!failure.empty())
  {
    Assert::Fail(winrt::to_hstring(failure).c_str());
  }
}

SplatImage RenderSplat(NeuronClient::GraphicsDevice& _device, const NeuronClient::VoxelScene& _scene,
                       const NeuronClient::ViewSplatPass& _pass, const NeuronCore::PerspectiveView& _view)
{
  NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, L"Test render target views");
  NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, L"Test depth stencil views");
  NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true, L"Test shader views");
  NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false, L"Test CPU-only views");
  NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
  targets.Resize(_device, _view.widthPixels, _view.heightPixels);
  NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Test constants");
  const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeViewConstants(_view));

  _device.Execute(
    [&](ID3D12GraphicsCommandList* _list)
    {
      std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
      _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
      targets.BeginSplat(_list);
      _pass.Record(_list, _scene, viewConstants);
      targets.EndSplat(_list);
    });

  const std::vector<std::byte> visibility = NeuronClient::ReadTexture2D(
    _device, targets.Visibility(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NeuronClient::ViewTargets::VISIBILITY_BYTES_PER_PIXEL);
  const std::vector<std::byte> depth =
    NeuronClient::ReadTexture2D(_device, targets.Depth(), D3D12_RESOURCE_STATE_DEPTH_WRITE, sizeof(float));
  SplatImage image{_view.widthPixels, _view.heightPixels, std::vector<std::uint32_t>(visibility.size() / sizeof(std::uint32_t)),
                   std::vector<float>(depth.size() / sizeof(float))};
  std::memcpy(image.visibility.data(), visibility.data(), visibility.size());
  std::memcpy(image.depth.data(), depth.data(), depth.size());
  return image;
}

NeuronCore::Box RecordBox(const NeuronCore::VoxModel& _model, std::uint32_t _record)
{
  const auto instance =
    std::ranges::find_if(_model.instances, [_record](const NeuronCore::ModelInstance& _instance)
                         { return _record >= _instance.firstRecord && _record - _instance.firstRecord < _instance.recordCount; });
  Assert::IsTrue(instance != _model.instances.end(), L"a record index outside every instance");
  return NeuronCore::VoxelBox(*instance, _model.records[_record]);
}

} // namespace NeuronClientTests
