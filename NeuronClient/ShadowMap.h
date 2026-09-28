#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstdint>

namespace NeuronClient
{

class DescriptorHeap;
class GraphicsDevice;

// The sun's depth (Design/Archive/SampleRenderer.md §8, §10): an R32_TYPELESS texture, written through a D32_FLOAT view by the
// shadow splat and read through an R32_FLOAT view by the lighting and the shadow-map view. Standard Z, cleared to the far
// plane (Design/ADR/ADR-006). Between frames it rests readable by every shader stage.
class ShadowMap
{
public:
  static constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
  static constexpr DXGI_FORMAT READ_FORMAT = DXGI_FORMAT_R32_FLOAT;
  static constexpr std::uint32_t BYTES_PER_TEXEL = 4;
  static constexpr D3D12_RESOURCE_STATES READABLE = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

  // A square map _sizePixels across. _shaderHeap is shader visible and takes the read view.
  ShadowMap(const GraphicsDevice& _device, DescriptorHeap& _dsvHeap, DescriptorHeap& _shaderHeap, std::uint32_t _sizePixels);

  // Clears the map to the far plane and binds it as the only output of the shadow splat, with a viewport over all of it.
  void BeginSplat(ID3D12GraphicsCommandList* _list) const;

  // Leaves the map readable by every shader stage.
  void EndSplat(ID3D12GraphicsCommandList* _list) const;

  [[nodiscard]] ID3D12Resource* Depth() const noexcept
  {
    return m_depth.get();
  }

  // A descriptor table of one SRV: the map as Texture2D<float>.
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE Table() const noexcept;

  [[nodiscard]] std::uint32_t SizePixels() const noexcept
  {
    return m_sizePixels;
  }

private:
  DescriptorHeap& m_dsvHeap;
  DescriptorHeap& m_shaderHeap;
  std::uint32_t m_dsv;
  std::uint32_t m_srv;
  std::uint32_t m_sizePixels;
  winrt::com_ptr<ID3D12Resource> m_depth;
};

} // namespace NeuronClient
