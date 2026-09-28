#include "pch.h"

#include "CoveragePass.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"
#include "ViewTargets.h"

namespace NeuronClient
{

CoveragePass::CoveragePass(const GraphicsDevice& _device)
{
  const D3D12_ROOT_SIGNATURE_DESC rootSignature{
    0, nullptr, 0, nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Coverage root signature");

  // The triangle lies at depth 0, the far plane in reversed Z, where the view's depth is cleared; LESS passes wherever a
  // voxel wrote a depth above it (§7.5).
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = DefaultGraphicsPipeline();
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = CoverageVertexShader();
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.DepthStencilState.DepthEnable = TRUE;
  pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  pipeline.NumRenderTargets = 0;
  pipeline.DSVFormat = ViewTargets::DEPTH_FORMAT;
  winrt::check_hresult(_device.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));
  m_pipeline->SetName(L"Coverage");
}

void CoveragePass::Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets) const
{
  const D3D12_CPU_DESCRIPTOR_HANDLE depth = _targets.DepthView();
  _list->OMSetRenderTargets(0, nullptr, FALSE, &depth);
  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->DrawInstanced(3, 1, 0, 0);
}

} // namespace NeuronClient
