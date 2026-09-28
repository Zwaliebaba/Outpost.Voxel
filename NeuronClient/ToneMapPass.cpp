#include "pch.h"

#include "ToneMapPass.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"
#include "ViewTargets.h"

#include <array>
#include <bit>
#include <cstdint>

namespace NeuronClient
{
namespace
{

enum RootParameter : std::uint8_t
{
  ExposureParameter,
  HdrColorParameter,
  RootParameterCount
};

} // namespace

ToneMapPass::ToneMapPass(GraphicsDevice& _device, DXGI_FORMAT _targetFormat)
{
  const D3D12_DESCRIPTOR_RANGE hdrColorRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
  parameters[ExposureParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[ExposureParameter].Constants = {0, 0, 1};
  parameters[HdrColorParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[HdrColorParameter].DescriptorTable = {1, &hdrColorRange};
  for (D3D12_ROOT_PARAMETER& parameter : parameters)
  {
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  const D3D12_ROOT_SIGNATURE_DESC rootSignature{
    static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Tone map root signature");

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = DefaultGraphicsPipeline();
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = ToneMapVertexShader();
  pipeline.PS = ToneMapPixelShader();
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.DepthStencilState.DepthEnable = FALSE;
  pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  pipeline.NumRenderTargets = 1;
  pipeline.RTVFormats[0] = _targetFormat;
  winrt::check_hresult(_device.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));
  m_pipeline->SetName(L"Tone map");
}

void ToneMapPass::Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, float _exposure) const
{
  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->SetGraphicsRoot32BitConstant(ExposureParameter, std::bit_cast<UINT>(_exposure), 0);
  _list->SetGraphicsRootDescriptorTable(HdrColorParameter, _targets.HdrColorTable());
  _list->DrawInstanced(3, 1, 0, 0);
}

} // namespace NeuronClient
