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
  const auto inverse = [](float _value) noexcept
  {
    if (_value == 0.0f)
    {
      return std::copysign(std::numeric_limits<float>::infinity(), _value);
    }
    return 1.0f / _value;
  };
  return {inverse(_ray.direction.x), inverse(_ray.direction.y), inverse(_ray.direction.z)};
}

} // namespace NeuronCore
