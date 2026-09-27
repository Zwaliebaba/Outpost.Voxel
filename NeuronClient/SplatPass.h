#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstdint>

namespace NeuronClient
{

class GraphicsDevice;
class VoxelScene;

// The splat (Design/SampleRenderer.md §9, §10): every voxel as a screen-space rectangle, intersected per pixel with its
// box. The view splat writes reversed-Z depth and the visibility buffer ViewTargets::BeginSplat has bound; the shadow
// splat writes standard-Z depth alone into the map ShadowMap::BeginSplat has bound. Each kind comes in the aligned
// permutation, for the intact model, and the oriented one, whose boxes the explosion poses (§12).
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

  // An instance of the draw covers this many voxels; Shader/Splat.hlsli relies on the same number (§9.1).
  static constexpr std::uint32_t RECTANGLES_PER_INSTANCE = 256;
  static constexpr std::uint32_t RECTANGLE_INDEX_COUNT = RECTANGLES_PER_INSTANCE * 6;

  SplatPass(GraphicsDevice& _device, Kind _kind, Permutation _permutation = Permutation::Aligned);

  // One DrawIndexedInstanced per placed model, drawn in order, so that a tie in depth goes to the lower record (§4.2).
  // _viewConstants are the ViewConstants or ShadowViewConstants the kind reads, and _explosionConstants the
  // ExplosionConstants the oriented permutation reads; the aligned one ignores them.
  void Record(ID3D12GraphicsCommandList* _list, const VoxelScene& _scene, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants,
              D3D12_GPU_VIRTUAL_ADDRESS _explosionConstants = 0) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  winrt::com_ptr<ID3D12Resource> m_rectangleIndices;
  D3D12_INDEX_BUFFER_VIEW m_indexView{};
  Permutation m_permutation;
};

} // namespace NeuronClient
