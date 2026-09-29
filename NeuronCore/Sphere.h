#pragma once

#include "Float3.h"

#include <algorithm>
#include <span>

namespace NeuronCore
{

// A sphere around what a placement draws, which each view culls it by (Design/Archive/SpaceScene.md §7.4).
struct Sphere
{
  Float3 center;
  float radius;
};

// How far beyond a view's bounds a sphere may lie and still be kept, in voxels. Culling computes in single precision,
// and the margin keeps a sphere that touches a view whatever the rounding does (§15).
inline constexpr float CULL_MARGIN = 1.0f;

// A sphere around every one of _spheres, about the middle of the box around them: not the smallest there is, but one
// that holds them all, to fit a view to or frame a camera on (Design/Archive/SpaceScene.md §10, §13). With none, it is the point
// at the origin.
[[nodiscard]] inline Sphere EnclosingSphere(std::span<const Sphere> _spheres) noexcept
{
  if (_spheres.empty())
  {
    return {{0.0f, 0.0f, 0.0f}, 0.0f};
  }
  Float3 lower = _spheres.front().center;
  Float3 upper = lower;
  for (const Sphere& sphere : _spheres)
  {
    lower = {std::min(lower.x, sphere.center.x - sphere.radius), std::min(lower.y, sphere.center.y - sphere.radius),
             std::min(lower.z, sphere.center.z - sphere.radius)};
    upper = {std::max(upper.x, sphere.center.x + sphere.radius), std::max(upper.y, sphere.center.y + sphere.radius),
             std::max(upper.z, sphere.center.z + sphere.radius)};
  }
  const Float3 center = (lower + upper) * 0.5f;
  float radius = 0.0f;
  for (const Sphere& sphere : _spheres)
  {
    radius = std::max(radius, Length(sphere.center - center) + sphere.radius);
  }
  return {center, radius};
}

} // namespace NeuronCore
