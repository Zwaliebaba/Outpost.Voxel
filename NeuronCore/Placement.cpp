#include "pch.h"

#include "Placement.h"

#include "VoxelRecord.h"

#include <algorithm>
#include <cstdint>

namespace NeuronCore
{
namespace
{

// A voxel's half-extents in its own space: it is a unit cube.
constexpr Float3 VOXEL_HALF_EXTENT{0.5f, 0.5f, 0.5f};

} // namespace

std::vector<std::uint32_t> SceneRecords(std::span<const VoxModel> _models)
{
  std::vector<std::uint32_t> records;
  for (const VoxModel& model : _models)
  {
    records.insert(records.end(), model.records.begin(), model.records.end());
  }
  return records;
}

std::vector<std::uint32_t> ModelFirstRecords(std::span<const VoxModel> _models)
{
  std::vector<std::uint32_t> firstRecords;
  std::uint32_t next = 0;
  for (const VoxModel& model : _models)
  {
    firstRecords.push_back(next);
    next += static_cast<std::uint32_t>(model.records.size());
  }
  return firstRecords;
}

Placement PlacePart(const VoxModel& _model, std::uint32_t _modelIndex, std::uint32_t _modelFirstRecord, std::uint32_t _part,
                    const RigidTransform& _transform) noexcept
{
  const ModelInstance& part = _model.instances[_part];
  // A record's coordinates run from 0 to 255, so the box starts beyond them and closes in. A part with no voxels gets
  // an empty box at its origin.
  Int3 lower{256, 256, 256};
  Int3 upper{0, 0, 0};
  for (std::uint32_t i = 0; i < part.recordCount; ++i)
  {
    const VoxelRecord voxel = UnpackVoxelRecord(_model.records[part.firstRecord + i]);
    lower = {std::min<std::int32_t>(lower.x, voxel.x), std::min<std::int32_t>(lower.y, voxel.y), std::min<std::int32_t>(lower.z, voxel.z)};
    upper = {std::max<std::int32_t>(upper.x, voxel.x + 1), std::max<std::int32_t>(upper.y, voxel.y + 1),
             std::max<std::int32_t>(upper.z, voxel.z + 1)};
  }
  if (part.recordCount == 0u)
  {
    lower = upper;
  }
  return {.transform = _transform,
          .lower = {static_cast<float>(lower.x), static_cast<float>(lower.y), static_cast<float>(lower.z)},
          .upper = {static_cast<float>(upper.x), static_cast<float>(upper.y), static_cast<float>(upper.z)},
          .firstRecord = _modelFirstRecord + part.firstRecord,
          .recordCount = part.recordCount,
          .paletteIndex = _modelIndex,
          .firstVoxel = 0u,
          .detonation = std::nullopt};
}

bool AssignVoxelIds(std::span<Placement> _placements) noexcept
{
  // The last id, one less than the total, must lie below NO_VOXEL.
  std::uint64_t total = 0;
  for (const Placement& placement : _placements)
  {
    total += placement.recordCount;
  }
  if (total > NO_VOXEL)
  {
    return false;
  }
  std::uint32_t next = 0;
  for (Placement& placement : _placements)
  {
    placement.firstVoxel = next;
    next += placement.recordCount;
  }
  return true;
}

bool IsAlignedPlacement(const Placement& _placement) noexcept
{
  return !_placement.detonation.has_value() && IsCubeSymmetry(_placement.transform.rotation);
}

Box PlacedVoxelBox(const Placement& _placement, std::uint32_t _voxel, std::uint32_t _record) noexcept
{
  // §7.7: the voxel in the part's own space, at rest or posed, and then the placement's transform. Whole or detonated,
  // the center goes through the one TransformPoint below, so that a compiler that fuses a multiply and an add cannot
  // round the two differently: at time 0 the pose is exactly the rest, and the boxes are exactly the whole placement's.
  const VoxelRecord record = UnpackVoxelRecord(_record);
  const Float3 restCenter{static_cast<float>(record.x) + 0.5f, static_cast<float>(record.y) + 0.5f, static_cast<float>(record.z) + 0.5f};
  VoxelPose pose{restCenter, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  if (_placement.detonation.has_value() && _placement.detonation->timeSeconds > 0.0f)
  {
    const PlacementDetonation& detonation = *_placement.detonation;
    const std::uint32_t fragment = detonation.fragments.fragmentOf[_voxel];
    pose = ExplosionPose(fragment, detonation.fragments.fragments[fragment], restCenter, detonation.parameters, detonation.timeSeconds);
  }
  const Float3 center = TransformPoint(_placement.transform, pose.center);
  const Rotation& rotation = _placement.transform.rotation;
  if (IsAlignedPlacement(_placement))
  {
    return MakeAxisAlignedBox(center, VOXEL_HALF_EXTENT);
  }
  return MakeOrientedBox(center, VOXEL_HALF_EXTENT, RotateVector(rotation, pose.axisX), RotateVector(rotation, pose.axisY),
                         RotateVector(rotation, pose.axisZ));
}

ExplosionEnvelope PlacementEnvelope(const Placement& _placement, const PlacementDetonation& _detonation) noexcept
{
  return BoundExplosion(_detonation.parameters, _placement.lower, _placement.upper, _detonation.fragments.radius);
}

PlacementHeat MakePlacementHeat(const Placement& _placement) noexcept
{
  if (!_placement.detonation.has_value())
  {
    return {};
  }
  const PlacementDetonation& detonation = *_placement.detonation;
  return {.blastOrigin = detonation.parameters.blastOrigin,
          .timeSeconds = detonation.timeSeconds,
          .shockSpeed = detonation.parameters.shockSpeed,
          .heatDistance = detonation.heat.heatDistance,
          .coolingRate = detonation.heat.coolingRate,
          .firstFragment = detonation.fragments.firstFragment};
}

Sphere PlacementSphere(const Placement& _placement) noexcept
{
  if (_placement.detonation.has_value())
  {
    const PlacementDetonation& detonation = *_placement.detonation;
    const ExplosionEnvelope envelope = PlacementEnvelope(_placement, detonation);
    const Sphere local = EnvelopeSphereAt(envelope, detonation.parameters, detonation.timeSeconds);
    return {TransformPoint(_placement.transform, local.center), local.radius};
  }
  const Float3 center = (_placement.lower + _placement.upper) * 0.5f;
  return {TransformPoint(_placement.transform, center), Length(_placement.upper - center)};
}

std::uint32_t FindPlacement(std::span<const Placement> _placements, std::uint32_t _voxel) noexcept
{
  const auto count = static_cast<std::uint32_t>(_placements.size());
  if (count == 0u || _voxel < _placements[0].firstVoxel)
  {
    return NO_PLACEMENT;
  }
  // The last placement whose first voxel is at most _voxel, which lies in [lower, upper).
  std::uint32_t lower = 0u;
  std::uint32_t upper = count;
  while (upper - lower > 1u)
  {
    const std::uint32_t middle = lower + (upper - lower) / 2u;
    if (_placements[middle].firstVoxel <= _voxel)
    {
      lower = middle;
    }
    else
    {
      upper = middle;
    }
  }
  return _voxel - _placements[lower].firstVoxel < _placements[lower].recordCount ? lower : NO_PLACEMENT;
}

std::optional<PlacedVoxel> FindVoxel(std::span<const Placement> _placements, std::uint32_t _voxel) noexcept
{
  const std::uint32_t found = FindPlacement(_placements, _voxel);
  if (found == NO_PLACEMENT)
  {
    return std::nullopt;
  }
  const Placement& placement = _placements[found];
  return PlacedVoxel{placement.firstRecord + (_voxel - placement.firstVoxel), placement.paletteIndex};
}

bool IsVoxelGone(const Placement& _placement, std::uint32_t _voxel) noexcept
{
  const std::size_t word = _voxel / 32u;
  return word < _placement.mask.size() && ((_placement.mask[word] >> (_voxel % 32u)) & 1u) != 0u;
}

std::vector<std::uint32_t> PlacementMask(std::span<const std::uint8_t> _gone, std::uint32_t _first, std::uint32_t _count)
{
  std::vector<std::uint32_t> mask((static_cast<std::size_t>(_count) + 31u) / 32u, 0u);
  bool anyGone = false;
  for (std::uint32_t voxel = 0; voxel < _count; ++voxel)
  {
    const std::size_t bit = static_cast<std::size_t>(_first) + voxel;
    if (bit / 8u < _gone.size() && ((_gone[bit / 8u] >> (bit % 8u)) & 1u) != 0u)
    {
      mask[voxel / 32u] |= 1u << (voxel % 32u);
      anyGone = true;
    }
  }
  if (!anyGone)
  {
    mask.clear();
  }
  return mask;
}

} // namespace NeuronCore
