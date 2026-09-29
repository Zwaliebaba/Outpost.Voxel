#include "pch.h"

#include "SkyPass.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"
#include "ViewTargets.h"

#include "PerspectiveView.h"

#include <array>
#include <cstdint>

namespace NeuronClient
{
namespace
{

enum RootParameter : std::uint8_t
{
  ViewConstantsParameter,
  SkyConstantsParameter,
  StarsParameter,
  RootParameterCount
};

// The triangle and the quads lie at the far plane, z = 0 (FullScreen.hlsli, SkyPass.hlsli), which is exactly the depth of
// every pixel no voxel covers, so the sky passes where the depth equals it (§9, Design/ADR/ADR-006).
static_assert(NeuronCore::IsFarPerspectiveDepth(0.0f), "the sky lies at the far plane, z = 0");
constexpr D3D12_COMPARISON_FUNC AT_THE_FAR_PLANE = D3D12_COMPARISON_FUNC_EQUAL;

// Four corners a star, as a strip of two triangles (SkyPass.hlsli).
constexpr UINT STAR_CORNERS = 4;

} // namespace

SkyPass::SkyPass(GraphicsDevice& _device, std::span<const NeuronCore::StarRecord> _stars)
  : m_starCount(static_cast<std::uint32_t>(_stars.size()))
{
  std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
  parameters[ViewConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[ViewConstantsParameter].Descriptor = {0, 0};
  parameters[SkyConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[SkyConstantsParameter].Descriptor = {1, 0};
  parameters[StarsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[StarsParameter].Descriptor = {0, 0};
  for (D3D12_ROOT_PARAMETER& parameter : parameters)
  {
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  const D3D12_ROOT_SIGNATURE_DESC rootSignature{static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
                                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                  D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                  D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Sky root signature");

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = DefaultGraphicsPipeline();
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = SkyVertexShader();
  pipeline.PS = SkyPixelShader();
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.DepthStencilState.DepthEnable = TRUE;
  pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  pipeline.DepthStencilState.DepthFunc = AT_THE_FAR_PLANE;
  pipeline.DSVFormat = ViewTargets::DEPTH_FORMAT;
  pipeline.NumRenderTargets = 1;
  pipeline.RTVFormats[0] = ViewTargets::HDR_FORMAT;
  winrt::check_hresult(_device.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_sky.put())));
  m_sky->SetName(L"Sky");

  // The stars add into the color the triangle wrote, and leave its alpha alone.
  pipeline.VS = StarVertexShader();
  pipeline.PS = StarPixelShader();
  D3D12_RENDER_TARGET_BLEND_DESC& blend = pipeline.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_ONE;
  blend.DestBlend = D3D12_BLEND_ONE;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.RenderTargetWriteMask =
    static_cast<UINT8>(D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE);
  winrt::check_hresult(_device.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_stars.put())));
  m_stars->SetName(L"Stars");

  if (!_stars.empty())
  {
    m_starBuffer = CreateStaticBuffer(_device, std::as_bytes(_stars), L"Stars");
  }
}

void SkyPass::Record(ID3D12GraphicsCommandList* _list, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants,
                     D3D12_GPU_VIRTUAL_ADDRESS _skyConstants) const
{
  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetGraphicsRootConstantBufferView(ViewConstantsParameter, _viewConstants);
  _list->SetGraphicsRootConstantBufferView(SkyConstantsParameter, _skyConstants);
  if (m_starCount > 0)
  {
    _list->SetGraphicsRootShaderResourceView(StarsParameter, m_starBuffer->GetGPUVirtualAddress());
  }
  _list->SetPipelineState(m_sky.get());
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->DrawInstanced(3, 1, 0, 0);
  if (m_starCount > 0)
  {
    _list->SetPipelineState(m_stars.get());
    _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    _list->DrawInstanced(STAR_CORNERS, m_starCount, 0, 0);
  }
}

} // namespace NeuronClient
