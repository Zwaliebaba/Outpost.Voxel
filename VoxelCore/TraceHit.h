#pragma once

#include "Box.h"
#include "Float3.h"
#include "Ray.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace VoxelCore
{

// The visibility buffer's value for a pixel that no voxel covers (Design/SampleRenderer.md §7.3).
inline constexpr std::uint32_t NO_VOXEL = 0xFFFFFFFFu;

// The voxel a ray resolves to, as the view and shadow passes resolve it.
struct TraceHit
{
  std::uint32_t voxel; // an index into the record buffer, or NO_VOXEL
  float distance;      // the ray parameter of the hit, in units of |direction|
  Float3 normal;       // the face normal, facing back along the ray
};

// The brute-force reference (Design/SampleRenderer.md §14): IntersectBox against every box, keeping the nearest hit at
// or beyond _minDistance, as a splat pass's discard and depth test do. A tie goes to the lower index, which is the box
// the pass draws first.
template <bool Oriented> [[nodiscard]] TraceHit TraceBoxes(std::span<const Box> _boxes, const Ray& _ray, float _minDistance) noexcept
{
  TraceHit nearest{NO_VOXEL, std::numeric_limits<float>::infinity(), {0.0f, 0.0f, 0.0f}};
  const Float3 invDirection = InverseDirection(_ray);
  for (std::size_t i = 0; i < _boxes.size(); ++i)
  {
    float distance = 0.0f;
    Float3 normal{};
    if (IntersectBox<Oriented, false>(_boxes[i], _ray.origin, _ray.direction, invDirection, distance, normal) && distance >= _minDistance &&
        distance < nearest.distance)
    {
      nearest = {static_cast<std::uint32_t>(i), distance, normal};
    }
  }
  return nearest;
}

} // namespace VoxelCore
