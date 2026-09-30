#include "pch.h"

#include "VoxelGrid.h"

#include "Box.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NeuronCore
{
namespace
{

// How near, in cells, the ray must pass to a cell for the walk to test it. IntersectBox computes in single precision,
// and within a few thousand units of the origin its error is a small fraction of this: ulp(4096) is 1/2048 of a cell.
constexpr double NEAR_CELLS = 1.0 / 64.0;

// How far past the best hit so far, in cells, the walk goes on before it trusts that hit.
constexpr double SETTLE_CELLS = 2.0;

// How far a segment must run inside a cell, in cells, for SegmentCells to count it: more than the rounding of a point
// taken into a model's space and back, so that a segment that starts on a cell's face and leaves it does not enter it.
constexpr double SEGMENT_INSIDE_CELLS = 1.0e-3;

// The neighbors along one axis that the ray passes near while it crosses a cell: 0, and -1 or 1 or both.
struct AxisNeighbors
{
  std::array<std::int32_t, 3> offsets;
  std::size_t count;
};

// _entry and _exit are the ray's coordinate along one axis where it enters and leaves the cell, relative to the cell's
// minimum corner. The coordinate is linear along the ray, so its extremes across the cell are at the two ends.
[[nodiscard]] AxisNeighbors NearNeighbors(double _entry, double _exit) noexcept
{
  AxisNeighbors neighbors{{0, 0, 0}, 1};
  if (std::min(_entry, _exit) < NEAR_CELLS)
  {
    neighbors.offsets[neighbors.count++] = -1;
  }
  if (std::max(_entry, _exit) > 1.0 - NEAR_CELLS)
  {
    neighbors.offsets[neighbors.count++] = 1;
  }
  return neighbors;
}

} // namespace

VoxelGrid::VoxelGrid(const VoxModel& _model)
{
  Fill(_model.records, _model.instances);
}

VoxelGrid::VoxelGrid(std::span<const std::uint32_t> _records)
{
  // One instance at the origin, so that a cell is a record's coordinates and holds its index.
  const ModelInstance part{{0, 0, 0}, {256, 256, 256}, 0, static_cast<std::uint32_t>(_records.size())};
  Fill(_records, {&part, 1});
}

VoxelGrid::VoxelGrid(std::span<const Int3> _cells)
{
  if (_cells.empty())
  {
    return;
  }
  Int3 lower = _cells.front();
  Int3 upper = _cells.front() + Int3{1, 1, 1};
  for (const Int3 cell : _cells)
  {
    lower = {std::min(lower.x, cell.x), std::min(lower.y, cell.y), std::min(lower.z, cell.z)};
    upper = {std::max(upper.x, cell.x + 1), std::max(upper.y, cell.y + 1), std::max(upper.z, cell.z + 1)};
  }
  m_origin = lower;
  m_size = upper - lower;
  const auto sizeX = static_cast<std::size_t>(m_size.x);
  const auto sizeY = static_cast<std::size_t>(m_size.y);
  m_cells.assign(sizeX * sizeY * static_cast<std::size_t>(m_size.z), NO_VOXEL);
  for (std::size_t index = 0; index < _cells.size(); ++index)
  {
    const Int3 cell = _cells[index] - m_origin;
    std::uint32_t& occupant =
      m_cells[static_cast<std::size_t>(cell.x) + sizeX * (static_cast<std::size_t>(cell.y) + sizeY * static_cast<std::size_t>(cell.z))];
    if (occupant == NO_VOXEL)
    {
      occupant = static_cast<std::uint32_t>(index);
    }
  }
}

void VoxelGrid::Fill(std::span<const std::uint32_t> _records, std::span<const ModelInstance> _instances)
{
  const auto position = [_records](const ModelInstance& _instance, std::uint32_t _index)
  {
    const VoxelRecord voxel = UnpackVoxelRecord(_records[_instance.firstRecord + _index]);
    return _instance.origin + Int3{voxel.x, voxel.y, voxel.z};
  };

  constexpr std::int32_t LOWEST = std::numeric_limits<std::int32_t>::lowest();
  constexpr std::int32_t HIGHEST = std::numeric_limits<std::int32_t>::max();
  Int3 lower{HIGHEST, HIGHEST, HIGHEST};
  Int3 upper{LOWEST, LOWEST, LOWEST};
  for (const ModelInstance& instance : _instances)
  {
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      const Int3 cell = position(instance, i);
      lower = {std::min(lower.x, cell.x), std::min(lower.y, cell.y), std::min(lower.z, cell.z)};
      upper = {std::max(upper.x, cell.x + 1), std::max(upper.y, cell.y + 1), std::max(upper.z, cell.z + 1)};
    }
  }
  if (lower.x >= upper.x)
  {
    return; // no voxels: an empty grid, which every ray misses
  }

  m_origin = lower;
  m_size = upper - lower;
  const auto sizeX = static_cast<std::size_t>(m_size.x);
  const auto sizeY = static_cast<std::size_t>(m_size.y);
  m_cells.assign(sizeX * sizeY * static_cast<std::size_t>(m_size.z), NO_VOXEL);
  for (const ModelInstance& instance : _instances)
  {
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      const Int3 cell = position(instance, i) - m_origin;
      std::uint32_t& occupant =
        m_cells[static_cast<std::size_t>(cell.x) + sizeX * (static_cast<std::size_t>(cell.y) + sizeY * static_cast<std::size_t>(cell.z))];
      if (occupant == NO_VOXEL)
      {
        occupant = instance.firstRecord + i;
      }
    }
  }
}

std::uint32_t VoxelGrid::VoxelAt(Int3 _position) const noexcept
{
  const Int3 cell = _position - m_origin;
  return CellAt(cell.x, cell.y, cell.z);
}

std::uint32_t VoxelGrid::CellAt(std::int32_t _x, std::int32_t _y, std::int32_t _z) const noexcept
{
  if (_x < 0 || _y < 0 || _z < 0 || _x >= m_size.x || _y >= m_size.y || _z >= m_size.z)
  {
    return NO_VOXEL;
  }
  const auto sizeX = static_cast<std::size_t>(m_size.x);
  const auto sizeY = static_cast<std::size_t>(m_size.y);
  return m_cells[static_cast<std::size_t>(_x) + sizeX * (static_cast<std::size_t>(_y) + sizeY * static_cast<std::size_t>(_z))];
}

TraceHit VoxelGrid::Trace(const Ray& _ray, float _minDistance) const noexcept
{
  TraceHit best{NO_VOXEL, std::numeric_limits<float>::infinity(), {0.0f, 0.0f, 0.0f}};
  GridWalk walk(*this, {_ray.origin.x, _ray.origin.y, _ray.origin.z}, {_ray.direction.x, _ray.direction.y, _ray.direction.z},
                static_cast<double>(_minDistance));
  const Float3 invDirection = InverseDirection(_ray);
  GridStep step{};
  while (walk.Next(step))
  {
    for (std::size_t i = 0; i < step.count; ++i)
    {
      const std::uint32_t voxel = step.values[i];
      float distance = 0.0f;
      Float3 normal{};
      if (IntersectBox<false, false>(CellBox(step.cells[i]), _ray.origin, _ray.direction, invDirection, distance, normal) &&
          distance >= _minDistance && (distance < best.distance || (distance == best.distance && voxel < best.voxel)))
      {
        best = {voxel, distance, normal};
      }
    }
    if (best.voxel != NO_VOXEL && step.leaveDistance > static_cast<double>(best.distance) + walk.SettleDistance())
    {
      break;
    }
  }
  return best;
}

std::vector<SegmentCell> SegmentCells(const VoxelGrid& _grid, const std::array<double, 3>& _origin, const std::array<double, 3>& _direction,
                                      double _from, double _to)
{
  std::vector<SegmentCell> cells;
  const double length = std::hypot(_direction[0], _direction[1], _direction[2]);
  GridWalk walk(_grid, _origin, _direction, _from);
  GridStep step{};
  while (walk.Next(step))
  {
    for (std::size_t i = 0; i < step.count; ++i)
    {
      // The stretch of the segment inside the cell, by slabs: an axis the segment does not move along holds it when its
      // coordinate lies in the cell's half-open span, so that a segment along a face belongs to one cell of the two.
      const std::array<std::int32_t, 3> corner{step.cells[i].x, step.cells[i].y, step.cells[i].z};
      double entry = _from;
      double exit = _to;
      for (std::size_t axis = 0; axis < 3 && entry < exit; ++axis)
      {
        const auto lower = static_cast<double>(corner[axis]);
        const double upper = lower + 1.0;
        if (_direction[axis] == 0.0)
        {
          if (_origin[axis] < lower || _origin[axis] >= upper)
          {
            exit = entry;
          }
          continue;
        }
        const double first = (lower - _origin[axis]) / _direction[axis];
        const double second = (upper - _origin[axis]) / _direction[axis];
        entry = std::max(entry, std::min(first, second));
        exit = std::min(exit, std::max(first, second));
      }
      if ((exit - entry) * length > SEGMENT_INSIDE_CELLS)
      {
        cells.push_back({step.values[i], entry});
      }
    }
    if (step.leaveDistance >= _to)
    {
      break;
    }
  }
  // A cell the walk met twice, as the neighbor of two cells it crossed, counts once.
  std::ranges::sort(cells, [](const SegmentCell& _a, const SegmentCell& _b)
                    { return _a.value < _b.value || (_a.value == _b.value && _a.entry < _b.entry); });
  const auto repeated = std::ranges::unique(cells, [](const SegmentCell& _a, const SegmentCell& _b) { return _a.value == _b.value; });
  cells.erase(repeated.begin(), repeated.end());
  std::ranges::sort(cells, [](const SegmentCell& _a, const SegmentCell& _b)
                    { return _a.entry < _b.entry || (_a.entry == _b.entry && _a.value < _b.value); });
  return cells;
}

GridWalk::GridWalk(const VoxelGrid& _grid, const std::array<double, 3>& _origin, const std::array<double, 3>& _direction,
                   double _minDistance) noexcept
  : m_grid(_grid),
    m_origin(_origin),
    m_direction(_direction)
{
  const Int3 gridOrigin = _grid.Origin();
  const Int3 gridSize = _grid.Size();
  m_gridOrigin = {gridOrigin.x, gridOrigin.y, gridOrigin.z};
  m_gridSize = {gridSize.x, gridSize.y, gridSize.z};
  m_directionLength = std::hypot(m_direction[0], m_direction[1], m_direction[2]);
  const bool finite =
    std::isfinite(m_origin[0]) && std::isfinite(m_origin[1]) && std::isfinite(m_origin[2]) && std::isfinite(m_directionLength);
  if (gridSize.x == 0 || !finite || !(m_directionLength > 0.0))
  {
    m_done = true;
    return;
  }

  // The stretch of the ray, at or beyond _minDistance, that passes within NEAR_CELLS of the grid.
  double tEnter = std::max(0.0, _minDistance);
  double tExit = std::numeric_limits<double>::infinity();
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    const double lower = static_cast<double>(m_gridOrigin[axis]) - NEAR_CELLS;
    const double upper = static_cast<double>(m_gridOrigin[axis]) + static_cast<double>(m_gridSize[axis]) + NEAR_CELLS;
    if (m_direction[axis] == 0.0)
    {
      if (m_origin[axis] < lower || m_origin[axis] > upper)
      {
        m_done = true;
        return;
      }
      continue;
    }
    const double first = (lower - m_origin[axis]) / m_direction[axis];
    const double second = (upper - m_origin[axis]) / m_direction[axis];
    tEnter = std::max(tEnter, std::min(first, second));
    tExit = std::min(tExit, std::max(first, second));
  }
  if (tEnter > tExit)
  {
    m_done = true;
    return;
  }

  // The cell the walk starts in, which may lie just outside the grid, and the ray parameter at which the ray leaves it
  // along each axis.
  m_exit = tExit;
  m_entered = tEnter;
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    const double position = m_origin[axis] + m_direction[axis] * tEnter - static_cast<double>(m_gridOrigin[axis]);
    m_cell[axis] = static_cast<std::int32_t>(std::clamp(std::floor(position), -1.0, static_cast<double>(m_gridSize[axis])));
    m_leave[axis] = NextFace(axis);
  }
}

bool GridWalk::Next(GridStep& _step) noexcept
{
  if (m_done)
  {
    return false;
  }
  std::size_t exitAxis = 0;
  if (m_leave[1] < m_leave[exitAxis])
  {
    exitAxis = 1;
  }
  if (m_leave[2] < m_leave[exitAxis])
  {
    exitAxis = 2;
  }
  const double left = std::min(m_leave[exitAxis], m_exit);

  // This cell, and every neighbor the ray passes near while it crosses this one.
  std::array<AxisNeighbors, 3> neighbors{};
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    const auto cellMin = static_cast<double>(m_gridOrigin[axis] + m_cell[axis]);
    neighbors[axis] =
      NearNeighbors(m_origin[axis] + m_direction[axis] * m_entered - cellMin, m_origin[axis] + m_direction[axis] * left - cellMin);
  }
  _step.count = 0;
  for (std::size_t i = 0; i < neighbors[0].count; ++i)
  {
    for (std::size_t j = 0; j < neighbors[1].count; ++j)
    {
      for (std::size_t k = 0; k < neighbors[2].count; ++k)
      {
        const Int3 candidate{m_gridOrigin[0] + m_cell[0] + neighbors[0].offsets[i], m_gridOrigin[1] + m_cell[1] + neighbors[1].offsets[j],
                             m_gridOrigin[2] + m_cell[2] + neighbors[2].offsets[k]};
        const std::uint32_t value = m_grid.VoxelAt(candidate);
        if (value != NO_VOXEL)
        {
          _step.cells[_step.count] = candidate;
          _step.values[_step.count] = value;
          ++_step.count;
        }
      }
    }
  }
  _step.leaveDistance = left;

  if (left >= m_exit)
  {
    m_done = true;
  }
  else
  {
    m_cell[exitAxis] += m_direction[exitAxis] > 0.0 ? 1 : -1;
    m_entered = left;
    m_leave[exitAxis] = NextFace(exitAxis);
  }
  return true;
}

double GridWalk::SettleDistance() const noexcept
{
  return SETTLE_CELLS / m_directionLength;
}

double GridWalk::NextFace(std::size_t _axis) const noexcept
{
  if (m_direction[_axis] == 0.0)
  {
    return std::numeric_limits<double>::infinity();
  }
  const std::int32_t face = m_gridOrigin[_axis] + m_cell[_axis] + (m_direction[_axis] > 0.0 ? 1 : 0);
  return (static_cast<double>(face) - m_origin[_axis]) / m_direction[_axis];
}

} // namespace NeuronCore
