#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstdint>

namespace NeuronClient
{

class GraphicsDevice;
class VoxelScene;

// The view splat (Design/SampleRenderer.md §9): every voxel as a screen-space rectangle, intersected per pixel with its
// box, into the depth and visibility buffers ViewTargets::BeginSplat has bound. The aligned perspective permutation.
class ViewSplatPass
{
public:
  // An instance of the draw covers this many voxels; Shader/Splat.hlsli relies on the same number (§9.1).
  static constexpr std::uint32_t RECTANGLES_PER_INSTANCE = 256;
  static constexpr std::uint32_t RECTANGLE_INDEX_COUNT = RECTANGLES_PER_INSTANCE * 6;

  explicit ViewSplatPass(GraphicsDevice& _device);

  // One DrawIndexedInstanced per placed model, drawn in order, so that a tie in depth goes to the lower record (§4.2).
  void Record(ID3D12GraphicsCommandList* _list, const VoxelScene& _scene, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  winrt::com_ptr<ID3D12Resource> m_rectangleIndices;
  D3D12_INDEX_BUFFER_VIEW m_indexView{};
};

} // namespace NeuronClient
