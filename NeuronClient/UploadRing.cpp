#include "pch.h"

#include "UploadRing.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"

#include <cstring>
#include <stdexcept>

namespace NeuronClient
{

UploadRing::UploadRing(const GraphicsDevice& _device, std::uint64_t _capacityBytes, const wchar_t* _name)
  : m_buffer(CreateBuffer(_device, D3D12_HEAP_TYPE_UPLOAD, _capacityBytes, _name)),
    m_capacityBytes(_capacityBytes)
{
  void* mapped = nullptr;
  const D3D12_RANGE nothingRead{0, 0};
  winrt::check_hresult(m_buffer->Map(0, &nothingRead, &mapped));
  m_mapped = static_cast<std::byte*>(mapped);
}

UploadRing::~UploadRing()
{
  if (m_buffer && m_mapped != nullptr)
  {
    m_buffer->Unmap(0, nullptr);
  }
}

D3D12_GPU_VIRTUAL_ADDRESS UploadRing::Push(std::span<const std::byte> _bytes)
{
  constexpr std::uint64_t ALIGNMENT_BYTES = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
  const std::uint64_t offset = (m_usedBytes + ALIGNMENT_BYTES - 1) / ALIGNMENT_BYTES * ALIGNMENT_BYTES;
  if (offset + _bytes.size() > m_capacityBytes)
  {
    throw std::length_error("An upload ring is full; its capacity is set where the renderer creates it.");
  }
  std::memcpy(m_mapped + offset, _bytes.data(), _bytes.size());
  m_usedBytes = offset + _bytes.size();
  return m_buffer->GetGPUVirtualAddress() + offset;
}

} // namespace NeuronClient
