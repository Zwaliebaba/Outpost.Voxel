#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstdint>

namespace NeuronClient
{

class GraphicsDevice;
class VoxelScene;

// The splat (Design/Archive/SampleRenderer.md §9, §10): every voxel as a screen-space rectangle, intersected per pixel with its
// box. The view splat writes reversed-Z depth and the visibility buffer ViewTargets::BeginSplat has bound; the shadow
// splat writes standard-Z depth alone into the map ShadowMap::BeginSplat has bound. Each kind comes in the aligned
// permutation, for the intact model, and the oriented one, whose boxes the detonation poses (Design/SpaceScene.md §5.5).
class SplatPass
{
public:
  enum class Kind : std::uint8_t
  {
    View,  // perspective, ViewConstants
    Shadow // orthographic, ShadowViewConstants
  };

  enum class Permutation : std::uint8_t
  {
    Aligned, // the intact model: at time 0 every rotation is the identity
    Oriented // after the detonation, reading ExplosionConstants
  };

  // The view splat's measurement variants (§14). Both draw what the standard one draws.
  enum class Variant : std::uint8_t
  {
    Standard,
    PlainDepth, // writes SV_Depth rather than conservative depth, so that PSInvocations shows what the promise saves
    Overdraw    // counts every pixel shader invocation into the overdraw texture, for the overdraw view
  };

  // An instance of the draw covers this many voxels; Shader/Splat.hlsli relies on the same number (§9.1).
  static constexpr std::uint32_t RECTANGLES_PER_INSTANCE = 256;
  static constexpr std::uint32_t RECTANGLE_INDEX_COUNT = RECTANGLES_PER_INSTANCE * 6;

  // A variant other than Standard is the view kind's only.
  SplatPass(GraphicsDevice& _device, Kind _kind, Permutation _permutation = Permutation::Aligned, Variant _variant = Variant::Standard);

  // One DrawIndexedInstanced per placed model, drawn in order, so that a tie in depth goes to the lower record (§4.2).
  // _viewConstants are the ViewConstants or ShadowViewConstants the kind reads, and _explosionConstants the
  // ExplosionConstants the oriented permutation reads; the aligned one ignores them. The overdraw variant counts into the
  // RWTexture2D<uint> of _overdrawTable, which the others ignore.
  void Record(ID3D12GraphicsCommandList* _list, const VoxelScene& _scene, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants,
              D3D12_GPU_VIRTUAL_ADDRESS _explosionConstants = 0, D3D12_GPU_DESCRIPTOR_HANDLE _overdrawTable = {}) const;

  // The overdraw variant, whose Record needs the overdraw count cleared and writable (ViewTargets::BeginOverdraw).
  [[nodiscard]] bool CountsOverdraw() const noexcept
  {
    return m_variant == Variant::Overdraw;
  }

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  winrt::com_ptr<ID3D12Resource> m_rectangleIndices;
  D3D12_INDEX_BUFFER_VIEW m_indexView{};
  Permutation m_permutation;
  Variant m_variant;
};

} // namespace NeuronClient
