#include "pch.h"

#include "GpuResources.h"

#include "GraphicsDevice.h"

#include <cstring>
#include <string_view>

namespace NeuronClient
{
namespace
{

[[nodiscard]] D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE _heap) noexcept
{
  return {_heap, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
}

[[nodiscard]] D3D12_RESOURCE_STATES BufferState(D3D12_HEAP_TYPE _heap) noexcept
{
  switch (_heap)
  {
  case D3D12_HEAP_TYPE_UPLOAD:
    return D3D12_RESOURCE_STATE_GENERIC_READ;
  case D3D12_HEAP_TYPE_READBACK:
    return D3D12_RESOURCE_STATE_COPY_DEST;
  default:
    return D3D12_RESOURCE_STATE_COMMON;
  }
}

} // namespace

winrt::com_ptr<ID3D12Resource> CreateBuffer(const GraphicsDevice& _device, D3D12_HEAP_TYPE _heap, std::uint64_t _sizeBytes,
                                            const wchar_t* _name, D3D12_RESOURCE_FLAGS _flags)
{
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(_heap);
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = _sizeBytes > 0 ? _sizeBytes : 1;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc = {1, 0};
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = _flags;
  winrt::com_ptr<ID3D12Resource> buffer;
  winrt::check_hresult(
    _device.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, BufferState(_heap), nullptr, IID_PPV_ARGS(buffer.put())));
  buffer->SetName(_name);
  return buffer;
}

winrt::com_ptr<ID3D12Resource> CreateStaticBuffer(GraphicsDevice& _device, std::span<const std::byte> _bytes, const wchar_t* _name,
                                                  D3D12_RESOURCE_FLAGS _flags)
{
  winrt::com_ptr<ID3D12Resource> buffer = CreateBuffer(_device, D3D12_HEAP_TYPE_DEFAULT, _bytes.size(), _name, _flags);
  if (_bytes.empty())
  {
    return buffer;
  }
  const winrt::com_ptr<ID3D12Resource> upload = CreateBuffer(_device, D3D12_HEAP_TYPE_UPLOAD, _bytes.size(), L"Static buffer upload");
  void* mapped = nullptr;
  const D3D12_RANGE nothingRead{0, 0};
  winrt::check_hresult(upload->Map(0, &nothingRead, &mapped));
  std::memcpy(mapped, _bytes.data(), _bytes.size());
  upload->Unmap(0, nullptr);
  _device.Execute([&](ID3D12GraphicsCommandList* _list) { _list->CopyBufferRegion(buffer.get(), 0, upload.get(), 0, _bytes.size()); });
  return buffer;
}

winrt::com_ptr<ID3D12Resource> CreateTexture2D(const GraphicsDevice& _device, DXGI_FORMAT _format, std::uint32_t _widthPixels,
                                               std::uint32_t _heightPixels, D3D12_RESOURCE_FLAGS _flags, D3D12_RESOURCE_STATES _state,
                                               const D3D12_CLEAR_VALUE* _clear, const wchar_t* _name)
{
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = _widthPixels;
  desc.Height = _heightPixels;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = _format;
  desc.SampleDesc = {1, 0};
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = _flags;
  winrt::com_ptr<ID3D12Resource> texture;
  winrt::check_hresult(
    _device.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, _state, _clear, IID_PPV_ARGS(texture.put())));
  texture->SetName(_name);
  return texture;
}

std::vector<std::byte> ReadTexture2D(GraphicsDevice& _device, ID3D12Resource* _texture, D3D12_RESOURCE_STATES _state,
                                     std::uint32_t _bytesPerPixel)
{
  const D3D12_RESOURCE_DESC desc = _texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows = 0;
  UINT64 rowBytes = 0;
  UINT64 totalBytes = 0;
  _device.Device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
  const winrt::com_ptr<ID3D12Resource> readback = CreateBuffer(_device, D3D12_HEAP_TYPE_READBACK, totalBytes, L"Texture readback");

  _device.Execute(
    [&](ID3D12GraphicsCommandList* _list)
    {
      const D3D12_RESOURCE_BARRIER toCopy = Transition(_texture, _state, D3D12_RESOURCE_STATE_COPY_SOURCE);
      _list->ResourceBarrier(1, &toCopy);
      D3D12_TEXTURE_COPY_LOCATION destination{};
      destination.pResource = readback.get();
      destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      destination.PlacedFootprint = footprint;
      D3D12_TEXTURE_COPY_LOCATION source{};
      source.pResource = _texture;
      source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      source.SubresourceIndex = 0;
      _list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
      const D3D12_RESOURCE_BARRIER back = Transition(_texture, D3D12_RESOURCE_STATE_COPY_SOURCE, _state);
      _list->ResourceBarrier(1, &back);
    });

  const std::size_t packedRowBytes = static_cast<std::size_t>(desc.Width) * _bytesPerPixel;
  std::vector<std::byte> pixels(packedRowBytes * rows);
  void* mapped = nullptr;
  const D3D12_RANGE everything{0, static_cast<SIZE_T>(totalBytes)};
  winrt::check_hresult(readback->Map(0, &everything, &mapped));
  const auto* source = static_cast<const std::byte*>(mapped);
  for (UINT row = 0; row < rows; ++row)
  {
    std::memcpy(pixels.data() + row * packedRowBytes,
                source + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch, packedRowBytes);
  }
  const D3D12_RANGE nothingWritten{0, 0};
  readback->Unmap(0, &nothingWritten);
  return pixels;
}

std::vector<std::byte> ReadBuffer(GraphicsDevice& _device, ID3D12Resource* _buffer, D3D12_RESOURCE_STATES _state, std::uint64_t _sizeBytes)
{
  const winrt::com_ptr<ID3D12Resource> readback = CreateBuffer(_device, D3D12_HEAP_TYPE_READBACK, _sizeBytes, L"Buffer readback");
  _device.Execute(
    [&](ID3D12GraphicsCommandList* _list)
    {
      if (_state != D3D12_RESOURCE_STATE_COMMON)
      {
        const D3D12_RESOURCE_BARRIER toCopy = Transition(_buffer, _state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        _list->ResourceBarrier(1, &toCopy);
      }
      _list->CopyBufferRegion(readback.get(), 0, _buffer, 0, _sizeBytes);
      if (_state != D3D12_RESOURCE_STATE_COMMON)
      {
        const D3D12_RESOURCE_BARRIER back = Transition(_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, _state);
        _list->ResourceBarrier(1, &back);
      }
    });
  std::vector<std::byte> bytes(static_cast<std::size_t>(_sizeBytes));
  void* mapped = nullptr;
  const D3D12_RANGE everything{0, static_cast<SIZE_T>(_sizeBytes)};
  winrt::check_hresult(readback->Map(0, &everything, &mapped));
  std::memcpy(bytes.data(), mapped, bytes.size());
  const D3D12_RANGE nothingWritten{0, 0};
  readback->Unmap(0, &nothingWritten);
  return bytes;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* _resource, D3D12_RESOURCE_STATES _before, D3D12_RESOURCE_STATES _after) noexcept
{
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = _resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = _before;
  barrier.Transition.StateAfter = _after;
  return barrier;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC DefaultGraphicsPipeline() noexcept
{
  // Every enumeration without a zero value is given one of its own, so nothing is left zero-filled.
  constexpr D3D12_RENDER_TARGET_BLEND_DESC NO_BLENDING{FALSE,
                                                       FALSE,
                                                       D3D12_BLEND_ONE,
                                                       D3D12_BLEND_ZERO,
                                                       D3D12_BLEND_OP_ADD,
                                                       D3D12_BLEND_ONE,
                                                       D3D12_BLEND_ZERO,
                                                       D3D12_BLEND_OP_ADD,
                                                       D3D12_LOGIC_OP_NOOP,
                                                       D3D12_COLOR_WRITE_ENABLE_ALL};
  constexpr D3D12_DEPTH_STENCILOP_DESC KEEP{D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                                            D3D12_COMPARISON_FUNC_ALWAYS};
  return {
    .pRootSignature = nullptr,
    .VS = {},
    .PS = {},
    .DS = {},
    .HS = {},
    .GS = {},
    .StreamOutput = {},
    .BlendState = {FALSE, FALSE, {NO_BLENDING, NO_BLENDING, NO_BLENDING, NO_BLENDING, NO_BLENDING, NO_BLENDING, NO_BLENDING, NO_BLENDING}},
    .SampleMask = UINT_MAX,
    .RasterizerState = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK, FALSE, D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
                        D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF},
    .DepthStencilState = {TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS, FALSE, D3D12_DEFAULT_STENCIL_READ_MASK,
                          D3D12_DEFAULT_STENCIL_WRITE_MASK, KEEP, KEEP},
    .InputLayout = {},
    .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 0,
    .RTVFormats = {},
    .DSVFormat = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {1, 0},
    .NodeMask = 0,
    .CachedPSO = {},
    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
  };
}

winrt::com_ptr<ID3D12RootSignature> CreateRootSignature(const GraphicsDevice& _device, const D3D12_ROOT_SIGNATURE_DESC& _desc,
                                                        const wchar_t* _name)
{
  winrt::com_ptr<ID3DBlob> serialized;
  winrt::com_ptr<ID3DBlob> errors;
  const HRESULT result = D3D12SerializeRootSignature(&_desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.put(), errors.put());
  if (FAILED(result) && errors)
  {
    // The serializer says what is wrong with the description; the HRESULT alone would not.
    throw winrt::hresult_error(
      result, winrt::to_hstring(std::string_view(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())));
  }
  winrt::check_hresult(result);
  winrt::com_ptr<ID3D12RootSignature> rootSignature;
  winrt::check_hresult(_device.Device()->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                             IID_PPV_ARGS(rootSignature.put())));
  rootSignature->SetName(_name);
  return rootSignature;
}

} // namespace NeuronClient
