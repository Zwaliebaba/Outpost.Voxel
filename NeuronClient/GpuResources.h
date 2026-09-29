#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace NeuronClient
{

class GraphicsDevice;

// A committed buffer in the state its heap requires: an upload buffer is GENERIC_READ, a readback buffer COPY_DEST,
// and a default buffer COMMON, from which a copy or a shader read promotes it implicitly and to which it decays when the
// command list that used it completes.
[[nodiscard]] winrt::com_ptr<ID3D12Resource> CreateBuffer(const GraphicsDevice& _device, D3D12_HEAP_TYPE _heap, std::uint64_t _sizeBytes,
                                                          const wchar_t* _name, D3D12_RESOURCE_FLAGS _flags = D3D12_RESOURCE_FLAG_NONE);

// A default-heap buffer holding _bytes, copied there through a temporary upload buffer. It rests in COMMON.
[[nodiscard]] winrt::com_ptr<ID3D12Resource> CreateStaticBuffer(GraphicsDevice& _device, std::span<const std::byte> _bytes,
                                                                const wchar_t* _name,
                                                                D3D12_RESOURCE_FLAGS _flags = D3D12_RESOURCE_FLAG_NONE);

// Copies the first _sizeBytes of a buffer back to the CPU. The buffer is in _state before and after.
[[nodiscard]] std::vector<std::byte> ReadBuffer(GraphicsDevice& _device, ID3D12Resource* _buffer, D3D12_RESOURCE_STATES _state,
                                                std::uint64_t _sizeBytes);

// A committed 2D texture with one mip level in the default heap.
[[nodiscard]] winrt::com_ptr<ID3D12Resource> CreateTexture2D(const GraphicsDevice& _device, DXGI_FORMAT _format, std::uint32_t _widthPixels,
                                                             std::uint32_t _heightPixels, D3D12_RESOURCE_FLAGS _flags,
                                                             D3D12_RESOURCE_STATES _state, const D3D12_CLEAR_VALUE* _clear,
                                                             const wchar_t* _name);

// Copies a 2D texture back to the CPU with its rows packed tightly, _bytesPerPixel times its width to a row. The
// texture is in _state before and after.
[[nodiscard]] std::vector<std::byte> ReadTexture2D(GraphicsDevice& _device, ID3D12Resource* _texture, D3D12_RESOURCE_STATES _state,
                                                   std::uint32_t _bytesPerPixel);

// A 2D texture's copy on its way back to the CPU: the readback buffer it lands in, and where the texture's rows lie there.
struct TextureReadback
{
  winrt::com_ptr<ID3D12Resource> buffer;
  std::uint64_t sizeBytes;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  std::uint32_t rows;
};

// Records on _list the copy of _texture into a new readback buffer. The texture must be in COPY_SOURCE when the list runs.
[[nodiscard]] TextureReadback RecordTextureReadback(const GraphicsDevice& _device, ID3D12GraphicsCommandList* _list,
                                                    ID3D12Resource* _texture);

// The copied texture's rows, packed tightly, _bytesPerPixel times its width to a row. The GPU must have run the copy.
[[nodiscard]] std::vector<std::byte> ReadTextureReadback(const TextureReadback& _readback, std::uint32_t _bytesPerPixel);

// A transition of every subresource of _resource.
[[nodiscard]] D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* _resource, D3D12_RESOURCE_STATES _before,
                                                D3D12_RESOURCE_STATES _after) noexcept;

// A graphics pipeline description with Direct3D 12's documented defaults in every field that has one: no blending,
// solid fill with back faces culled, a LESS depth test that writes, and no stencil. The enumerations are valid even
// where their feature is off, which the runtime checks.
[[nodiscard]] D3D12_GRAPHICS_PIPELINE_STATE_DESC DefaultGraphicsPipeline() noexcept;

// Serializes a version 1.0 root signature and creates it (Design/Archive/SampleRenderer.md §5).
[[nodiscard]] winrt::com_ptr<ID3D12RootSignature> CreateRootSignature(const GraphicsDevice& _device, const D3D12_ROOT_SIGNATURE_DESC& _desc,
                                                                      const wchar_t* _name);

} // namespace NeuronClient
