#include "pch.h"

#include "ViewTargets.h"

#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"

#include "PerspectiveView.h"
#include "TraceHit.h"

#include <array>

namespace NeuronClient
{

ViewTargets::ViewTargets(DescriptorHeap& _rtvHeap, DescriptorHeap& _dsvHeap, DescriptorHeap& _shaderHeap, DescriptorHeap& _cpuHeap)
  : m_rtvHeap(_rtvHeap),
    m_dsvHeap(_dsvHeap),
    m_shaderHeap(_shaderHeap),
    m_cpuHeap(_cpuHeap),
    m_visibilityRtv(_rtvHeap.Allocate()),
    m_depthDsv(_dsvHeap.Allocate()),
    m_visibilitySrv(_shaderHeap.Allocate()),
    m_visibilityUav(_shaderHeap.Allocate()),
    m_visibilityCpuUav(_cpuHeap.Allocate())
{
}

void ViewTargets::Resize(const GraphicsDevice& _device, std::uint32_t _widthPixels, std::uint32_t _heightPixels)
{
  m_widthPixels = _widthPixels;
  m_heightPixels = _heightPixels;

  // The visibility buffer is cleared as a UAV: a render-target clear takes floats, and no float converts exactly to the
  // NO_VOXEL bit pattern.
  m_visibility = CreateTexture2D(_device, VISIBILITY_FORMAT, _widthPixels, _heightPixels,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, L"Visibility");
  D3D12_CLEAR_VALUE depthClear{};
  depthClear.Format = DEPTH_FORMAT;
  depthClear.DepthStencil = {NeuronCore::PERSPECTIVE_FAR_DEPTH, 0};
  m_depth = CreateTexture2D(_device, DXGI_FORMAT_R32_TYPELESS, _widthPixels, _heightPixels, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                            D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, L"View depth");

  ID3D12Device* device = _device.Device();
  D3D12_RENDER_TARGET_VIEW_DESC rtv{};
  rtv.Format = VISIBILITY_FORMAT;
  rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  device->CreateRenderTargetView(m_visibility.get(), &rtv, m_rtvHeap.Cpu(m_visibilityRtv));

  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = VISIBILITY_FORMAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(m_visibility.get(), &srv, m_shaderHeap.Cpu(m_visibilitySrv));

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = VISIBILITY_FORMAT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device->CreateUnorderedAccessView(m_visibility.get(), nullptr, &uav, m_shaderHeap.Cpu(m_visibilityUav));
  device->CreateUnorderedAccessView(m_visibility.get(), nullptr, &uav, m_cpuHeap.Cpu(m_visibilityCpuUav));

  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
  dsv.Format = DEPTH_FORMAT;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  device->CreateDepthStencilView(m_depth.get(), &dsv, m_dsvHeap.Cpu(m_depthDsv));
}

void ViewTargets::BeginSplat(ID3D12GraphicsCommandList* _list) const
{
  const D3D12_RESOURCE_BARRIER toClear =
    Transition(m_visibility.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  _list->ResourceBarrier(1, &toClear);
  const std::array<UINT, 4> noVoxel{NeuronCore::NO_VOXEL, 0u, 0u, 0u};
  _list->ClearUnorderedAccessViewUint(m_shaderHeap.Gpu(m_visibilityUav), m_cpuHeap.Cpu(m_visibilityCpuUav), m_visibility.get(),
                                      noVoxel.data(), 0, nullptr);
  const D3D12_RESOURCE_BARRIER toDraw =
    Transition(m_visibility.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
  _list->ResourceBarrier(1, &toDraw);

  const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap.Cpu(m_visibilityRtv);
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap.Cpu(m_depthDsv);
  _list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, NeuronCore::PERSPECTIVE_FAR_DEPTH, 0, 0, nullptr);
  _list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
  const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(m_widthPixels), static_cast<float>(m_heightPixels), 0.0f, 1.0f};
  const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_widthPixels), static_cast<LONG>(m_heightPixels)};
  _list->RSSetViewports(1, &viewport);
  _list->RSSetScissorRects(1, &scissor);
}

void ViewTargets::EndSplat(ID3D12GraphicsCommandList* _list) const
{
  const D3D12_RESOURCE_BARRIER toRead =
    Transition(m_visibility.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  _list->ResourceBarrier(1, &toRead);
}

D3D12_GPU_DESCRIPTOR_HANDLE ViewTargets::VisibilityTable() const noexcept
{
  return m_shaderHeap.Gpu(m_visibilitySrv);
}

} // namespace NeuronClient
