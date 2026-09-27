#include "pch.h"

#include "ShadowMap.h"

#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"

#include "OrthographicView.h"

namespace NeuronClient
{

ShadowMap::ShadowMap(const GraphicsDevice& _device, DescriptorHeap& _dsvHeap, DescriptorHeap& _shaderHeap, std::uint32_t _sizePixels)
  : m_dsvHeap(_dsvHeap),
    m_shaderHeap(_shaderHeap),
    m_dsv(_dsvHeap.Allocate()),
    m_srv(_shaderHeap.Allocate()),
    m_sizePixels(_sizePixels)
{
  D3D12_CLEAR_VALUE clear{};
  clear.Format = DEPTH_FORMAT;
  clear.DepthStencil = {NeuronCore::ORTHOGRAPHIC_FAR_DEPTH, 0};
  m_depth = CreateTexture2D(_device, DXGI_FORMAT_R32_TYPELESS, _sizePixels, _sizePixels, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, READABLE,
                            &clear, L"Shadow map");

  ID3D12Device* device = _device.Device();
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
  dsv.Format = DEPTH_FORMAT;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  device->CreateDepthStencilView(m_depth.get(), &dsv, m_dsvHeap.Cpu(m_dsv));

  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = READ_FORMAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(m_depth.get(), &srv, m_shaderHeap.Cpu(m_srv));
}

void ShadowMap::BeginSplat(ID3D12GraphicsCommandList* _list) const
{
  const D3D12_RESOURCE_BARRIER toWrite = Transition(m_depth.get(), READABLE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  _list->ResourceBarrier(1, &toWrite);
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap.Cpu(m_dsv);
  _list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, NeuronCore::ORTHOGRAPHIC_FAR_DEPTH, 0, 0, nullptr);
  _list->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
  const auto size = static_cast<float>(m_sizePixels);
  const D3D12_VIEWPORT viewport{0.0f, 0.0f, size, size, 0.0f, 1.0f};
  const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_sizePixels), static_cast<LONG>(m_sizePixels)};
  _list->RSSetViewports(1, &viewport);
  _list->RSSetScissorRects(1, &scissor);
}

void ShadowMap::EndSplat(ID3D12GraphicsCommandList* _list) const
{
  const D3D12_RESOURCE_BARRIER toRead = Transition(m_depth.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, READABLE);
  _list->ResourceBarrier(1, &toRead);
}

D3D12_GPU_DESCRIPTOR_HANDLE ShadowMap::Table() const noexcept
{
  return m_shaderHeap.Gpu(m_srv);
}

} // namespace NeuronClient
