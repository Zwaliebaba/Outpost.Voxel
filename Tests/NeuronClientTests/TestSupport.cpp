#include "pch.h"

#include "TestSupport.h"

#include "DescriptorHeap.h"
#include "ExplosionConstants.h"
#include "FailureReport.h"
#include "GpuResources.h"
#include "ShadowMap.h"
#include "ShadowViewConstants.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"

#include "ColorSpace.h"
#include "Half.h"
#include "Lighting.h"
#include "RigidTransform.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

// GameData/_fileName, from above the working directory or above the test sources; a missing file fails the test.
[[nodiscard]] std::filesystem::path FindGameData(const wchar_t* _fileName)
{
  const std::filesystem::path relative = std::filesystem::path("GameData") / _fileName;
  std::optional<std::filesystem::path> found = FindAbove(std::filesystem::current_path(), relative);
  if (!found)
  {
    found = FindAbove(std::filesystem::path(std::source_location::current().file_name()).parent_path(), relative);
  }
  Assert::IsTrue(found.has_value(), std::format(L"GameData/{} is not above the working directory or the test sources", _fileName).c_str());
  return found.value_or(std::filesystem::path());
}

} // namespace

NeuronCore::VoxModel LoadGameData(const wchar_t* _fileName)
{
  auto model = NeuronCore::LoadVoxModel(FindGameData(_fileName));
  Assert::IsTrue(model.has_value(), std::format(L"{} was refused", _fileName).c_str());
  return std::move(*model);
}

std::filesystem::path GameDataDirectory()
{
  return FindGameData(L"MilitaryStation.nvf").parent_path();
}

NeuronCore::VoxModel LoadMilitaryStation()
{
  return LoadGameData(L"MilitaryStation.vox");
}

std::vector<NeuronCore::Placement> WholePlacements(const NeuronCore::VoxModel& _model)
{
  std::vector<NeuronCore::Placement> placements;
  for (std::uint32_t part = 0; part < _model.instances.size(); ++part)
  {
    const NeuronCore::Int3 origin = _model.instances[part].origin;
    const NeuronCore::RigidTransform transform{NeuronCore::IDENTITY_ROTATION,
                                               {static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z)}};
    placements.push_back(NeuronCore::PlacePart(_model, 0, 0, part, transform));
  }
  Assert::IsTrue(NeuronCore::AssignVoxelIds(placements), L"the model's ids fit");
  return placements;
}

NeuronCore::Placement PlaceCentered(const NeuronCore::VoxModel& _model, std::uint32_t _modelIndex, std::uint32_t _modelFirstRecord,
                                    std::uint32_t _part, const NeuronCore::Rotation& _rotation, NeuronCore::Float3 _center)
{
  NeuronCore::Placement placement =
    NeuronCore::PlacePart(_model, _modelIndex, _modelFirstRecord, _part, {NeuronCore::IDENTITY_ROTATION, {0.0f, 0.0f, 0.0f}});
  placement.transform = {_rotation, _center - NeuronCore::RotateVector(_rotation, (placement.lower + placement.upper) * 0.5f)};
  return placement;
}

std::vector<NeuronCore::Placement> DetonatePlacements(std::vector<NeuronCore::Placement> _placements,
                                                      std::span<const NeuronCore::VoxModel> _models,
                                                      const NeuronCore::SceneFragments& _fragments,
                                                      const NeuronCore::ExplosionParameters& _parameters, float _timeSeconds)
{
  const std::vector<std::uint32_t> firstRecords = NeuronCore::ModelFirstRecords(_models);
  for (NeuronCore::Placement& placement : _placements)
  {
    NeuronCore::ExplosionParameters parameters = _parameters;
    parameters.blastOrigin = NeuronCore::InverseTransformPoint(placement.transform, _parameters.blastOrigin);
    parameters.inheritedVelocity = NeuronCore::UnrotateVector(placement.transform.rotation, _parameters.inheritedVelocity);
    // The placement's model is its palette's, and its part the one whose records it draws.
    const std::uint32_t model = placement.paletteIndex;
    const std::vector<NeuronCore::ModelInstance>& instances = _models[model].instances;
    const auto part = std::ranges::find(instances, placement.firstRecord - firstRecords[model], &NeuronCore::ModelInstance::firstRecord);
    Assert::IsTrue(part != instances.end(), L"the placement draws one of its model's parts");
    placement.detonation = NeuronCore::PlacementDetonation{parameters, _timeSeconds,
                                                           _fragments.Part(model, static_cast<std::uint32_t>(part - instances.begin()))};
  }
  return _placements;
}

std::vector<NeuronCore::Box> PlacedBoxes(std::span<const std::uint32_t> _records, std::span<const NeuronCore::Placement> _placements,
                                         float _change)
{
  std::vector<NeuronCore::Box> boxes;
  const float radius = 0.5f + _change;
  for (const NeuronCore::Placement& placement : _placements)
  {
    Assert::AreEqual(static_cast<std::uint32_t>(boxes.size()), placement.firstVoxel, L"the ids run on from placement to placement");
    for (std::uint32_t i = 0; i < placement.recordCount; ++i)
    {
      const NeuronCore::Box box = NeuronCore::PlacedVoxelBox(placement, i, _records[placement.firstRecord + i]);
      boxes.push_back(NeuronCore::MakeOrientedBox(box.center, {radius, radius, radius}, box.axisX, box.axisY, box.axisZ));
    }
  }
  return boxes;
}

NeuronClient::SplatPlacements PushTestPlacements(NeuronClient::UploadRing& _ring, std::span<const NeuronCore::Placement> _placements,
                                                 Permutations _permutations)
{
  NeuronClient::SplatPlacements pushed = NeuronClient::PushSplatPlacements(_ring, _placements);
  if (_permutations == Permutations::AllOriented)
  {
    // A whole placement drawn oriented reads constants at rest, as the renderer gives a whole rigid one.
    D3D12_GPU_VIRTUAL_ADDRESS rest = 0;
    for (NeuronClient::SplatDraw& draw : pushed.draws)
    {
      if (!draw.oriented)
      {
        if (rest == 0)
        {
          rest = _ring.Push(NeuronClient::ExplosionConstants{});
        }
        draw.oriented = true;
        draw.explosion = rest;
      }
    }
  }
  return pushed;
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
                       std::span<const NeuronCore::Placement> _placements, const NeuronClient::SplatPass& _pass,
                       const NeuronCore::PerspectiveView& _view, Permutations _permutations)
{
  NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false, L"Test render target views");
  NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false, L"Test depth stencil views");
  NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 12, true, L"Test shader views");
  NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false, L"Test CPU-only views");
  NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
  targets.Resize(_device, _view.widthPixels, _view.heightPixels);
  NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES + NeuronClient::SplatPlacementBytes(_placements), L"Test constants");
  const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeViewConstants(_view));
  const NeuronClient::SplatPlacements placements = PushTestPlacements(constants, _placements, _permutations);

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
      _pass.Record(_list, _scene, viewConstants, placements.constants, placements.draws, targets.OverdrawWriteTable());
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
                                     std::span<const NeuronCore::Placement> _placements, const NeuronClient::SplatPass& _pass,
                                     const NeuronCore::OrthographicView& _view, Permutations _permutations)
{
  NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, L"Test depth stencil views");
  NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, L"Test shader views");
  const NeuronClient::ShadowMap map(_device, dsvHeap, shaderHeap, _view.widthPixels);
  NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES + NeuronClient::SplatPlacementBytes(_placements), L"Test constants");
  const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeShadowViewConstants(_view));
  const NeuronClient::SplatPlacements placements = PushTestPlacements(constants, _placements, _permutations);

  _device.Execute(
    [&](ID3D12GraphicsCommandList* _list)
    {
      std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
      _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
      map.BeginSplat(_list);
      _pass.Record(_list, _scene, viewConstants, placements.constants, placements.draws);
      map.EndSplat(_list);
    });

  const std::vector<std::byte> bytes =
    NeuronClient::ReadTexture2D(_device, map.Depth(), NeuronClient::ShadowMap::READABLE, NeuronClient::ShadowMap::BYTES_PER_TEXEL);
  std::vector<float> depth(bytes.size() / sizeof(float));
  std::memcpy(depth.data(), bytes.data(), bytes.size());
  return depth;
}

void WriteTexture2D(NeuronClient::GraphicsDevice& _device, ID3D12Resource* _texture, D3D12_RESOURCE_STATES _state,
                    std::span<const std::byte> _pixels, std::uint32_t _bytesPerPixel)
{
  const D3D12_RESOURCE_DESC desc = _texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows = 0;
  UINT64 rowBytes = 0;
  UINT64 totalBytes = 0;
  _device.Device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
  const winrt::com_ptr<ID3D12Resource> upload = NeuronClient::CreateBuffer(_device, D3D12_HEAP_TYPE_UPLOAD, totalBytes, L"Texture upload");
  const std::size_t packedRowBytes = static_cast<std::size_t>(desc.Width) * _bytesPerPixel;
  Assert::AreEqual(packedRowBytes * rows, _pixels.size(), L"the pixels fill the texture");
  void* mapped = nullptr;
  const D3D12_RANGE nothingRead{0, 0};
  winrt::check_hresult(upload->Map(0, &nothingRead, &mapped));
  auto* destination = static_cast<std::byte*>(mapped);
  for (UINT row = 0; row < rows; ++row)
  {
    std::memcpy(destination + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                _pixels.data() + row * packedRowBytes, packedRowBytes);
  }
  upload->Unmap(0, nullptr);

  _device.Execute(
    [&](ID3D12GraphicsCommandList* _list)
    {
      const D3D12_RESOURCE_BARRIER toCopy = NeuronClient::Transition(_texture, _state, D3D12_RESOURCE_STATE_COPY_DEST);
      _list->ResourceBarrier(1, &toCopy);
      D3D12_TEXTURE_COPY_LOCATION target{};
      target.pResource = _texture;
      target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      target.SubresourceIndex = 0;
      D3D12_TEXTURE_COPY_LOCATION source{};
      source.pResource = upload.get();
      source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      source.PlacedFootprint = footprint;
      _list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
      const D3D12_RESOURCE_BARRIER back = NeuronClient::Transition(_texture, D3D12_RESOURCE_STATE_COPY_DEST, _state);
      _list->ResourceBarrier(1, &back);
    });
}

NeuronCore::WorldSettings TestWorld() noexcept
{
  constexpr float RADIANS_PER_DEGREE = 0.0174532925f;
  const float ground = 0.7f * NeuronCore::SrgbToLinear(80);
  return {NeuronCore::SunDirection(50.0f * RADIANS_PER_DEGREE, 50.0f * RADIANS_PER_DEGREE),
          {0.7f, 0.7f, 0.7f},
          0.27f * RADIANS_PER_DEGREE,
          {0.7f, 0.7f, 0.7f},
          {ground, ground, ground},
          1,
          {0.5f, 0.0f, 0.0f, 0.8660254f}};
}

std::uint32_t HalfSteps(float _expected, float _actual) noexcept
{
  // A half at least zero orders as its bits do.
  const auto expected = static_cast<std::int32_t>(NeuronCore::FloatToHalf(_expected));
  const auto actual = static_cast<std::int32_t>(NeuronCore::FloatToHalf(_actual));
  return static_cast<std::uint32_t>(std::abs(actual - expected));
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
  return NeuronCore::MakeShadowView(_toSun, (lower + upper) * 0.5f, _halfExtent, lower, upper, _sizePixels);
}

NeuronCore::VoxModel RandomBlock()
{
  NeuronCore::VoxModel model{};
  model.version = 150;
  std::uint32_t state = 12345u;
  for (std::uint32_t y = 0; y < 8; ++y)
  {
    for (std::uint32_t z = 0; z < 8; ++z)
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
