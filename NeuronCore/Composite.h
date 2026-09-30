#pragma once

#include "Float3.h"
#include "Message.h"
#include "RigidTransform.h"
#include "VoxModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace NeuronCore
{

// The geometry of a composite model (Design/ADR/ADR-029): its components' models placed together in its own space. The
// server that places an entity and the client that draws it measure a composite through these same functions, as they
// measured a model through OccupiedBounds before (ADR-018).

// The transform that takes _component's model into its composite's space: its rotation, then its translation.
[[nodiscard]] RigidTransform ComponentTransform(const CompositeComponent& _component) noexcept;

// Whether _component leaves its model where it is, as a single model's composite does.
[[nodiscard]] bool IsIdentityComponent(const CompositeComponent& _component) noexcept;

// The box around every voxel of _composite, in its space: each component's VoxelBounds, turned and moved there, whole
// cells since every component turns by one of the cube's rotations. An entity stands where the middle of this box is
// (Design/Archive/SpaceScene.md §5.1). Nothing when no component holds a voxel. _models are the welcome's, which the
// components name.
[[nodiscard]] std::optional<VoxelBounds> CompositeBounds(std::span<const VoxModel> _models, const CompositeModel& _composite);

// The mean of _composite's voxel centers, in its space: where its detonation blasts from (§5.5). A composite of one model
// left where it is has that model's VoxelCentroid, exactly.
[[nodiscard]] Float3 CompositeCentroid(std::span<const VoxModel> _models, const CompositeModel& _composite);

// A composite of each of _modelCount models alone and left where it is: how a world whose entities are single models, as
// the space scene's are, names them. Composite i is model i.
[[nodiscard]] std::vector<CompositeModel> SingleModelComposites(std::size_t _modelCount);

// One voxel of a composite (Design/ADR/ADR-035): the cell it fills in the composite's space, whole since every component
// turns by one of the cube's rotations and moves by whole voxels; the component it belongs to; and its record's color,
// its palette entry minus one (R14).
struct CompositeVoxel
{
  Int3 cell;
  std::uint16_t component;
  std::uint8_t color;
};

// Every voxel of _composite, in the order an EntityMask's bits run: component after component, each component's model part
// after part, and each part's records in their order. _models are the welcome's, which the components name.
[[nodiscard]] std::vector<CompositeVoxel> CompositeVoxels(std::span<const VoxModel> _models, const CompositeModel& _composite);

// How many voxels _composite holds: the bits of its entities' masks.
[[nodiscard]] std::uint32_t CompositeVoxelCount(std::span<const VoxModel> _models, const CompositeModel& _composite) noexcept;

} // namespace NeuronCore
