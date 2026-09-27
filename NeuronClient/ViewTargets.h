#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstdint>

namespace NeuronClient
{

class DescriptorHeap;
class GraphicsDevice;

// The targets at the size of the view (Design/SampleRenderer.md §7.3, §8): the view splat's reversed-Z depth and
// visibility buffer of voxel index and packed normal, and the lighting's HDR color. Between frames the depth and the
// visibility buffer rest readable by every shader stage, and the HDR color by pixel shaders.
class ViewTargets
{
public:
  static constexpr DXGI_FORMAT VISIBILITY_FORMAT = DXGI_FORMAT_R32G32_UINT;
  static constexpr std::uint32_t VISIBILITY_BYTES_PER_PIXEL = 8;
  static constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
  static constexpr DXGI_FORMAT DEPTH_READ_FORMAT = DXGI_FORMAT_R32_FLOAT;
  static constexpr DXGI_FORMAT HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
  static constexpr std::uint32_t HDR_BYTES_PER_PIXEL = 8;
  static constexpr D3D12_RESOURCE_STATES READABLE = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

  // _shaderHeap is shader visible; _cpuHeap is not, and holds the second descriptor a UAV clear needs.
  ViewTargets(DescriptorHeap& _rtvHeap, DescriptorHeap& _dsvHeap, DescriptorHeap& _shaderHeap, DescriptorHeap& _cpuHeap);

  // Creates the targets at a new size, or for the first time, and writes their descriptors.
  void Resize(const GraphicsDevice& _device, std::uint32_t _widthPixels, std::uint32_t _heightPixels);

  // Clears the visibility buffer to NO_VOXEL and depth to the far plane, and binds both as the splat's output with a
  // viewport over the whole view. The shader-visible heap must be set on _list.
  void BeginSplat(ID3D12GraphicsCommandList* _list) const;

  // Leaves the visibility buffer and the depth readable by every shader stage.
  void EndSplat(ID3D12GraphicsCommandList* _list) const;

  // Makes the HDR color writable by the lighting pass, and readable by pixel shaders again afterwards.
  void BeginLighting(ID3D12GraphicsCommandList* _list) const;
  void EndLighting(ID3D12GraphicsCommandList* _list) const;

  [[nodiscard]] ID3D12Resource* Visibility() const noexcept
  {
    return m_visibility.get();
  }

  [[nodiscard]] ID3D12Resource* Depth() const noexcept
  {
    return m_depth.get();
  }

  [[nodiscard]] ID3D12Resource* HdrColor() const noexcept
  {
    return m_hdrColor.get();
  }

  // Descriptor tables of one view each: the visibility buffer as Texture2D<uint2>, the depth as Texture2D<float>, the
  // HDR color as Texture2D<float4> and as RWTexture2D<float4>.
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE VisibilityTable() const noexcept;
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE DepthTable() const noexcept;
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE HdrColorTable() const noexcept;
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE HdrColorWriteTable() const noexcept;

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept
  {
    return m_widthPixels;
  }

  [[nodiscard]] std::uint32_t HeightPixels() const noexcept
  {
    return m_heightPixels;
  }

private:
  DescriptorHeap& m_rtvHeap;
  DescriptorHeap& m_dsvHeap;
  DescriptorHeap& m_shaderHeap;
  DescriptorHeap& m_cpuHeap;
  std::uint32_t m_visibilityRtv;
  std::uint32_t m_depthDsv;
  std::uint32_t m_visibilitySrv;
  std::uint32_t m_visibilityUav;
  std::uint32_t m_visibilityCpuUav;
  std::uint32_t m_depthSrv;
  std::uint32_t m_hdrColorSrv;
  std::uint32_t m_hdrColorUav;
  winrt::com_ptr<ID3D12Resource> m_visibility;
  winrt::com_ptr<ID3D12Resource> m_depth;
  winrt::com_ptr<ID3D12Resource> m_hdrColor;
  std::uint32_t m_widthPixels = 0;
  std::uint32_t m_heightPixels = 0;
};

} // namespace NeuronClient
