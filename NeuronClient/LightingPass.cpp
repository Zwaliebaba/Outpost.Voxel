#include "pch.h"

#include "LightingPass.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"
#include "ShadowMap.h"
#include "UploadRing.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "Blast.h"

#include <array>
#include <cstdint>
#include <vector>

namespace NeuronClient
{
namespace
{

enum RootParameter : std::uint8_t
{
  ViewConstantsParameter,
  ShadowViewConstantsParameter,
  LightingConstantsParameter,
  RecordsParameter,
  DepthParameter,
  VisibilityParameter,
  ShadowMapParameter,
  PlacementsParameter,
  PalettesParameter,
  BlastLightingParameter,
  PlacementHeatParameter,
  FragmentOfParameter,
  FragmentsParameter,
  ColorParameter,
  RootParameterCount
};

// Every piece an upload ring hands out starts on this boundary.
constexpr std::uint64_t RING_ALIGNMENT_BYTES = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

} // namespace

D3D12_GPU_VIRTUAL_ADDRESS PushPlacementHeat(UploadRing& _ring, std::span<const NeuronCore::Placement> _placements)
{
  std::vector<NeuronCore::PlacementHeat> heat;
  heat.reserve(_placements.size());
  for (const NeuronCore::Placement& placement : _placements)
  {
    heat.push_back(NeuronCore::MakePlacementHeat(placement));
  }
  return _ring.PushBytes(std::as_bytes(std::span(heat)));
}

std::uint64_t PlacementHeatBytes(std::size_t _placementCount) noexcept
{
  const std::uint64_t bytes = _placementCount * sizeof(NeuronCore::PlacementHeat);
  return (bytes + RING_ALIGNMENT_BYTES - 1) / RING_ALIGNMENT_BYTES * RING_ALIGNMENT_BYTES;
}

LightingPass::LightingPass(GraphicsDevice& _device)
{
  const D3D12_DESCRIPTOR_RANGE depthRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0};
  const D3D12_DESCRIPTOR_RANGE visibilityRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 0};
  const D3D12_DESCRIPTOR_RANGE shadowMapRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, 0};
  const D3D12_DESCRIPTOR_RANGE colorRange{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
  std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
  parameters[ViewConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[ViewConstantsParameter].Descriptor = {0, 0};
  parameters[ShadowViewConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[ShadowViewConstantsParameter].Descriptor = {1, 0};
  parameters[LightingConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[LightingConstantsParameter].Descriptor = {2, 0};
  parameters[RecordsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[RecordsParameter].Descriptor = {0, 0};
  parameters[DepthParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[DepthParameter].DescriptorTable = {1, &depthRange};
  parameters[VisibilityParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[VisibilityParameter].DescriptorTable = {1, &visibilityRange};
  parameters[ShadowMapParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[ShadowMapParameter].DescriptorTable = {1, &shadowMapRange};
  parameters[PlacementsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[PlacementsParameter].Descriptor = {4, 0};
  parameters[PalettesParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[PalettesParameter].Descriptor = {5, 0};
  parameters[BlastLightingParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[BlastLightingParameter].Descriptor = {3, 0};
  parameters[PlacementHeatParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[PlacementHeatParameter].Descriptor = {6, 0};
  parameters[FragmentOfParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[FragmentOfParameter].Descriptor = {7, 0};
  parameters[FragmentsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[FragmentsParameter].Descriptor = {8, 0};
  parameters[ColorParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[ColorParameter].DescriptorTable = {1, &colorRange};
  for (D3D12_ROOT_PARAMETER& parameter : parameters)
  {
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  // The shadow map's sampler (§10): each tap compares the point's depth with the four texels around it, LESS_EQUAL,
  // and blends the results bilinearly; beyond the map lies an opaque white border, the far plane, which lights.
  // Every member is named: two of the enumerations have no zero value, so a value-initialized description would
  // hold invalid ones until each was assigned. The map has one mip level, so both LOD bounds are 0.
  const D3D12_STATIC_SAMPLER_DESC shadowSampler{.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
                                                .AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                                .AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                                .AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                                .MipLODBias = 0.0f,
                                                .MaxAnisotropy = 1,
                                                .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                                                .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
                                                .MinLOD = 0.0f,
                                                .MaxLOD = 0.0f,
                                                .ShaderRegister = 0,
                                                .RegisterSpace = 0,
                                                .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL};

  const D3D12_ROOT_SIGNATURE_DESC rootSignature{static_cast<UINT>(parameters.size()), parameters.data(), 1, &shadowSampler,
                                                D3D12_ROOT_SIGNATURE_FLAG_NONE};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Lighting root signature");

  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.CS = LightingComputeShader();
  winrt::check_hresult(_device.Device()->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));
  m_pipeline->SetName(L"Lighting");
}

void LightingPass::Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, const ShadowMap& _shadowMap,
                          const VoxelScene& _scene, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants,
                          D3D12_GPU_VIRTUAL_ADDRESS _shadowViewConstants, D3D12_GPU_VIRTUAL_ADDRESS _lightingConstants,
                          D3D12_GPU_VIRTUAL_ADDRESS _placements, D3D12_GPU_VIRTUAL_ADDRESS _blastLighting,
                          D3D12_GPU_VIRTUAL_ADDRESS _placementHeat) const
{
  _list->SetComputeRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->SetComputeRootConstantBufferView(ViewConstantsParameter, _viewConstants);
  _list->SetComputeRootConstantBufferView(ShadowViewConstantsParameter, _shadowViewConstants);
  _list->SetComputeRootConstantBufferView(LightingConstantsParameter, _lightingConstants);
  _list->SetComputeRootShaderResourceView(RecordsParameter, _scene.Records());
  _list->SetComputeRootDescriptorTable(DepthParameter, _targets.DepthTable());
  _list->SetComputeRootDescriptorTable(VisibilityParameter, _targets.VisibilityTable());
  _list->SetComputeRootDescriptorTable(ShadowMapParameter, _shadowMap.Table());
  _list->SetComputeRootShaderResourceView(PlacementsParameter, _placements);
  _list->SetComputeRootShaderResourceView(PalettesParameter, _scene.Palettes());
  _list->SetComputeRootConstantBufferView(BlastLightingParameter, _blastLighting);
  _list->SetComputeRootShaderResourceView(PlacementHeatParameter, _placementHeat);
  _list->SetComputeRootShaderResourceView(FragmentOfParameter, _scene.FragmentOf());
  _list->SetComputeRootShaderResourceView(FragmentsParameter, _scene.Fragments());
  _list->SetComputeRootDescriptorTable(ColorParameter, _targets.HdrColorWriteTable());
  _list->Dispatch((_targets.WidthPixels() + GROUP_PIXELS - 1) / GROUP_PIXELS, (_targets.HeightPixels() + GROUP_PIXELS - 1) / GROUP_PIXELS,
                  1);
}

} // namespace NeuronClient
