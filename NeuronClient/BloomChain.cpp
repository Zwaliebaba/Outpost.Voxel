#include "pch.h"

#include "BloomChain.h"

#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"

#include <format>
#include <string>
#include <utility>

namespace NeuronClient
{

BloomChain::BloomChain(DescriptorHeap& _shaderHeap)
  : m_shaderHeap(_shaderHeap)
{
  for (std::uint32_t level = 0; level < NeuronCore::BLOOM_MAX_LEVELS; ++level)
  {
    m_srvs[level] = _shaderHeap.Allocate();
    m_uavs[level] = _shaderHeap.Allocate();
  }
}

void BloomChain::Resize(const GraphicsDevice& _device, std::uint32_t _widthPixels, std::uint32_t _heightPixels)
{
  m_levels.clear();
  const std::uint32_t count = NeuronCore::BloomLevelCount(_widthPixels, _heightPixels);
  std::uint32_t width = _widthPixels;
  std::uint32_t height = _heightPixels;
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = FORMAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = FORMAT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  for (std::uint32_t level = 0; level < count; ++level)
  {
    width = NeuronCore::BloomLevelPixels(width);
    height = NeuronCore::BloomLevelPixels(height);
    const std::wstring name = std::format(L"Bloom level {}", level + 1);
    winrt::com_ptr<ID3D12Resource> texture =
      CreateTexture2D(_device, FORMAT, width, height, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, READABLE, nullptr, name.c_str());
    _device.Device()->CreateShaderResourceView(texture.get(), &srv, m_shaderHeap.Cpu(m_srvs[level]));
    _device.Device()->CreateUnorderedAccessView(texture.get(), nullptr, &uav, m_shaderHeap.Cpu(m_uavs[level]));
    m_levels.push_back({std::move(texture), width, height});
  }
}

ID3D12Resource* BloomChain::Texture(std::uint32_t _level) const noexcept
{
  return m_levels[_level].texture.get();
}

std::uint32_t BloomChain::WidthPixels(std::uint32_t _level) const noexcept
{
  return m_levels[_level].widthPixels;
}

std::uint32_t BloomChain::HeightPixels(std::uint32_t _level) const noexcept
{
  return m_levels[_level].heightPixels;
}

D3D12_GPU_DESCRIPTOR_HANDLE BloomChain::LevelTable(std::uint32_t _level) const noexcept
{
  return m_shaderHeap.Gpu(m_srvs[_level]);
}

D3D12_GPU_DESCRIPTOR_HANDLE BloomChain::LevelWriteTable(std::uint32_t _level) const noexcept
{
  return m_shaderHeap.Gpu(m_uavs[_level]);
}

} // namespace NeuronClient
