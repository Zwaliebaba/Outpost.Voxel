#include "pch.h"

#include "SceneTracer.h"

#include <array>
#include <limits>
#include <map>
#include <utility>

namespace NeuronCore
{
namespace
{

// _vector in a rotation's own axes: the transpose of the rotation, applied in double precision.
[[nodiscard]] std::array<double, 3> Unrotate(const Rotation& _rotation, double _x, double _y, double _z) noexcept
{
  const auto dot = [_x, _y, _z](Float3 _axis) noexcept
  { return static_cast<double>(_axis.x) * _x + static_cast<double>(_axis.y) * _y + static_cast<double>(_axis.z) * _z; };
  return {dot(_rotation.axisX), dot(_rotation.axisY), dot(_rotation.axisZ)};
}

// Keeps _distance, _normal and _voxel in _best when they are nearer, or as near with a lower id.
void KeepNearer(TraceHit& _best, std::uint32_t _voxel, float _distance, Float3 _normal) noexcept
{
  if (_distance < _best.distance || (_distance == _best.distance && _voxel < _best.voxel))
  {
    _best = {_voxel, _distance, _normal};
  }
}

} // namespace

SceneTracer::SceneTracer(std::span<const VoxModel> _models, std::span<const Placement> _placements)
  : m_records(SceneRecords(_models)),
    m_placements(_placements.begin(), _placements.end())
{
  // One grid for each part a whole placement draws, however many placements draw it.
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> grids;
  const std::span<const std::uint32_t> records(m_records);
  for (const Placement& placement : m_placements)
  {
    Traced traced{0, {}, {}};
    if (placement.detonation.has_value())
    {
      // Its posed voxels, those it still draws (Design/ADR/ADR-035), with each one's voxel within the placement.
      traced.boxes.reserve(placement.recordCount);
      traced.voxels.reserve(placement.recordCount);
      for (std::uint32_t i = 0; i < placement.recordCount; ++i)
      {
        if (!IsVoxelGone(placement, i))
        {
          traced.boxes.push_back(PlacedVoxelBox(placement, i, m_records[placement.firstRecord + i]));
          traced.voxels.push_back(i);
        }
      }
    }
    else
    {
      const std::pair<std::uint32_t, std::uint32_t> part{placement.firstRecord, placement.recordCount};
      const auto [found, added] = grids.try_emplace(part, m_grids.size());
      if (added)
      {
        m_grids.emplace_back(records.subspan(placement.firstRecord, placement.recordCount));
      }
      traced.grid = found->second;
    }
    m_traced.push_back(std::move(traced));
  }
}

TraceHit SceneTracer::Trace(const Ray& _ray, float _minDistance) const noexcept
{
  TraceHit best{NO_VOXEL, std::numeric_limits<float>::infinity(), {0.0f, 0.0f, 0.0f}};
  const Float3 invDirection = InverseDirection(_ray);
  for (std::size_t p = 0; p < m_placements.size(); ++p)
  {
    const Placement& placement = m_placements[p];
    const Traced& traced = m_traced[p];
    if (placement.detonation.has_value())
    {
      const TraceHit hit = TraceBoxes<true>(traced.boxes, _ray, _minDistance);
      if (hit.voxel != NO_VOXEL)
      {
        KeepNearer(best, placement.firstVoxel + traced.voxels[hit.voxel], hit.distance, hit.normal);
      }
      continue;
    }

    // The ray in the part's space, where the grid lies. A rigid transform keeps the ray's parameter, so distances
    // along it compare with the world's.
    const Rotation& rotation = placement.transform.rotation;
    const Float3 translation = placement.transform.translation;
    const std::array<double, 3> origin = Unrotate(rotation, static_cast<double>(_ray.origin.x) - static_cast<double>(translation.x),
                                                  static_cast<double>(_ray.origin.y) - static_cast<double>(translation.y),
                                                  static_cast<double>(_ray.origin.z) - static_cast<double>(translation.z));
    const std::array<double, 3> direction = Unrotate(rotation, _ray.direction.x, _ray.direction.y, _ray.direction.z);
    const bool aligned = IsAlignedPlacement(placement);
    GridWalk walk(m_grids[traced.grid], origin, direction, static_cast<double>(_minDistance));
    GridStep step{};
    while (walk.Next(step))
    {
      for (std::size_t i = 0; i < step.count; ++i)
      {
        const std::uint32_t local = step.values[i];
        if (IsVoxelGone(placement, local))
        {
          continue;
        }
        const Box box = PlacedVoxelBox(placement, local, m_records[placement.firstRecord + local]);
        float distance = 0.0f;
        Float3 normal{};
        const bool hit = aligned ? IntersectBox<false, false>(box, _ray.origin, _ray.direction, invDirection, distance, normal)
                                 : IntersectBox<true, false>(box, _ray.origin, _ray.direction, invDirection, distance, normal);
        if (hit && distance >= _minDistance)
        {
          KeepNearer(best, placement.firstVoxel + local, distance, normal);
        }
      }
      if (best.voxel != NO_VOXEL && step.leaveDistance > static_cast<double>(best.distance) + walk.SettleDistance())
      {
        break;
      }
    }
  }
  return best;
}

} // namespace NeuronCore
