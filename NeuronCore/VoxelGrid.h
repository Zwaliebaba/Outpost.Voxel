#pragma once

#include "Float3.h"
#include "Ray.h"
#include "TraceHit.h"
#include "VoxModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace NeuronCore
{

// The reference tracer for an intact model (Design/Archive/SampleRenderer.md §14): a dense grid over its voxels, walked by a 3D
// DDA that runs the ray-box twin on the occupied cells along the ray. The view and shadow passes are compared with it
// pixel for pixel.
//
// The walk decides in double precision which cells to test, and it tests every occupied cell the ray passes near, not
// only the ones it enters. IntersectBox computes in single precision, so a ray that grazes an edge can hit a cell it
// never enters, and a reference that skipped that cell would disagree with the brute force it stands in for.
class VoxelGrid
{
public:
  // A grid over every instance of _model, in the world: a cell holds the index of its record in the model's buffer.
  explicit VoxelGrid(const VoxModel& _model);

  // A grid over one part's records, in the part's own space, where a record's cell is the one whose minimum corner is
  // its coordinates: a cell holds the record's index in _records (Design/Archive/SpaceScene.md §15).
  explicit VoxelGrid(std::span<const std::uint32_t> _records);

  // A grid over _cells, each a cell's minimum corner: a cell holds the index of its first entry in _cells. The server
  // walks a composite's voxels so (Design/ADR/ADR-035).
  explicit VoxelGrid(std::span<const Int3> _cells);

  // The position of the grid's minimum corner, and its extent in cells: the tight bounds of its voxels.
  [[nodiscard]] Int3 Origin() const noexcept
  {
    return m_origin;
  }

  [[nodiscard]] Int3 Size() const noexcept
  {
    return m_size;
  }

  // What the cell whose minimum corner is _position holds, or NO_VOXEL. Where instances overlap, the first one's record
  // holds the cell: the view pass draws it first, and it wins the tie.
  [[nodiscard]] std::uint32_t VoxelAt(Int3 _position) const noexcept;

  // What the view pass resolves for _ray: the nearest hit of the aligned IntersectBox with canStartInBox false, at or
  // beyond _minDistance, over every voxel, with a tie going to the lower record. The same answer as TraceBoxes over
  // every voxel's box, found by visiting a few cells instead of all of them.
  [[nodiscard]] TraceHit Trace(const Ray& _ray, float _minDistance) const noexcept;

private:
  void Fill(std::span<const std::uint32_t> _records, std::span<const ModelInstance> _instances);

  [[nodiscard]] std::uint32_t CellAt(std::int32_t _x, std::int32_t _y, std::int32_t _z) const noexcept;

  Int3 m_origin{};
  Int3 m_size{};
  std::vector<std::uint32_t> m_cells; // what each cell holds, x fastest, then y
};

// The occupied cells a ray passes near while it crosses one cell of a grid: that cell and any of its 26 neighbors.
struct GridStep
{
  static constexpr std::size_t MAX_CELLS = 27;

  std::array<Int3, MAX_CELLS> cells;           // their minimum corners, in the grid's space
  std::array<std::uint32_t, MAX_CELLS> values; // what the grid holds in each
  std::size_t count;
  double leaveDistance; // the ray parameter at which the ray leaves the cell it crosses
};

// An occupied cell that a segment passes through: what the grid holds there, and the segment's parameter where it enters.
struct SegmentCell
{
  std::uint32_t value;
  double entry;
};

// Every occupied cell of _grid that the segment from _origin to _origin + _direction passes through, between parameters
// _from and _to of [0, 1], in the order it enters them, a tie going to the lower value (Design/ADR/ADR-035). A cell counts
// when the segment runs more than a thousandth of a cell inside it, so a segment that only touches a face, an edge or a
// corner passes it by, and one that starts on a cell's face and leaves it does not enter it, whatever the rounding of a
// point taken into the grid's space. In double precision, and in the grid's space.
[[nodiscard]] std::vector<SegmentCell> SegmentCells(const VoxelGrid& _grid, const std::array<double, 3>& _origin,
                                                    const std::array<double, 3>& _direction, double _from, double _to);

// VoxelGrid's walk along a ray, a cell at a time from the nearest, for a caller that tests the occupied cells its own
// way. Trace is one; the scene tracer is another, which walks a placement's grid in its part's space and tests each
// cell with the box the GPU draws, in the world (Design/Archive/SpaceScene.md §15). The ray is in double precision and in the
// grid's space, and its parameter is the caller's: a rigid transform leaves it unchanged.
class GridWalk
{
public:
  // A walk along the ray from _origin along _direction, from parameter _minDistance on. A ray that is not finite, or
  // has no direction, walks nothing.
  GridWalk(const VoxelGrid& _grid, const std::array<double, 3>& _origin, const std::array<double, 3>& _direction,
           double _minDistance) noexcept;

  // The next cell's step, or false once the ray has left the grid.
  [[nodiscard]] bool Next(GridStep& _step) noexcept;

  // How far beyond a hit the walk must go before no cell ahead can hold a nearer one: once a step leaves at a parameter
  // beyond the hit's plus this, the caller may stop.
  [[nodiscard]] double SettleDistance() const noexcept;

private:
  [[nodiscard]] double NextFace(std::size_t _axis) const noexcept;

  const VoxelGrid& m_grid;
  std::array<double, 3> m_origin;
  std::array<double, 3> m_direction;
  std::array<std::int32_t, 3> m_gridOrigin{};
  std::array<std::int32_t, 3> m_gridSize{};
  double m_directionLength = 0.0;
  double m_exit = 0.0;                  // the parameter at which the ray leaves the grid's box, widened by the margin
  double m_entered = 0.0;               // the parameter at which the ray entered the current cell
  std::array<std::int32_t, 3> m_cell{}; // the current cell, relative to the grid's minimum corner
  std::array<double, 3> m_leave{};      // the parameter at which the ray leaves the current cell along each axis
  bool m_done = false;
};

} // namespace NeuronCore
