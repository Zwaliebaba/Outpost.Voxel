#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "Placement.h"

#include <cstdint>
#include <span>
#include <vector>

namespace NeuronClient
{

class GraphicsDevice;
class UploadRing;
class VoxelScene;

// One placement's draw (Design/Archive/SpaceScene.md §7.4).
struct SplatDraw
{
  std::uint32_t placement; // its index in the frame's structured buffer of placements
  std::uint32_t recordCount;
  bool oriented;                       // the oriented permutation draws it; otherwise the aligned one (§7.2)
  D3D12_GPU_VIRTUAL_ADDRESS explosion; // the ExplosionConstants the oriented permutation reads for it
};

// A frame's placements as the splat passes read them: their PlacementConstants in one structured buffer, and a draw for
// each, in their order.
struct SplatPlacements
{
  D3D12_GPU_VIRTUAL_ADDRESS constants;
  std::vector<SplatDraw> draws;
};

// Pushes _placements into _ring as the splat passes read them. Each placement the oriented permutation draws gets
// ExplosionConstants: a detonated one its own, and every whole one a single block at rest.
[[nodiscard]] SplatPlacements PushSplatPlacements(UploadRing& _ring, std::span<const NeuronCore::Placement> _placements);

// The most PushSplatPlacements takes of a ring for _placements, its alignment included.
[[nodiscard]] std::uint64_t SplatPlacementBytes(std::span<const NeuronCore::Placement> _placements) noexcept;

// The splat (Design/Archive/SampleRenderer.md §9, §10): every voxel of every placement drawn as a screen-space rectangle,
// intersected per pixel with its box. The view splat writes reversed-Z depth and the visibility buffer
// ViewTargets::BeginSplat has bound; the shadow splat writes standard-Z depth alone into the map ShadowMap::BeginSplat has
// bound. A pass holds both permutations: the aligned one, for a whole placement turned by a symmetry of the cube, and the
// oriented one, for any other, whose boxes turn with the placement and its detonation poses (Design/Archive/SpaceScene.md §7.2,
// §7.7).
class SplatPass
{
public:
  enum class Kind : std::uint8_t
  {
    View,  // perspective, ViewConstants
    Shadow // orthographic, ShadowViewConstants
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
  SplatPass(GraphicsDevice& _device, Kind _kind, Variant _variant = Variant::Standard);

  // One DrawIndexedInstanced per draw, in _draws' order, each through the permutation it names; within a draw the records
  // go in order, so that a tie in depth goes to the lower one (§4.2), and between draws to the first drawn
  // (Design/Archive/SpaceScene.md §7.3). _viewConstants are the ViewConstants or ShadowViewConstants the kind reads, and
  // _placements the structured buffer PushSplatPlacements made. The overdraw variant counts into the RWTexture2D<uint> of
  // _overdrawTable, which the others ignore.
  void Record(ID3D12GraphicsCommandList* _list, const VoxelScene& _scene, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants,
              D3D12_GPU_VIRTUAL_ADDRESS _placements, std::span<const SplatDraw> _draws,
              D3D12_GPU_DESCRIPTOR_HANDLE _overdrawTable = {}) const;

  // The overdraw variant, whose Record needs the overdraw count cleared and writable (ViewTargets::BeginOverdraw).
  [[nodiscard]] bool CountsOverdraw() const noexcept
  {
    return m_variant == Variant::Overdraw;
  }

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_alignedPipeline;
  winrt::com_ptr<ID3D12PipelineState> m_orientedPipeline;
  winrt::com_ptr<ID3D12Resource> m_rectangleIndices;
  D3D12_INDEX_BUFFER_VIEW m_indexView{};
  Variant m_variant;
};

} // namespace NeuronClient
