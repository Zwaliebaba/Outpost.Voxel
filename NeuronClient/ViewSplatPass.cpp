#include "pch.h"

#include "ViewSplatPass.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

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

// Reversed-Z: the pass clears to the far plane and keeps the nearer depth, which is the greater one (§7.5).
static_assert(NeuronCore::IsNearerPerspectiveDepth(1.0f, 0.5f), "the view splat keeps the greater depth");
constexpr D3D12_COMPARISON_FUNC NEARER = D3D12_COMPARISON_FUNC_GREATER;

// Rectangle r is vertices 4r to 4r + 3, corners (min, min), (max, min), (min, max) and (max, max), as two triangles.
[[nodiscard]] std::array<std::uint16_t, ViewSplatPass::RECTANGLE_INDEX_COUNT> RectangleIndices() noexcept
{
  std::array<std::uint16_t, ViewSplatPass::RECTANGLE_INDEX_COUNT> indices{};
  for (std::uint32_t rectangle = 0; rectangle < ViewSplatPass::RECTANGLES_PER_INSTANCE; ++rectangle)
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

ViewSplatPass::ViewSplatPass(GraphicsDevice& _device)
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
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"View splat root signature");

  // No input layout: the vertex shader pulls everything by index. Culling is off, since a rectangle's winding means
  // nothing, and depth clipping is on (§9.1).
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = DefaultGraphicsPipeline();
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = ViewSplatAlignedVertexShader();
  pipeline.PS = ViewSplatAlignedPixelShader();
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.DepthStencilState.DepthFunc = NEARER;
  pipeline.NumRenderTargets = 1;
  pipeline.RTVFormats[0] = ViewTargets::VISIBILITY_FORMAT;
  pipeline.DSVFormat = ViewTargets::DEPTH_FORMAT;
  winrt::check_hresult(_device.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));
  m_pipeline->SetName(L"View splat, aligned");

  const std::array<std::uint16_t, RECTANGLE_INDEX_COUNT> indices = RectangleIndices();
  m_rectangleIndices = CreateStaticBuffer(_device, std::as_bytes(std::span(indices)), L"Splat rectangle indices");
  m_indexView = {m_rectangleIndices->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(indices)), DXGI_FORMAT_R16_UINT};
}

void ViewSplatPass::Record(ID3D12GraphicsCommandList* _list, const VoxelScene& _scene, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants) const
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
