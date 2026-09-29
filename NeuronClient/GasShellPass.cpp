#include "pch.h"

#include "GasShellPass.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"
#include "ViewTargets.h"

#include <array>
#include <cstdint>

namespace NeuronClient
{
namespace
{

enum RootParameter : std::uint8_t
{
  ViewConstantsParameter,
  ShellsParameter,
  DepthParameter,
  ColorParameter,
  RootParameterCount
};

} // namespace

GasShellPass::GasShellPass(GraphicsDevice& _device)
{
  const D3D12_DESCRIPTOR_RANGE depthRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  const D3D12_DESCRIPTOR_RANGE colorRange{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
  std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
  parameters[ViewConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[ViewConstantsParameter].Descriptor = {0, 0};
  parameters[ShellsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[ShellsParameter].Descriptor = {1, 0};
  parameters[DepthParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[DepthParameter].DescriptorTable = {1, &depthRange};
  parameters[ColorParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[ColorParameter].DescriptorTable = {1, &colorRange};
  for (D3D12_ROOT_PARAMETER& parameter : parameters)
  {
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  const D3D12_ROOT_SIGNATURE_DESC rootSignature{static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
                                                D3D12_ROOT_SIGNATURE_FLAG_NONE};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Gas shell root signature");

  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.CS = GasShellComputeShader();
  winrt::check_hresult(_device.Device()->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));
  m_pipeline->SetName(L"Gas shells");
}

void GasShellPass::Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants,
                          D3D12_GPU_VIRTUAL_ADDRESS _shells) const
{
  _list->SetComputeRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->SetComputeRootConstantBufferView(ViewConstantsParameter, _viewConstants);
  _list->SetComputeRootConstantBufferView(ShellsParameter, _shells);
  _list->SetComputeRootDescriptorTable(DepthParameter, _targets.DepthTable());
  _list->SetComputeRootDescriptorTable(ColorParameter, _targets.HdrColorWriteTable());
  _list->Dispatch((_targets.WidthPixels() + GROUP_PIXELS - 1) / GROUP_PIXELS, (_targets.HeightPixels() + GROUP_PIXELS - 1) / GROUP_PIXELS,
                  1);
}

} // namespace NeuronClient
