#pragma once

#include "Float3.h"

namespace VoxelCore
{

// A ray as the views make it. The direction is not normalized: a perspective ray has a view-depth component of exactly
// one, so that the parameter of a hit is its view depth (Design/SampleRenderer.md §9.3).
struct Ray
{
  Float3 origin;
  Float3 direction;
};

// The reciprocal of the direction, as the axis-aligned form of IntersectBox takes it. A zero component becomes an
// infinity of the same sign, which the intersection relies on.
[[nodiscard]] constexpr Float3 InverseDirection(const Ray& _ray) noexcept
{
  return {1.0f / _ray.direction.x, 1.0f / _ray.direction.y, 1.0f / _ray.direction.z};
}

} // namespace VoxelCore
