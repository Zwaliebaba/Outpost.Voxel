#pragma once

#include "Float3.h"

#include <cmath>

namespace NeuronCore
{

// A box as the splat pass draws one: an intact voxel is axis-aligned, an exploding one is not (Design/Archive/SampleRenderer.md
// §9.4).
struct Box
{
  Float3 center;
  Float3 radius;    // half-extents along axisX, axisY and axisZ
  Float3 invRadius; // 1 / radius; read only when CanStartInBox
  Float3 axisX;     // the box's axes in world space: the columns of the paper's box.rot
  Float3 axisY;
  Float3 axisZ;
};

[[nodiscard]] constexpr Box MakeOrientedBox(Float3 _center, Float3 _radius, Float3 _axisX, Float3 _axisY, Float3 _axisZ) noexcept
{
  return {_center, _radius, {1.0f / _radius.x, 1.0f / _radius.y, 1.0f / _radius.z}, _axisX, _axisY, _axisZ};
}

[[nodiscard]] constexpr Box MakeAxisAlignedBox(Float3 _center, Float3 _radius) noexcept
{
  return MakeOrientedBox(_center, _radius, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
}

// Majercik et al. 2018, Listing 5: the C++ twin of IntersectBox in RayBox.hlsli (R15), line for line. Oriented and
// CanStartInBox stand in for the paper's `const bool` arguments.
//
// _invDirection is read only when Oriented is false. On a hit, _distance is in units of |_direction| and _normal faces
// back along the ray. With CanStartInBox false, a ray that starts inside the box reports no hit, which is the paper's
// documented "outside" behavior and the one the splat pass relies on (§4.2, item 5).
//
// A zero direction component divides by zero below. The result is then an infinity or a NaN, and the tests that follow
// are written so that either one fails them: that is the paper's reason for needing no zero check, and the reason this
// twin is tested on exactly those rays (§4.2, item 6).
template <bool Oriented, bool CanStartInBox>
[[nodiscard]] bool IntersectBox(const Box& _box, Float3 _origin, Float3 _direction, Float3 _invDirection, float& _distance,
                                Float3& _normal) noexcept
{
  Float3 origin = _origin - _box.center;
  Float3 direction = _direction;
  if constexpr (Oriented)
  {
    // World to box: the transpose of box.rot, spelled out rather than written as GLSL's v * M.
    origin = {Dot(origin, _box.axisX), Dot(origin, _box.axisY), Dot(origin, _box.axisZ)};
    direction = {Dot(direction, _box.axisX), Dot(direction, _box.axisY), Dot(direction, _box.axisZ)};
  }

  float winding = 1.0f;
  if constexpr (CanStartInBox)
  {
    winding = MaxComponent(Abs(origin) * _box.invRadius) < 1.0f ? -1.0f : 1.0f;
  }

  Float3 sgn = -Sign(direction);

  // Distance to the three candidate front faces.
  Float3 d = _box.radius * winding * sgn - origin;
  if constexpr (Oriented)
  {
    d = d / direction;
  }
  else
  {
    d = d * _invDirection;
  }

  // Is each candidate hit in front of the origin and on its face? The face includes its edges, so that a ray on the seam
  // between two voxels hits both (§4.2, item 12); the paper's test is strict.
  const bool hitX =
    d.x >= 0.0f && std::abs(origin.y + direction.y * d.x) <= _box.radius.y && std::abs(origin.z + direction.z * d.x) <= _box.radius.z;
  const bool hitY =
    d.y >= 0.0f && std::abs(origin.z + direction.z * d.y) <= _box.radius.z && std::abs(origin.x + direction.x * d.y) <= _box.radius.x;
  const bool hitZ =
    d.z >= 0.0f && std::abs(origin.x + direction.x * d.z) <= _box.radius.x && std::abs(origin.y + direction.y * d.z) <= _box.radius.y;

  // Keep exactly one axis, carrying the sign of the face normal.
  sgn = hitX ? Float3{sgn.x, 0.0f, 0.0f} : (hitY ? Float3{0.0f, sgn.y, 0.0f} : Float3{0.0f, 0.0f, hitZ ? sgn.z : 0.0f});

  _distance = sgn.x != 0.0f ? d.x : (sgn.y != 0.0f ? d.y : d.z);
  if constexpr (Oriented)
  {
    _normal = _box.axisX * sgn.x + _box.axisY * sgn.y + _box.axisZ * sgn.z;
  }
  else
  {
    _normal = sgn;
  }
  return sgn.x != 0.0f || sgn.y != 0.0f || sgn.z != 0.0f;
}

} // namespace NeuronCore
