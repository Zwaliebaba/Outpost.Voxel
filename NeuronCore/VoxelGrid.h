#pragma once

#include "Float3.h"
#include "Ray.h"
#include "TraceHit.h"
#include "VoxModel.h"

#include <cstdint>
#include <vector>

namespace NeuronCore
{

// The reference tracer for an intact model (Design/SampleRenderer.md §14): a dense grid over its voxels, walked by a 3D
// DDA that runs the ray-box twin on the occupied cells along the ray. The view and shadow passes are compared with it
// pixel for pixel.
//
// The walk decides in double precision which cells to test, and it tests every occupied cell the ray passes near, not
// only the ones it enters. IntersectBox computes in single precision, so a ray that grazes an edge can hit a cell it
// never enters, and a reference that skipped that cell would disagree with the brute force it stands in for.
class VoxelGrid
{
public:
  explicit VoxelGrid(const VoxModel& _model);

  // The world position of the grid's minimum corner, and its extent in cells: the tight bounds of the model's voxels.
  [[nodiscard]] Int3 Origin() const noexcept
  {
    return m_origin;
  }

  [[nodiscard]] Int3 Size() const noexcept
  {
    return m_size;
  }

  // The record occupying the cell whose minimum corner is _position, or NO_VOXEL. Where instances overlap, the first
  // one's record holds the cell: the view pass draws it first, and it wins the tie.
  [[nodiscard]] std::uint32_t VoxelAt(Int3 _position) const noexcept;

  // What the view pass resolves for _ray: the nearest hit of the aligned IntersectBox with canStartInBox false, at or
  // beyond _minDistance, over every voxel, with a tie going to the lower record. The same answer as TraceBoxes over
  // every voxel's box, found by visiting a few cells instead of all of them.
  [[nodiscard]] TraceHit Trace(const Ray& _ray, float _minDistance) const noexcept;

private:
  [[nodiscard]] std::uint32_t CellAt(std::int32_t _x, std::int32_t _y, std::int32_t _z) const noexcept;

  Int3 m_origin{};
  Int3 m_size{};
  std::vector<std::uint32_t> m_cells; // a record index per cell, x fastest, then y
};

} // namespace NeuronCore
