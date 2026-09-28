#pragma once

#include "SnapshotBuffer.h"

#include "Float3.h"
#include "Placement.h"
#include "Sphere.h"
#include "VoxModel.h"

#include <cstdint>
#include <span>
#include <vector>

namespace NeuronClient
{

// The models a welcome names, measured for drawing its entities (Design/SpaceScene.md §5.1, §7). An entity stands where
// the middle of its model's box is, turned by its rotation, and draws as one placement for each part of its model, with
// the part's origin folded into the placement's transform (§7.1, §7.2). A detonated one draws its parts' debris, blasted
// from the mean of its model's voxels with the entity's velocity at the event and the event's seed, at the time since
// the event (§5.5, §7.7): so every client poses the same debris from the same event.
class SceneModels
{
public:
  // Throws std::invalid_argument for a model with no voxel, which no entity can stand in the middle of.
  explicit SceneModels(std::span<const NeuronCore::VoxModel> _models);

  // The models, whose records the scene's record buffer holds model after model (NeuronCore::SceneRecords).
  [[nodiscard]] std::span<const NeuronCore::VoxModel> Models() const noexcept
  {
    return m_models;
  }

  // Appends the placements that draw _entity, one per part of its model in the order of its parts. Their ids are
  // NeuronCore::AssignVoxelIds's to give. Throws std::out_of_range for a model the welcome did not name.
  void Place(const SampledEntity& _entity, std::vector<NeuronCore::Placement>& _placements) const;

  // The sphere around what _entity draws now: its model's, whole, or its debris's at its time.
  [[nodiscard]] NeuronCore::Sphere Extent(const SampledEntity& _entity) const;

  // The sphere around everything _entity can draw from now on: whole where it is, and its debris at every time, from its
  // event or, while it is whole, as if it detonated now at its velocity. The sun's view is fitted around it.
  [[nodiscard]] NeuronCore::Sphere Reach(const SampledEntity& _entity) const;

  // The radius of model _model's sphere, about the middle of its box.
  [[nodiscard]] float Radius(std::uint16_t _model) const;

private:
  struct Measure
  {
    NeuronCore::Float3 middle;   // of its box, where an entity's position puts it
    float radius;                // of the sphere about the middle
    NeuronCore::Float3 centroid; // of its voxels' centers, the blast origin
    std::uint32_t firstPart;     // its parts' whole placements in m_parts
    std::uint32_t partCount;
  };

  std::vector<NeuronCore::VoxModel> m_models;
  std::vector<Measure> m_measures;
  std::vector<NeuronCore::Placement> m_parts; // every model's parts, whole and untransformed, model after model
};

} // namespace NeuronClient
