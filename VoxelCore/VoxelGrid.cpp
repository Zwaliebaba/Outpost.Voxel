#include "pch.h"

#include "VoxelGrid.h"

#include "Box.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace VoxelCore
{
namespace
{

// How near, in cells, the ray must pass to a cell for the walk to test it. IntersectBox computes in single precision,
// and within a few thousand units of the origin its error is a small fraction of this: ulp(4096) is 1/2048 of a cell.
constexpr double NEAR_CELLS = 1.0 / 64.0;

// How far past the best hit so far, in cells, the walk goes on before it trusts that hit.
constexpr double SETTLE_CELLS = 2.0;

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
  const auto position = [&_model](const ModelInstance& _instance, std::uint32_t _index)
  {
    const VoxelRecord voxel = UnpackVoxelRecord(_model.records[_instance.firstRecord + _index]);
    return _instance.origin + Int3{voxel.x, voxel.y, voxel.z};
  };

  constexpr std::int32_t LOWEST = std::numeric_limits<std::int32_t>::lowest();
  constexpr std::int32_t HIGHEST = std::numeric_limits<std::int32_t>::max();
  Int3 lower{HIGHEST, HIGHEST, HIGHEST};
  Int3 upper{LOWEST, LOWEST, LOWEST};
  for (const ModelInstance& instance : _model.instances)
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
  for (const ModelInstance& instance : _model.instances)
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
  const std::array<double, 3> origin{_ray.origin.x, _ray.origin.y, _ray.origin.z};
  const std::array<double, 3> direction{_ray.direction.x, _ray.direction.y, _ray.direction.z};
  const std::array<std::int32_t, 3> gridOrigin{m_origin.x, m_origin.y, m_origin.z};
  const std::array<std::int32_t, 3> gridSize{m_size.x, m_size.y, m_size.z};
  const double directionLength = std::hypot(direction[0], direction[1], direction[2]);
  const bool finite = std::isfinite(origin[0]) && std::isfinite(origin[1]) && std::isfinite(origin[2]) && std::isfinite(directionLength);
  if (m_cells.empty() || !finite || !(directionLength > 0.0))
  {
    return best;
  }

  // The stretch of the ray, at or beyond _minDistance, that passes within NEAR_CELLS of the grid.
  double tEnter = std::max(0.0, static_cast<double>(_minDistance));
  double tExit = std::numeric_limits<double>::infinity();
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    const double lower = static_cast<double>(gridOrigin[axis]) - NEAR_CELLS;
    const double upper = static_cast<double>(gridOrigin[axis]) + static_cast<double>(gridSize[axis]) + NEAR_CELLS;
    if (direction[axis] == 0.0)
    {
      if (origin[axis] < lower || origin[axis] > upper)
      {
        return best;
      }
      continue;
    }
    const double first = (lower - origin[axis]) / direction[axis];
    const double second = (upper - origin[axis]) / direction[axis];
    tEnter = std::max(tEnter, std::min(first, second));
    tExit = std::min(tExit, std::max(first, second));
  }
  if (tEnter > tExit)
  {
    return best;
  }

  // The cell the walk starts in, which may lie just outside the grid, and the ray parameter at which the ray leaves it
  // along each axis.
  std::array<std::int32_t, 3> cell{};
  std::array<double, 3> leave{};
  const auto nextFace = [&](std::size_t _axis)
  {
    if (direction[_axis] == 0.0)
    {
      return std::numeric_limits<double>::infinity();
    }
    const std::int32_t face = gridOrigin[_axis] + cell[_axis] + (direction[_axis] > 0.0 ? 1 : 0);
    return (static_cast<double>(face) - origin[_axis]) / direction[_axis];
  };
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    const double position = origin[axis] + direction[axis] * tEnter - static_cast<double>(gridOrigin[axis]);
    cell[axis] = static_cast<std::int32_t>(std::clamp(std::floor(position), -1.0, static_cast<double>(gridSize[axis])));
    leave[axis] = nextFace(axis);
  }

  const Float3 invDirection = InverseDirection(_ray);
  double entered = tEnter;
  while (true)
  {
    std::size_t exitAxis = 0;
    if (leave[1] < leave[exitAxis])
    {
      exitAxis = 1;
    }
    if (leave[2] < leave[exitAxis])
    {
      exitAxis = 2;
    }
    const double left = std::min(leave[exitAxis], tExit);

    // This cell, and every neighbor the ray passes near while it crosses this one.
    std::array<AxisNeighbors, 3> neighbors{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      const auto cellMin = static_cast<double>(gridOrigin[axis] + cell[axis]);
      neighbors[axis] = NearNeighbors(origin[axis] + direction[axis] * entered - cellMin, origin[axis] + direction[axis] * left - cellMin);
    }
    for (std::size_t i = 0; i < neighbors[0].count; ++i)
    {
      for (std::size_t j = 0; j < neighbors[1].count; ++j)
      {
        for (std::size_t k = 0; k < neighbors[2].count; ++k)
        {
          const Int3 candidate{cell[0] + neighbors[0].offsets[i], cell[1] + neighbors[1].offsets[j], cell[2] + neighbors[2].offsets[k]};
          const std::uint32_t voxel = CellAt(candidate.x, candidate.y, candidate.z);
          if (voxel == NO_VOXEL)
          {
            continue;
          }
          float distance = 0.0f;
          Float3 normal{};
          if (IntersectBox<false, false>(CellBox(m_origin + candidate), _ray.origin, _ray.direction, invDirection, distance, normal) &&
              distance >= _minDistance && (distance < best.distance || (distance == best.distance && voxel < best.voxel)))
          {
            best = {voxel, distance, normal};
          }
        }
      }
    }

    const bool settled = best.voxel != NO_VOXEL && left > static_cast<double>(best.distance) + SETTLE_CELLS / directionLength;
    if (settled || left >= tExit)
    {
      return best;
    }
    cell[exitAxis] += direction[exitAxis] > 0.0 ? 1 : -1;
    entered = left;
    leave[exitAxis] = nextFace(exitAxis);
  }
}

} // namespace VoxelCore
