#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstdint>

namespace NeuronClient
{

class GraphicsDevice;

// A fixed number of descriptors of one type, handed out in order and never returned: the renderer's descriptors live
// as long as the renderer, and a resize rewrites them in place.
class DescriptorHeap
{
public:
  DescriptorHeap(const GraphicsDevice& _device, D3D12_DESCRIPTOR_HEAP_TYPE _type, std::uint32_t _capacity, bool _shaderVisible,
                 const wchar_t* _name);

  // The index of an unused descriptor. Throws when the heap is full.
  [[nodiscard]] std::uint32_t Allocate();

  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE Cpu(std::uint32_t _index) const noexcept;

  // Only for a shader-visible heap.
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE Gpu(std::uint32_t _index) const noexcept;

  [[nodiscard]] ID3D12DescriptorHeap* Heap() const noexcept
  {
    return m_heap.get();
  }

private:
  winrt::com_ptr<ID3D12DescriptorHeap> m_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart{};
  D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart{};
  std::uint32_t m_incrementBytes = 0;
  std::uint32_t m_capacity = 0;
  std::uint32_t m_allocated = 0;
};

} // namespace NeuronClient
