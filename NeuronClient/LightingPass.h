#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstdint>

namespace NeuronClient
{

class GraphicsDevice;
class ShadowMap;
class ViewTargets;
class VoxelScene;

// The lighting pass (Design/Archive/SampleRenderer.md §8, §11): one compute dispatch that shades every pixel of the view from the
// depth and visibility buffers, the shadow map and the palettes, into the HDR color of ViewTargets. A pixel's voxel id
// leads to its record and palette through the frame's placements (Design/SpaceScene.md §7.3).
class LightingPass
{
public:
  // §8's group size; Shader/LightingPass.hlsli relies on the same number.
  static constexpr std::uint32_t GROUP_PIXELS = 8;

  explicit LightingPass(GraphicsDevice& _device);

  // The depth, the visibility buffer and the shadow map must be readable, as their EndSplat calls leave them, the HDR
  // color writable, as ViewTargets::BeginLighting leaves it, and the shader-visible heap set on _list. The constants are
  // ViewConstants, ShadowViewConstants and LightingConstants, and _placements the structured buffer of the placements
  // the splats drew, as many as LightingConstants says.
  void Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, const ShadowMap& _shadowMap, const VoxelScene& _scene,
              D3D12_GPU_VIRTUAL_ADDRESS _viewConstants, D3D12_GPU_VIRTUAL_ADDRESS _shadowViewConstants,
              D3D12_GPU_VIRTUAL_ADDRESS _lightingConstants, D3D12_GPU_VIRTUAL_ADDRESS _placements) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace NeuronClient
