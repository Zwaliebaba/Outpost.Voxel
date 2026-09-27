#include "pch.h"

#include "SplatPass.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"
#include "ShadowMap.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "OrthographicView.h"
#include "PerspectiveView.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace NeuronClient
{
namespace
{

enum RootParameter : std::uint8_t
{
  ViewConstantsParameter,
  InstanceConstantsParameter,
  RecordsParameter,
  RootParameterCount
};

// The view is reversed-Z: it clears to the far plane and keeps the nearer depth, which is the greater one. The shadow
// map is standard Z, where the nearer depth is the smaller one (§7.5, Design/ADR/ADR-006).
static_assert(NeuronCore::IsNearerPerspectiveDepth(1.0f, 0.5f), "the view splat keeps the greater depth");
constexpr D3D12_COMPARISON_FUNC VIEW_NEARER = D3D12_COMPARISON_FUNC_GREATER;
static_assert(NeuronCore::IsNearerOrthographicDepth(0.5f, 1.0f), "the shadow splat keeps the smaller depth");
constexpr D3D12_COMPARISON_FUNC SHADOW_NEARER = D3D12_COMPARISON_FUNC_LESS;

// Rectangle r is vertices 4r to 4r + 3, corners (min, min), (max, min), (min, max) and (max, max), as two triangles.
[[nodiscard]] std::array<std::uint16_t, SplatPass::RECTANGLE_INDEX_COUNT> RectangleIndices() noexcept
{
  std::array<std::uint16_t, SplatPass::RECTANGLE_INDEX_COUNT> indices{};
  for (std::uint32_t rectangle = 0; rectangle < SplatPass::RECTANGLES_PER_INSTANCE; ++rectangle)
  {
    const auto first = static_cast<std::uint16_t>(rectangle * 4u);
    const std::size_t at = static_cast<std::size_t>(rectangle) * 6u;
    indices[at + 0] = first;
    indices[at + 1] = static_cast<std::uint16_t>(first + 1u);
    indices[at + 2] = static_cast<std::uint16_t>(first + 2u);
    indices[at + 3] = static_cast<std::uint16_t>(first + 2u);
    indices[at + 4] = static_cast<std::uint16_t>(first + 1u);
    indices[at + 5] = static_cast<std::uint16_t>(first + 3u);
  }
  return indices;
}

} // namespace

SplatPass::SplatPass(GraphicsDevice& _device, Kind _kind)
{
  std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
  parameters[ViewConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[ViewConstantsParameter].Descriptor = {0, 0};
  parameters[ViewConstantsParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[InstanceConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[InstanceConstantsParameter].Descriptor = {1, 0};
  parameters[InstanceConstantsParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[RecordsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[RecordsParameter].Descriptor = {0, 0};
  parameters[RecordsParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  const D3D12_ROOT_SIGNATURE_DESC rootSignature{static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
                                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                  D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                  D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Splat root signature");

  // No input layout: the vertex shader pulls everything by index. Culling is off, since a rectangle's winding means
  // nothing, and depth clipping is on (§9.1).
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = DefaultGraphicsPipeline();
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  if (_kind == Kind::View)
  {
    pipeline.VS = ViewSplatAlignedVertexShader();
    pipeline.PS = ViewSplatAlignedPixelShader();
    pipeline.DepthStencilState.DepthFunc = VIEW_NEARER;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = ViewTargets::VISIBILITY_FORMAT;
    pipeline.DSVFormat = ViewTargets::DEPTH_FORMAT;
  }
  else
  {
    // Depth alone, into the map (§10).
    pipeline.VS = ShadowSplatAlignedVertexShader();
    pipeline.PS = ShadowSplatAlignedPixelShader();
    pipeline.DepthStencilState.DepthFunc = SHADOW_NEARER;
    pipeline.NumRenderTargets = 0;
    pipeline.DSVFormat = ShadowMap::DEPTH_FORMAT;
  }
  winrt::check_hresult(_device.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));
  m_pipeline->SetName(_kind == Kind::View ? L"View splat, aligned" : L"Shadow splat, aligned");

  const std::array<std::uint16_t, RECTANGLE_INDEX_COUNT> indices = RectangleIndices();
  m_rectangleIndices = CreateStaticBuffer(_device, std::as_bytes(std::span(indices)), L"Splat rectangle indices");
  m_indexView = {m_rectangleIndices->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(indices)), DXGI_FORMAT_R16_UINT};
}

void SplatPass::Record(ID3D12GraphicsCommandList* _list, const VoxelScene& _scene, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants) const
{
  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->SetPipelineState(m_pipeline.get());
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->IASetIndexBuffer(&m_indexView);
  _list->SetGraphicsRootConstantBufferView(ViewConstantsParameter, _viewConstants);
  _list->SetGraphicsRootShaderResourceView(RecordsParameter, _scene.Records());
  for (const SceneInstance& instance : _scene.Instances())
  {
    if (instance.recordCount == 0)
    {
      continue;
    }
    _list->SetGraphicsRootConstantBufferView(InstanceConstantsParameter, instance.constants);
    const UINT instances = (instance.recordCount + RECTANGLES_PER_INSTANCE - 1) / RECTANGLES_PER_INSTANCE;
    _list->DrawIndexedInstanced(RECTANGLE_INDEX_COUNT, instances, 0, 0, 0);
  }
}

} // namespace NeuronClient
