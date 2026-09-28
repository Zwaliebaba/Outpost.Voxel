#include "pch.h"

#include "SplatPass.h"

#include "ExplosionConstants.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "PlacementConstants.h"
#include "Shaders.h"
#include "ShadowMap.h"
#include "UploadRing.h"
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
  DrawParameter,
  ExplosionConstantsParameter,
  RecordsParameter,
  PlacementsParameter,
  OverdrawParameter,
  RootParameterCount
};

// Every piece an upload ring hands out starts on this boundary.
constexpr std::uint64_t RING_ALIGNMENT_BYTES = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

[[nodiscard]] constexpr std::uint64_t RingBytes(std::uint64_t _bytes) noexcept
{
  return (_bytes + RING_ALIGNMENT_BYTES - 1) / RING_ALIGNMENT_BYTES * RING_ALIGNMENT_BYTES;
}

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

// The view splat's pixel shader for a permutation and a variant.
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatPixelShader(bool _oriented, SplatPass::Variant _variant) noexcept
{
  switch (_variant)
  {
  case SplatPass::Variant::PlainDepth:
    return _oriented ? ViewSplatOrientedPlainDepthPixelShader() : ViewSplatAlignedPlainDepthPixelShader();
  case SplatPass::Variant::Overdraw:
    return _oriented ? ViewSplatOrientedOverdrawPixelShader() : ViewSplatAlignedOverdrawPixelShader();
  case SplatPass::Variant::Standard:
    break;
  }
  return _oriented ? ViewSplatOrientedPixelShader() : ViewSplatAlignedPixelShader();
}

} // namespace

SplatPlacements PushSplatPlacements(UploadRing& _ring, std::span<const NeuronCore::Placement> _placements)
{
  std::vector<PlacementConstants> constants;
  constants.reserve(_placements.size());
  for (const NeuronCore::Placement& placement : _placements)
  {
    constants.push_back(MakePlacementConstants(placement));
  }
  SplatPlacements pushed{_ring.PushBytes(std::as_bytes(std::span(constants))), {}};
  pushed.draws.reserve(_placements.size());
  D3D12_GPU_VIRTUAL_ADDRESS rest = 0;
  for (std::uint32_t i = 0; i < _placements.size(); ++i)
  {
    const NeuronCore::Placement& placement = _placements[i];
    const bool oriented = !NeuronCore::IsAlignedPlacement(placement);
    D3D12_GPU_VIRTUAL_ADDRESS explosion = 0;
    if (placement.detonation.has_value())
    {
      explosion = _ring.Push(MakeExplosionConstants(placement));
    }
    else if (oriented)
    {
      if (rest == 0)
      {
        rest = _ring.Push(MakeExplosionConstants(placement));
      }
      explosion = rest;
    }
    pushed.draws.push_back({i, placement.recordCount, oriented, explosion});
  }
  return pushed;
}

std::uint64_t SplatPlacementBytes(std::span<const NeuronCore::Placement> _placements) noexcept
{
  std::uint64_t detonated = 0;
  for (const NeuronCore::Placement& placement : _placements)
  {
    detonated += placement.detonation.has_value() ? 1u : 0u;
  }
  return RingBytes(_placements.size() * sizeof(PlacementConstants)) + (detonated + 1) * RingBytes(sizeof(ExplosionConstants));
}

SplatPass::SplatPass(GraphicsDevice& _device, Kind _kind, Variant _variant)
  : m_variant(_variant)
{
  std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
  parameters[ViewConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[ViewConstantsParameter].Descriptor = {0, 0};
  parameters[ViewConstantsParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[DrawParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[DrawParameter].Constants = {1, 0, 1};
  parameters[DrawParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[ExplosionConstantsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[ExplosionConstantsParameter].Descriptor = {2, 0};
  parameters[ExplosionConstantsParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[RecordsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[RecordsParameter].Descriptor = {0, 0};
  parameters[RecordsParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[PlacementsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[PlacementsParameter].Descriptor = {1, 0};
  parameters[PlacementsParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  const D3D12_DESCRIPTOR_RANGE overdrawRange{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
  parameters[OverdrawParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[OverdrawParameter].DescriptorTable = {1, &overdrawRange};
  parameters[OverdrawParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // Only the overdraw variant has the UAV table, the last parameter. Below resource binding tier 3 a UAV table in the
  // root signature must be set for every draw, even one whose shaders never read it (§5 keeps to tier 1).
  const UINT parameterCount = _variant == Variant::Overdraw ? RootParameterCount : OverdrawParameter;
  const D3D12_ROOT_SIGNATURE_DESC rootSignature{parameterCount, parameters.data(), 0, nullptr,
                                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                  D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                  D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Splat root signature");

  // No input layout: the vertex shader pulls everything by index. Culling is off, since a rectangle's winding means
  // nothing, and depth clipping is on (§9.1). The two permutations share the root signature, so that a draw list can
  // move between them with nothing rebound.
  for (const bool oriented : {false, true})
  {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = DefaultGraphicsPipeline();
    pipeline.pRootSignature = m_rootSignature.get();
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    if (_kind == Kind::View)
    {
      pipeline.VS = oriented ? ViewSplatOrientedVertexShader() : ViewSplatAlignedVertexShader();
      pipeline.PS = ViewSplatPixelShader(oriented, _variant);
      pipeline.DepthStencilState.DepthFunc = VIEW_NEARER;
      pipeline.NumRenderTargets = 1;
      pipeline.RTVFormats[0] = ViewTargets::VISIBILITY_FORMAT;
      pipeline.DSVFormat = ViewTargets::DEPTH_FORMAT;
    }
    else
    {
      // Depth alone, into the map (§10).
      pipeline.VS = oriented ? ShadowSplatOrientedVertexShader() : ShadowSplatAlignedVertexShader();
      pipeline.PS = oriented ? ShadowSplatOrientedPixelShader() : ShadowSplatAlignedPixelShader();
      pipeline.DepthStencilState.DepthFunc = SHADOW_NEARER;
      pipeline.NumRenderTargets = 0;
      pipeline.DSVFormat = ShadowMap::DEPTH_FORMAT;
    }
    winrt::com_ptr<ID3D12PipelineState>& made = oriented ? m_orientedPipeline : m_alignedPipeline;
    winrt::check_hresult(_device.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(made.put())));
    if (_kind == Kind::View)
    {
      constexpr std::array<const wchar_t*, 6> VIEW_NAMES{L"View splat, aligned",
                                                         L"View splat, oriented",
                                                         L"View splat, aligned, plain depth",
                                                         L"View splat, oriented, plain depth",
                                                         L"View splat, aligned, overdraw",
                                                         L"View splat, oriented, overdraw"};
      made->SetName(VIEW_NAMES[2u * static_cast<std::size_t>(_variant) + (oriented ? 1u : 0u)]);
    }
    else
    {
      made->SetName(oriented ? L"Shadow splat, oriented" : L"Shadow splat, aligned");
    }
  }

  const std::array<std::uint16_t, RECTANGLE_INDEX_COUNT> indices = RectangleIndices();
  m_rectangleIndices = CreateStaticBuffer(_device, std::as_bytes(std::span(indices)), L"Splat rectangle indices");
  m_indexView = {m_rectangleIndices->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(indices)), DXGI_FORMAT_R16_UINT};
}

void SplatPass::Record(ID3D12GraphicsCommandList* _list, const VoxelScene& _scene, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants,
                       D3D12_GPU_VIRTUAL_ADDRESS _placements, std::span<const SplatDraw> _draws,
                       D3D12_GPU_DESCRIPTOR_HANDLE _overdrawTable) const
{
  _list->SetGraphicsRootSignature(m_rootSignature.get());
  _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _list->IASetIndexBuffer(&m_indexView);
  _list->SetGraphicsRootConstantBufferView(ViewConstantsParameter, _viewConstants);
  _list->SetGraphicsRootShaderResourceView(RecordsParameter, _scene.Records());
  _list->SetGraphicsRootShaderResourceView(PlacementsParameter, _placements);
  if (m_variant == Variant::Overdraw)
  {
    _list->SetGraphicsRootDescriptorTable(OverdrawParameter, _overdrawTable);
  }
  const ID3D12PipelineState* bound = nullptr;
  for (const SplatDraw& draw : _draws)
  {
    if (draw.recordCount == 0)
    {
      continue;
    }
    ID3D12PipelineState* pipeline = draw.oriented ? m_orientedPipeline.get() : m_alignedPipeline.get();
    if (pipeline != bound)
    {
      _list->SetPipelineState(pipeline);
      bound = pipeline;
    }
    _list->SetGraphicsRoot32BitConstant(DrawParameter, draw.placement, 0);
    if (draw.oriented)
    {
      _list->SetGraphicsRootConstantBufferView(ExplosionConstantsParameter, draw.explosion);
    }
    const UINT instances = (draw.recordCount + RECTANGLES_PER_INSTANCE - 1) / RECTANGLES_PER_INSTANCE;
    _list->DrawIndexedInstanced(RECTANGLE_INDEX_COUNT, instances, 0, 0, 0);
  }
}

} // namespace NeuronClient
