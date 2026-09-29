#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "DebugView.h"

#include <cstdint>

namespace NeuronClient
{

class GraphicsDevice;
class ShadowMap;
class ViewTargets;
class VoxelScene;

// The debug view pass (Design/Archive/SampleRenderer.md §11): one triangle over the render target bound by the caller, which
// shows the chosen view of the visibility buffer, the shadow map or the overdraw count in place of the lit image. _targetFormat is the
// render target's view format; the application's is the swap chain's sRGB view, and a test's may be a float format
// that keeps the linear color exact.
class DebugViewPass
{
public:
  DebugViewPass(GraphicsDevice& _device, DXGI_FORMAT _targetFormat);

  // The visibility buffer and the shadow map must be readable, as their EndSplat calls leave them, and so must the
  // overdraw count, as EndOverdraw leaves it; the overdraw view shows what the view splat's overdraw variant counted
  // last. The shader-visible heap must be set on _list. _placements is the structured buffer of the _placementCount
  // placements the splats drew, through which a pixel's voxel id finds its palette (Design/Archive/SpaceScene.md §7.3).
  void Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, const ShadowMap& _shadowMap, const VoxelScene& _scene,
              D3D12_GPU_VIRTUAL_ADDRESS _viewConstants, D3D12_GPU_VIRTUAL_ADDRESS _placements, std::uint32_t _placementCount,
              NeuronCore::DebugView _view) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace NeuronClient
