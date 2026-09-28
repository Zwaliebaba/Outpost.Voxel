#pragma once

#include "Float3.h"
#include <cmath>
#include <limits>

namespace NeuronCore
{

// A ray as the views make it. The direction is not normalized: a perspective ray has a view-depth component of exactly
// one, so that the parameter of a hit is its view depth (Design/Archive/SampleRenderer.md §9.3).
struct Ray
{
  Float3 origin;
  Float3 direction;
};

// The reciprocal of the direction, as the axis-aligned form of IntersectBox takes it. A zero component becomes an
// infinity of the same sign, which the intersection relies on.
[[nodiscard]] inline Float3 InverseDirection(const Ray& _ray) noexcept
{
  // Avoid floating-point division by zero to prevent compiler warnings. A zero component becomes an
  // infinity of the same sign, which the intersection logic relies on.
  auto inv = [&](float v) noexcept {
    if (v == 0.0f)
    {
      return std::copysign(std::numeric_limits<float>::infinity(), v);
    }
    return 1.0f / v;
  };
  return {inv(_ray.direction.x), inv(_ray.direction.y), inv(_ray.direction.z)};
}

} // namespace NeuronCore
