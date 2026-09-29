#include "pch.h"

#include "Composite.h"

#include "Explosion.h"
#include "Quaternion.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace NeuronCore
{
namespace
{

[[nodiscard]] Float3 ToFloat3(Int3 _value) noexcept
{
  return {static_cast<float>(_value.x), static_cast<float>(_value.y), static_cast<float>(_value.z)};
}

// _value, a point of a cube rotation's image of whole cells, as the whole cells it is.
[[nodiscard]] Int3 ToInt3(Float3 _value) noexcept
{
  return {static_cast<std::int32_t>(_value.x), static_cast<std::int32_t>(_value.y), static_cast<std::int32_t>(_value.z)};
}

} // namespace

RigidTransform ComponentTransform(const CompositeComponent& _component) noexcept
{
  return {RotationOf(_component.rotation), ToFloat3(_component.translation)};
}

bool IsIdentityComponent(const CompositeComponent& _component) noexcept
{
  const Int3 t = _component.translation;
  return t.x == 0 && t.y == 0 && t.z == 0 && IsIdentityRotation(RotationOf(_component.rotation));
}

std::optional<VoxelBounds> CompositeBounds(std::span<const VoxModel> _models, const CompositeModel& _composite)
{
  std::optional<VoxelBounds> bounds;
  for (const CompositeComponent& component : _composite.components)
  {
    const std::optional<VoxelBounds> model = OccupiedBounds(_models[component.model]);
    if (!model)
    {
      continue;
    }
    // The box's corners, turned by a symmetry of the cube and moved by whole voxels, are whole again, and exact.
    const RigidTransform transform = ComponentTransform(component);
    const std::array<Int3, 2> ends{model->lower, model->upper};
    for (std::uint32_t corner = 0; corner < 8; ++corner)
    {
      const Int3 point{ends[corner & 1u].x, ends[(corner >> 1u) & 1u].y, ends[(corner >> 2u) & 1u].z};
      const Int3 placed = ToInt3(TransformPoint(transform, ToFloat3(point)));
      if (!bounds)
      {
        bounds = VoxelBounds{placed, placed};
        continue;
      }
      bounds->lower = {std::min(bounds->lower.x, placed.x), std::min(bounds->lower.y, placed.y), std::min(bounds->lower.z, placed.z)};
      bounds->upper = {std::max(bounds->upper.x, placed.x), std::max(bounds->upper.y, placed.y), std::max(bounds->upper.z, placed.z)};
    }
  }
  return bounds;
}

Float3 CompositeCentroid(std::span<const VoxModel> _models, const CompositeModel& _composite)
{
  if (_composite.components.size() == 1 && IsIdentityComponent(_composite.components.front()))
  {
    return VoxelCentroid(_models[_composite.components.front().model]);
  }
  // Each model's centroid, placed and weighted by its voxels, in double, as VoxelCentroid sums.
  std::array<double, 3> sum{};
  std::uint64_t count = 0;
  for (const CompositeComponent& component : _composite.components)
  {
    const VoxModel& model = _models[component.model];
    std::uint64_t voxels = 0;
    for (const ModelInstance& instance : model.instances)
    {
      voxels += instance.recordCount;
    }
    if (voxels == 0)
    {
      continue;
    }
    const Float3 centroid = TransformPoint(ComponentTransform(component), VoxelCentroid(model));
    const auto weight = static_cast<double>(voxels);
    sum[0] += weight * centroid.x;
    sum[1] += weight * centroid.y;
    sum[2] += weight * centroid.z;
    count += voxels;
  }
  if (count == 0)
  {
    return {0.0f, 0.0f, 0.0f};
  }
  const double scale = 1.0 / static_cast<double>(count);
  return {static_cast<float>(sum[0] * scale), static_cast<float>(sum[1] * scale), static_cast<float>(sum[2] * scale)};
}

std::vector<CompositeModel> SingleModelComposites(std::size_t _modelCount)
{
  std::vector<CompositeModel> composites;
  composites.reserve(_modelCount);
  for (std::size_t model = 0; model < _modelCount; ++model)
  {
    composites.push_back({{{static_cast<std::uint16_t>(model), {0, 0, 0}, {0.0f, 0.0f, 0.0f, 1.0f}}}});
  }
  return composites;
}

} // namespace NeuronCore
