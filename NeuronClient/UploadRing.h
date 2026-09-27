#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace NeuronClient
{

class GraphicsDevice;

// Constants the CPU writes for one frame and the GPU reads in place from an upload heap (Design/SampleRenderer.md §8),
// in pieces aligned for a constant buffer view. A frame in flight owns one ring and resets it only after the GPU has
// finished the frame that last used it.
class UploadRing
{
public:
  UploadRing(const GraphicsDevice& _device, std::uint64_t _capacityBytes, const wchar_t* _name);
  ~UploadRing();

  UploadRing(const UploadRing&) = delete;
  UploadRing& operator=(const UploadRing&) = delete;
  UploadRing(UploadRing&&) = delete;
  UploadRing& operator=(UploadRing&&) = delete;

  void Reset() noexcept
  {
    m_usedBytes = 0;
  }

  // Copies _bytes in and returns their GPU address. Throws when the ring is full.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Push(std::span<const std::byte> _bytes);

  template <typename T> [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Push(const T& _value)
  {
    static_assert(std::is_trivially_copyable_v<T>, "constants are copied as bytes");
    return Push(std::as_bytes(std::span<const T, 1>(&_value, 1)));
  }

private:
  winrt::com_ptr<ID3D12Resource> m_buffer;
  std::byte* m_mapped = nullptr;
  std::uint64_t m_capacityBytes = 0;
  std::uint64_t m_usedBytes = 0;
};

} // namespace NeuronClient
