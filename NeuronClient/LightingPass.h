#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "Placement.h"

#include <cstdint>
#include <span>

namespace NeuronClient
{

class GraphicsDevice;
class ShadowMap;
class UploadRing;
class ViewTargets;
class VoxelScene;

// Pushes the heat of _placements (Design/ADR/ADR-025) into _ring as the lighting pass's structured buffer, parallel to the
// placements: NeuronCore::MakePlacementHeat of each.
[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS PushPlacementHeat(UploadRing& _ring, std::span<const NeuronCore::Placement> _placements);

// The ring bytes PushPlacementHeat takes for _placementCount placements, aligned as the ring aligns every piece.
[[nodiscard]] std::uint64_t PlacementHeatBytes(std::size_t _placementCount) noexcept;

// The lighting pass (Design/Archive/SampleRenderer.md §8, §11): one compute dispatch that shades every pixel of the view from the
// depth and visibility buffers, the shadow map and the palettes, into the HDR color of ViewTargets. A pixel's voxel id
// leads to its record and palette through the frame's placements (Design/Archive/SpaceScene.md §7.3), and to its heat
// through its placement's and its record's fragment; the frame's flashes light every voxel (Design/ADR/ADR-025).
class LightingPass
{
public:
  // §8's group size; Shader/LightingPass.hlsli relies on the same number.
  static constexpr std::uint32_t GROUP_PIXELS = 8;

  explicit LightingPass(GraphicsDevice& _device);

  // The depth, the visibility buffer and the shadow map must be readable, as their EndSplat calls leave them, the HDR
  // color writable, as ViewTargets::BeginLighting leaves it, and the shader-visible heap set on _list. The constants are
  // ViewConstants, ShadowViewConstants, LightingConstants and NeuronCore::BlastLighting, _placements the structured
  // buffer of the placements the splats drew, as many as LightingConstants says, and _placementHeat their heat, as
  // PushPlacementHeat pushes it.
  void Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, const ShadowMap& _shadowMap, const VoxelScene& _scene,
              D3D12_GPU_VIRTUAL_ADDRESS _viewConstants, D3D12_GPU_VIRTUAL_ADDRESS _shadowViewConstants,
              D3D12_GPU_VIRTUAL_ADDRESS _lightingConstants, D3D12_GPU_VIRTUAL_ADDRESS _placements, D3D12_GPU_VIRTUAL_ADDRESS _blastLighting,
              D3D12_GPU_VIRTUAL_ADDRESS _placementHeat) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace NeuronClient
