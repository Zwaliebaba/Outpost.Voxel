#include "pch.h"

#include "BloomPass.h"

#include "BloomChain.h"
#include "BloomConstants.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"
#include "ViewTargets.h"

#include "Bloom.h"

#include <array>
#include <cstdint>

namespace NeuronClient
{
namespace
{

enum RootParameter : std::uint8_t
{
  ConstantsParameter,
  SourceParameter,
  LevelParameter,
  RootParameterCount
};

// Writes level _level of _chain as a UAV, from _source, and leaves it readable.
void DispatchLevel(ID3D12GraphicsCommandList* _list, const BloomChain& _chain, std::uint32_t _level, const BloomConstants& _constants,
                   D3D12_GPU_DESCRIPTOR_HANDLE _source)
{
  const D3D12_RESOURCE_BARRIER toWrite = Transition(_chain.Texture(_level), BloomChain::READABLE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  _list->ResourceBarrier(1, &toWrite);
  _list->SetComputeRoot32BitConstants(ConstantsParameter, BLOOM_CONSTANT_WORDS, &_constants, 0);
  _list->SetComputeRootDescriptorTable(SourceParameter, _source);
  _list->SetComputeRootDescriptorTable(LevelParameter, _chain.LevelWriteTable(_level));
  _list->Dispatch((_constants.widthPixels + BloomPass::GROUP_PIXELS - 1) / BloomPass::GROUP_PIXELS,
                  (_constants.heightPixels + BloomPass::GROUP_PIXELS - 1) / BloomPass::GROUP_PIXELS, 1);
  const D3D12_RESOURCE_BARRIER toRead = Transition(_chain.Texture(_level), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, BloomChain::READABLE);
  _list->ResourceBarrier(1, &toRead);
}

} // namespace

BloomPass::BloomPass(GraphicsDevice& _device)
{
  const D3D12_DESCRIPTOR_RANGE sourceRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  const D3D12_DESCRIPTOR_RANGE levelRange{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
  std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
  parameters[ConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[ConstantsParameter].Constants = {0, 0, BLOOM_CONSTANT_WORDS};
  parameters[SourceParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[SourceParameter].DescriptorTable = {1, &sourceRange};
  parameters[LevelParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[LevelParameter].DescriptorTable = {1, &levelRange};
  for (D3D12_ROOT_PARAMETER& parameter : parameters)
  {
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  const D3D12_ROOT_SIGNATURE_DESC rootSignature{static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
                                                D3D12_ROOT_SIGNATURE_FLAG_NONE};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Bloom root signature");

  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.CS = BloomDownComputeShader();
  winrt::check_hresult(_device.Device()->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(m_down.put())));
  m_down->SetName(L"Bloom down");
  pipeline.CS = BloomUpComputeShader();
  winrt::check_hresult(_device.Device()->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(m_up.put())));
  m_up->SetName(L"Bloom up");
}

void BloomPass::Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, const BloomChain& _chain) const
{
  RecordDown(_list, _targets, _chain);
  RecordUp(_list, _chain);
}

void BloomPass::RecordDown(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, const BloomChain& _chain) const
{
  _list->SetComputeRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_down.get());
  for (std::uint32_t level = 0; level < _chain.LevelCount(); ++level)
  {
    const BloomConstants constants{_chain.WidthPixels(level), _chain.HeightPixels(level), level == 0 ? 1u : 0u, 0.0f};
    DispatchLevel(_list, _chain, level, constants, level == 0 ? _targets.HdrColorTable() : _chain.LevelTable(level - 1));
  }
}

void BloomPass::RecordUp(ID3D12GraphicsCommandList* _list, const BloomChain& _chain) const
{
  _list->SetComputeRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_up.get());
  const std::uint32_t count = _chain.LevelCount();
  for (std::uint32_t below = count; below-- > 1;)
  {
    const std::uint32_t level = below - 1;
    const BloomConstants constants{_chain.WidthPixels(level), _chain.HeightPixels(level), 0u, NeuronCore::BloomBelowShare(count - below)};
    DispatchLevel(_list, _chain, level, constants, _chain.LevelTable(below));
  }
}

} // namespace NeuronClient
