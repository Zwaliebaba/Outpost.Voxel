#pragma once

#include "Blast.h"
#include "Box.h"
#include "Explosion.h"
#include "Float3.h"
#include "Fragmentation.h"
#include "RigidTransform.h"
#include "Sphere.h"
#include "TraceHit.h"
#include "VoxModel.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace NeuronCore
{

// What FindPlacement gives for an id that no placement holds.
inline constexpr std::uint32_t NO_PLACEMENT = 0xFFFFFFFFu;

// What a detonated placement adds (Design/Archive/SpaceScene.md §7.7, Design/ADR/ADR-024, ADR-025): the detonation, in its
// part's space, the time since, the fragments its part breaks into, and how they heat.
struct PlacementDetonation
{
  ExplosionParameters parameters; // in the part's space: the blast origin, and the inherited velocity turned into it
  float timeSeconds;              // since the detonation
  PartFragments fragments;        // its part's, from the scene's SceneFragments
  HeatParameters heat;            // none, a heat distance of 0, leaves its debris cold
};

// A model's part under a rigid transform, whole or detonated (§7): what the renderer draws with one draw, and what the
// scene tracer traces. In the part's space, the voxel of record r is the unit cell whose minimum corner is r.xyz.
struct Placement
{
  RigidTransform transform; // from the part's space into the world (§7.2)
  Float3 lower;             // the box around the part's voxels, in its space
  Float3 upper;
  std::uint32_t firstRecord; // the part's records in the scene's record buffer (§7.1)
  std::uint32_t recordCount;
  std::uint32_t paletteIndex; // its model's palette
  std::uint32_t firstVoxel;   // the id of its first voxel (§7.3); AssignVoxelIds gives it
  // Empty while the placement is whole.
  std::optional<PlacementDetonation> detonation;
};

// The scene's record buffer: every model's records, model after model (§7.1). A model's records are stored once, however
// many placements draw them.
[[nodiscard]] std::vector<std::uint32_t> SceneRecords(std::span<const VoxModel> _models);

// Where each model's records start in the scene's record buffer.
[[nodiscard]] std::vector<std::uint32_t> ModelFirstRecords(std::span<const VoxModel> _models);

// A whole placement of part _part of _model under _transform. _model is model _modelIndex of the scene, whose palette the
// placement takes, and its records start at _modelFirstRecord in the scene's record buffer. The placement's id is
// AssignVoxelIds's to give.
[[nodiscard]] Placement PlacePart(const VoxModel& _model, std::uint32_t _modelIndex, std::uint32_t _modelFirstRecord, std::uint32_t _part,
                                  const RigidTransform& _transform) noexcept;

// Gives each placement its first voxel's id: the number of voxels the placements before it hold (§7.3). False, with the
// placements unchanged, when the ids would reach NO_VOXEL, which the host refuses.
[[nodiscard]] bool AssignVoxelIds(std::span<Placement> _placements) noexcept;

// Whether _placement draws with the aligned splat: whole, and turned by one of the cube's symmetries (§7.2). Any other
// draws with the oriented splat.
[[nodiscard]] bool IsAlignedPlacement(const Placement& _placement) noexcept;

// The box the splat pass draws for voxel _voxel of _placement, whose packed record is _record (§7.2, §7.7). Whole, it is
// the voxel's cell taken into the world, axis-aligned when the placement is aligned and turned with it when not.
// Detonated, it is the voxel's pose in the part's space, as part of its fragment, taken into the world; at time 0 the pose
// is the rest exactly, and the shader skips it. The twin of PlacedVoxelBox in
// Shader/Splat.hlsli (R15).
[[nodiscard]] Box PlacedVoxelBox(const Placement& _placement, std::uint32_t _voxel, std::uint32_t _record) noexcept;

// The envelope of _detonation, _placement's, in its part's space: BoundExplosion over its part's box and fragments.
[[nodiscard]] ExplosionEnvelope PlacementEnvelope(const Placement& _placement, const PlacementDetonation& _detonation) noexcept;

// What the lighting pass reads of _placement's heat: its detonation's blast origin, time, shock speed and heat, and its
// model's first fragment; or, for a whole placement, zero, whose time 0 heats nothing (ADR-025).
[[nodiscard]] PlacementHeat MakePlacementHeat(const Placement& _placement) noexcept;

// The sphere around everything _placement draws, which the views cull by (§7.4): around its part's box, or around its
// detonation's envelope at its time.
[[nodiscard]] Sphere PlacementSphere(const Placement& _placement) noexcept;

// The index of the placement that holds voxel id _voxel, or NO_PLACEMENT: a binary search over the placements' first
// voxels, which rise with their order (§7.3). The twin of FindPlacement in Shader/Placement.hlsli (R15).
[[nodiscard]] std::uint32_t FindPlacement(std::span<const Placement> _placements, std::uint32_t _voxel) noexcept;

// Where a voxel id leads (§7.3): the record its placement draws for it, in the scene's record buffer, and the palette
// the placement takes.
struct PlacedVoxel
{
  std::uint32_t record;
  std::uint32_t paletteIndex;
};

// The record and palette of voxel id _voxel, through the placement FindPlacement finds; nothing for an id no placement
// holds. The lighting pass and the debug views find a pixel's palette entry this way. The twin of FindVoxel in
// Shader/Placement.hlsli (R15).
[[nodiscard]] std::optional<PlacedVoxel> FindVoxel(std::span<const Placement> _placements, std::uint32_t _voxel) noexcept;

} // namespace NeuronCore
