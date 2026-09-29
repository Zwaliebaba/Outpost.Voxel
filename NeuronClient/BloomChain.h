#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "Bloom.h"

#include <array>
#include <cstdint>
#include <vector>

namespace NeuronClient
{

class DescriptorHeap;
class GraphicsDevice;

// Bloom's chain (Design/Archive/SpaceScene.md §12.2): its levels, the first half the view's size and each after it half the one
// above, rounded up, as many as NeuronCore::BloomLevelCount gives, in the HDR color's format: about 5.5 MB at 1080p.
// Between frames every level rests readable by every shader stage.
class BloomChain
{
public:
  static constexpr DXGI_FORMAT FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
  static constexpr std::uint32_t BYTES_PER_TEXEL = 8;
  static constexpr D3D12_RESOURCE_STATES READABLE = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

  // Takes a shader resource view and an unordered access view in _shaderHeap for every level there can be.
  explicit BloomChain(DescriptorHeap& _shaderHeap);

  // Creates the levels for a view of this size, or anew for a new size, and writes their descriptors.
  void Resize(const GraphicsDevice& _device, std::uint32_t _widthPixels, std::uint32_t _heightPixels);

  [[nodiscard]] std::uint32_t LevelCount() const noexcept
  {
    return static_cast<std::uint32_t>(m_levels.size());
  }

  [[nodiscard]] ID3D12Resource* Texture(std::uint32_t _level) const noexcept;
  [[nodiscard]] std::uint32_t WidthPixels(std::uint32_t _level) const noexcept;
  [[nodiscard]] std::uint32_t HeightPixels(std::uint32_t _level) const noexcept;

  // Descriptor tables of one view each: a level as Texture2D<float4>, and as RWTexture2D<float4>.
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE LevelTable(std::uint32_t _level) const noexcept;
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE LevelWriteTable(std::uint32_t _level) const noexcept;

private:
  struct Level
  {
    winrt::com_ptr<ID3D12Resource> texture;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
  };

  DescriptorHeap& m_shaderHeap;
  std::array<std::uint32_t, NeuronCore::BLOOM_MAX_LEVELS> m_srvs{};
  std::array<std::uint32_t, NeuronCore::BLOOM_MAX_LEVELS> m_uavs{};
  std::vector<Level> m_levels;
};

} // namespace NeuronClient
