#include "pch.h"

#include "DebugViewPass.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include <array>
#include <cstdint>

namespace NeuronClient
{
namespace
{

enum RootParameter : std::uint8_t
{
  ViewConstantsParameter,
  PaletteParameter,
  SelectionParameter,
  RecordsParameter,
  VisibilityParameter,
  RootParameterCount
};

} // namespace

DebugViewPass::DebugViewPass(GraphicsDevice& _device, DXGI_FORMAT _targetFormat)
{
  const D3D12_DESCRIPTOR_RANGE visibilityRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0};
  std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
  parameters[ViewConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[ViewConstantsParameter].Descriptor = {0, 0};
  parameters[PaletteParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[PaletteParameter].Descriptor = {1, 0};
  parameters[SelectionParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[SelectionParameter].Constants = {2, 0, 1};
  parameters[RecordsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[RecordsParameter].Descriptor = {0, 0};
  parameters[VisibilityParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[VisibilityParameter].DescriptorTable = {1, &visibilityRange};
  for (D3D12_ROOT_PARAMETER& parameter : parameters)
  {
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  const D3D12_ROOT_SIGNATURE_DESC rootSignature{
    static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Debug view root signature");

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = DefaultGraphicsPipeline();
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = DebugViewVertexShader();
  pipeline.PS = DebugViewPixelShader();
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.DepthStencilState.DepthEnable = FALSE;
  pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  pipeline.NumRenderTargets = 1;
  pipeline.RTVFormats[0] = _targetFormat;
  winrt::check_hresult(_device.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));
  m_pipeline->SetName(L"Debug view");
}

void DebugViewPass::Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, const VoxelScene& _scene,
                           D3D12_GPU_VIRTUAL_ADDRESS _viewConstants, NeuronCore::DebugView _view) const
{
  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->SetGraphicsRootConstantBufferView(ViewConstantsParameter, _viewConstants);
  _list->SetGraphicsRootConstantBufferView(PaletteParameter, _scene.Palette());
  _list->SetGraphicsRoot32BitConstant(SelectionParameter, static_cast<UINT>(_view), 0);
  _list->SetGraphicsRootShaderResourceView(RecordsParameter, _scene.Records());
  _list->SetGraphicsRootDescriptorTable(VisibilityParameter, _targets.VisibilityTable());
  _list->DrawInstanced(3, 1, 0, 0);
}

} // namespace NeuronClient
