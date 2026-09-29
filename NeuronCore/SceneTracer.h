#pragma once

#include "Box.h"
#include "Placement.h"
#include "Ray.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelGrid.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace NeuronCore
{

// The reference tracer for a frame of placements (Design/Archive/SpaceScene.md §15): what the view and shadow passes resolve for
// a ray when they draw the placements, as ids. Like VoxelGrid, it walks each whole placement's grid, in its part's space,
// and tests the occupied cells the ray passes near with the box the GPU draws, in the world, through the same
// permutation; a detonated placement's posed boxes it tests one by one, as TraceBoxes does.
//
// A tie goes to the lower id. Within a placement that is the GPU's rule too, since its records draw in order; across
// placements the GPU keeps the one it drew first (§7.3), and the tests keep whole placements apart, as the world does,
// so that no such tie arises.
class SceneTracer
{
public:
  // _models are the scene's, in the order of its record buffer, and _placements the frame's, with their ids assigned.
  // The tracer keeps copies of both.
  SceneTracer(std::span<const VoxModel> _models, std::span<const Placement> _placements);

  // The nearest hit at or beyond _minDistance over every voxel of every placement, with the voxel's id.
  [[nodiscard]] TraceHit Trace(const Ray& _ray, float _minDistance) const noexcept;

private:
  // What the tracer holds for one placement: the grid of its part while it is whole, or its posed boxes once detonated.
  struct Traced
  {
    std::size_t grid;       // into m_grids, for a whole placement
    std::vector<Box> boxes; // for a detonated one, in record order
  };

  std::vector<std::uint32_t> m_records;
  std::vector<Placement> m_placements;
  std::vector<VoxelGrid> m_grids;
  std::vector<Traced> m_traced;
};

} // namespace NeuronCore
