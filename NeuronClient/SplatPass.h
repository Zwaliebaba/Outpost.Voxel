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
// splat writes standard-Z depth alone into the map ShadowMap::BeginSplat has bound. Both are the aligned permutation.
class SplatPass
{
public:
  enum class Kind : std::uint8_t
  {
    View,  // perspective, ViewConstants
    Shadow // orthographic, ShadowViewConstants
  };

  // An instance of the draw covers this many voxels; Shader/Splat.hlsli relies on the same number (§9.1).
  static constexpr std::uint32_t RECTANGLES_PER_INSTANCE = 256;
  static constexpr std::uint32_t RECTANGLE_INDEX_COUNT = RECTANGLES_PER_INSTANCE * 6;

  SplatPass(GraphicsDevice& _device, Kind _kind);

  // One DrawIndexedInstanced per placed model, drawn in order, so that a tie in depth goes to the lower record (§4.2).
  // _viewConstants are the ViewConstants or ShadowViewConstants the kind reads.
  void Record(ID3D12GraphicsCommandList* _list, const VoxelScene& _scene, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  winrt::com_ptr<ID3D12Resource> m_rectangleIndices;
  D3D12_INDEX_BUFFER_VIEW m_indexView{};
};

} // namespace NeuronClient
