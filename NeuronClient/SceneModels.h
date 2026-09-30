#pragma once

#include "SnapshotBuffer.h"

#include "Blast.h"
#include "Float3.h"
#include "Fragmentation.h"
#include "Message.h"
#include "Placement.h"
#include "RigidTransform.h"
#include "Sphere.h"
#include "VoxModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace NeuronClient
{

// The models and composites a welcome names, measured for drawing its entities (Design/Archive/SpaceScene.md §5.1, §7;
// Design/ADR/ADR-029). An entity stands where the middle of its composite's box is, turned by its rotation, and draws as
// one placement for each part of each of its composite's components, with the component's turn and translation and the
// part's origin folded into the placement's transform (§7.1, §7.2). Each placement takes its model's palette as the
// entity's side draws it (NeuronCore::SidePaletteIndex). A detonated entity draws its parts' debris, blasted from the mean
// of its composite's voxels with the entity's velocity at the event and the event's seed, at the time since the event,
// each part broken into the fragments its model breaks into (§5.5, §7.7, Design/ADR/ADR-024): so every client poses the
// same debris from the same event. A placement leaves out the voxels its entity has lost, whole or detonated, and a part
// that has lost every voxel draws nothing (Design/ADR/ADR-035). An entity that detonates after losing voxels, as a ship
// shot to pieces or a piece cut off from it does, blasts from what it has left: from its remaining voxels' mean, and sized
// by their box, as a composite of those voxels alone would.
class SceneModels
{
public:
  // _sideCount is the number of sides the welcome names. Throws std::invalid_argument for a component that names a model
  // _models lacks, and for a composite with no voxel, which no entity can stand in the middle of.
  SceneModels(std::span<const NeuronCore::VoxModel> _models, std::span<const NeuronCore::CompositeModel> _composites,
              std::size_t _sideCount);

  // The models, whose records the scene's record buffer holds model after model (NeuronCore::SceneRecords).
  [[nodiscard]] std::span<const NeuronCore::VoxModel> Models() const noexcept
  {
    return m_models;
  }

  // What the models break into when they detonate, which the renderer uploads and the placements' detonations view.
  [[nodiscard]] const NeuronCore::SceneFragments& Fragments() const noexcept
  {
    return m_fragments;
  }

  // Appends the placements that draw _entity, one per part of each of its composite's components that has a voxel left,
  // component after component and in the order of their parts, with its side's palettes, or their remembered variants
  // for an entity _remembered out of sight (NeuronCore::RememberedPaletteIndex, Design/ADR/ADR-032), and each with the
  // part's share of the entity's mask. Their ids are NeuronCore::AssignVoxelIds's to give. Throws std::out_of_range for
  // a composite the welcome did not name.
  void Place(const SampledEntity& _entity, std::vector<NeuronCore::Placement>& _placements, bool _remembered = false) const;

  // The light of _entity's detonation, in the world (Design/ADR/ADR-025): from the mean of the voxels it has left,
  // drifting as its debris drifts, scaled by their radius, with the event's seed and the time since; nothing while it has
  // not detonated.
  [[nodiscard]] std::optional<NeuronCore::Blast> Blast(const SampledEntity& _entity) const;

  // The sphere around what _entity draws now: its composite's, whole, or its debris's at its time.
  [[nodiscard]] NeuronCore::Sphere Extent(const SampledEntity& _entity) const;

  // The sphere around everything _entity can draw from now on: whole where it is, and its debris at every time, from its
  // event or, while it is whole, as if it detonated now at its velocity. The sun's view is fitted around it.
  [[nodiscard]] NeuronCore::Sphere Reach(const SampledEntity& _entity) const;

  // The radius of composite _composite's sphere, about the middle of its box.
  [[nodiscard]] float Radius(std::uint16_t _composite) const;

private:
  // One part of one of a composite's components.
  struct Part
  {
    NeuronCore::Placement whole;          // untransformed, with its model's own palette
    NeuronCore::RigidTransform component; // from its model's space into the composite's
    bool isIdentity;                      // the component leaves its model where it is
    NeuronCore::Float3 origin;            // of the part's space, in its model's
    std::uint32_t model;
    std::uint32_t part;       // within its model
    std::uint32_t firstVoxel; // among its composite's voxels, in the order of an entity's mask (NeuronCore::CompositeVoxels)
  };

  struct Measure
  {
    NeuronCore::Float3 middle;   // of its box, where an entity's position puts it
    float radius;                // of the sphere about the middle
    NeuronCore::Float3 centroid; // of its voxels' centers, the blast origin
    std::uint32_t firstPart;     // its parts in m_parts
    std::uint32_t partCount;
  };

  // Where a detonation blasts from and how large, in its composite's space.
  struct Remains
  {
    NeuronCore::Float3 centroid; // of the voxels it has left
    float radius;                // of the sphere about the middle of their box
  };

  // _entity's remains: its composite's centroid and radius while it has every voxel, or when it has none left, and
  // otherwise those of the voxels it has left.
  [[nodiscard]] Remains RemainsOf(const SampledEntity& _entity) const;

  std::vector<NeuronCore::VoxModel> m_models;
  NeuronCore::SceneFragments m_fragments; // what a detonated placement's fragments view
  std::size_t m_sideCount;
  std::vector<Measure> m_measures;
  std::vector<Part> m_parts;                          // every composite's parts, composite after composite
  std::vector<std::vector<NeuronCore::Int3>> m_cells; // by composite, its voxels' cells in the order of a mask
};

} // namespace NeuronClient
