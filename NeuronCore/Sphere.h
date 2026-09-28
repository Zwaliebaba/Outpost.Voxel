#pragma once

#include "Float3.h"

namespace NeuronCore
{

// A sphere around what a placement draws, which each view culls it by (Design/SpaceScene.md §7.4).
struct Sphere
{
  Float3 center;
  float radius;
};

// How far beyond a view's bounds a sphere may lie and still be kept, in voxels. Culling computes in single precision,
// and the margin keeps a sphere that touches a view whatever the rounding does (§15).
inline constexpr float CULL_MARGIN = 1.0f;

} // namespace NeuronCore
