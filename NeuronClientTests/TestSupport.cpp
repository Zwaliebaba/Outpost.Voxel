#include "pch.h"

#include "TestSupport.h"

#include "DescriptorHeap.h"
#include "FailureReport.h"
#include "GpuResources.h"
#include "ShadowMap.h"
#include "ShadowViewConstants.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"

#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
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

SplatImage RenderSplat(NeuronClient::GraphicsDevice& _device, const NeuronClient::VoxelScene& _scene, const NeuronClient::SplatPass& _pass,
                       const NeuronCore::PerspectiveView& _view, const std::optional<NeuronClient::ExplosionConstants>& _explosion)
{
  NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, L"Test render target views");
  NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, L"Test depth stencil views");
  NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 12, true, L"Test shader views");
  NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false, L"Test CPU-only views");
  NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
  targets.Resize(_device, _view.widthPixels, _view.heightPixels);
  NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Test constants");
  const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeViewConstants(_view));
  const D3D12_GPU_VIRTUAL_ADDRESS explosionConstants = _explosion ? constants.Push(*_explosion) : 0;

  _device.Execute(
    [&](ID3D12GraphicsCommandList* _list)
    {
      std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
      _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
      targets.BeginSplat(_list);
      if (_pass.CountsOverdraw())
      {
        targets.BeginOverdraw(_list);
      }
      _pass.Record(_list, _scene, viewConstants, explosionConstants, targets.OverdrawWriteTable());
      if (_pass.CountsOverdraw())
      {
        targets.EndOverdraw(_list);
      }
      targets.EndSplat(_list);
    });

  const std::vector<std::byte> visibility = NeuronClient::ReadTexture2D(_device, targets.Visibility(), NeuronClient::ViewTargets::READABLE,
                                                                        NeuronClient::ViewTargets::VISIBILITY_BYTES_PER_PIXEL);
  const std::vector<std::byte> depth =
    NeuronClient::ReadTexture2D(_device, targets.Depth(), NeuronClient::ViewTargets::READABLE, sizeof(float));
  SplatImage image{_view.widthPixels,
                   _view.heightPixels,
                   std::vector<std::uint32_t>(visibility.size() / sizeof(std::uint32_t)),
                   std::vector<float>(depth.size() / sizeof(float)),
                   {}};
  std::memcpy(image.visibility.data(), visibility.data(), visibility.size());
  std::memcpy(image.depth.data(), depth.data(), depth.size());
  if (_pass.CountsOverdraw())
  {
    const std::vector<std::byte> overdraw = NeuronClient::ReadTexture2D(_device, targets.Overdraw(), NeuronClient::ViewTargets::READABLE,
                                                                        NeuronClient::ViewTargets::OVERDRAW_BYTES_PER_PIXEL);
    image.overdraw.resize(overdraw.size() / sizeof(std::uint32_t));
    std::memcpy(image.overdraw.data(), overdraw.data(), overdraw.size());
  }
  return image;
}

std::vector<float> RenderShadowSplat(NeuronClient::GraphicsDevice& _device, const NeuronClient::VoxelScene& _scene,
                                     const NeuronClient::SplatPass& _pass, const NeuronCore::OrthographicView& _view,
                                     const std::optional<NeuronClient::ExplosionConstants>& _explosion)
{
  NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, L"Test depth stencil views");
  NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, L"Test shader views");
  const NeuronClient::ShadowMap map(_device, dsvHeap, shaderHeap, _view.widthPixels);
  NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Test constants");
  const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeShadowViewConstants(_view));
  const D3D12_GPU_VIRTUAL_ADDRESS explosionConstants = _explosion ? constants.Push(*_explosion) : 0;

  _device.Execute(
    [&](ID3D12GraphicsCommandList* _list)
    {
      std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
      _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
      map.BeginSplat(_list);
      _pass.Record(_list, _scene, viewConstants, explosionConstants);
      map.EndSplat(_list);
    });

  const std::vector<std::byte> bytes =
    NeuronClient::ReadTexture2D(_device, map.Depth(), NeuronClient::ShadowMap::READABLE, NeuronClient::ShadowMap::BYTES_PER_TEXEL);
  std::vector<float> depth(bytes.size() / sizeof(float));
  std::memcpy(depth.data(), bytes.data(), bytes.size());
  return depth;
}

float HalfToFloat(std::uint16_t _half) noexcept
{
  const std::uint32_t sign = static_cast<std::uint32_t>(_half & 0x8000u) << 16u;
  const std::uint32_t exponent = (_half >> 10u) & 0x1Fu;
  const std::uint32_t mantissa = _half & 0x3FFu;
  if (exponent == 0u)
  {
    // Zero or subnormal: mantissa × 2^-24, exact in single precision.
    const float magnitude = static_cast<float>(mantissa) * 5.9604644775390625e-8f;
    return sign != 0u ? -magnitude : magnitude;
  }
  const std::uint32_t bits =
    exponent == 0x1Fu ? (sign | 0x7F800000u | (mantissa << 13u)) : (sign | ((exponent + 112u) << 23u) | (mantissa << 13u));
  return std::bit_cast<float>(bits);
}

NeuronCore::OrthographicView TestShadowView(const NeuronCore::VoxModel& _model, NeuronCore::Float3 _toSun, float _halfExtent,
                                            std::uint32_t _sizePixels)
{
  constexpr float NONE = std::numeric_limits<float>::infinity();
  NeuronCore::Float3 lower{NONE, NONE, NONE};
  NeuronCore::Float3 upper{-NONE, -NONE, -NONE};
  for (const NeuronCore::ModelInstance& instance : _model.instances)
  {
    const NeuronCore::Float3 minimum{static_cast<float>(instance.origin.x), static_cast<float>(instance.origin.y),
                                     static_cast<float>(instance.origin.z)};
    const NeuronCore::Float3 maximum =
      minimum +
      NeuronCore::Float3{static_cast<float>(instance.size.x), static_cast<float>(instance.size.y), static_cast<float>(instance.size.z)};
    lower = {std::min(lower.x, minimum.x), std::min(lower.y, minimum.y), std::min(lower.z, minimum.z)};
    upper = {std::max(upper.x, maximum.x), std::max(upper.y, maximum.y), std::max(upper.z, maximum.z)};
  }
  const NeuronCore::Float3 center = (lower + upper) * 0.5f;
  lower.z = std::min(lower.z, 0.0f);
  return NeuronCore::MakeShadowView(_toSun, center, _halfExtent, lower, upper, _sizePixels);
}

NeuronCore::VoxModel RandomBlock()
{
  NeuronCore::VoxModel model{};
  model.version = 150;
  std::uint32_t state = 12345u;
  for (std::uint32_t z = 0; z < 8; ++z)
  {
    for (std::uint32_t y = 0; y < 8; ++y)
    {
      for (std::uint32_t x = 0; x < 8; ++x)
      {
        state = state * 1664525u + 1013904223u;
        if ((state >> 24u) < 96u)
        {
          model.records.push_back(NeuronCore::PackVoxelRecord({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y),
                                                               static_cast<std::uint8_t>(z), static_cast<std::uint8_t>(x % 16u)}));
        }
      }
    }
  }
  model.instances.push_back({{0, 0, 0}, {8, 8, 8}, 0, static_cast<std::uint32_t>(model.records.size())});
  for (NeuronCore::PaletteEntry& entry : model.palette)
  {
    entry = {128, 128, 128, 255, false, 0.0f, 0.0f};
  }
  return model;
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
