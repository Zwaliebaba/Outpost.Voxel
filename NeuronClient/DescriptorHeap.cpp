#include "pch.h"

#include "DescriptorHeap.h"

#include "GraphicsDevice.h"

#include <stdexcept>

namespace NeuronClient
{

DescriptorHeap::DescriptorHeap(const GraphicsDevice& _device, D3D12_DESCRIPTOR_HEAP_TYPE _type, std::uint32_t _capacity,
                               bool _shaderVisible, const wchar_t* _name)
  : m_capacity(_capacity)
{
  const D3D12_DESCRIPTOR_HEAP_DESC desc{_type, _capacity,
                                        _shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
  winrt::check_hresult(_device.Device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_heap.put())));
  m_heap->SetName(_name);
  m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
  if (_shaderVisible)
  {
    m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
  }
  m_incrementBytes = _device.Device()->GetDescriptorHandleIncrementSize(_type);
}

std::uint32_t DescriptorHeap::Allocate()
{
  if (m_allocated == m_capacity)
  {
    throw std::length_error("A descriptor heap is full; its capacity is set where the renderer creates it.");
  }
  return m_allocated++;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::Cpu(std::uint32_t _index) const noexcept
{
  return {m_cpuStart.ptr + static_cast<SIZE_T>(_index) * m_incrementBytes};
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::Gpu(std::uint32_t _index) const noexcept
{
  return {m_gpuStart.ptr + static_cast<UINT64>(_index) * m_incrementBytes};
}

} // namespace NeuronClient
