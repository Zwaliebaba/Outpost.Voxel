#pragma once

// A ray as the views make it, and the reciprocal of its direction (Design/SampleRenderer.md §9.3). The C++ twin is
// NeuronCore/Ray.h (R15).

struct Ray
{
  float3 origin;
  float3 direction; // not normalized: a perspective ray has a view-depth component of exactly one
};

// A zero component becomes an infinity of the same sign, which IntersectBox relies on.
float3 InverseDirection(Ray _ray)
{
  return float3(1.0, 1.0, 1.0) / _ray.direction;
}
