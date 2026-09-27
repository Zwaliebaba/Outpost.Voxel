#pragma once

// Majercik et al. 2018, Listing 5, in HLSL (Design/SampleRenderer.md §9.4): IntersectBox and the Box it takes. The C++
// twin is NeuronCore/Box.h (R15), line for line. ORIENTED and CAN_START_IN_BOX are 0/1 switches standing in for the
// paper's `const bool` arguments; the entry-point file sets them.

#ifndef ORIENTED
#   error "the entry-point file sets ORIENTED"
#endif
#ifndef CAN_START_IN_BOX
#   error "the entry-point file sets CAN_START_IN_BOX"
#endif

struct Box
{
  float3 center;
  float3 radius;    // half-extents along axisX, axisY and axisZ
  float3 invRadius; // 1 / radius; read only when CAN_START_IN_BOX
  float3 axisX;     // the box's axes in world space: the columns of the paper's box.rot
  float3 axisY;
  float3 axisZ;
};

Box MakeOrientedBox(float3 _center, float3 _radius, float3 _axisX, float3 _axisY, float3 _axisZ)
{
  Box box;
  box.center = _center;
  box.radius = _radius;
  box.invRadius = 1.0 / _radius;
  box.axisX = _axisX;
  box.axisY = _axisY;
  box.axisZ = _axisZ;
  return box;
}

Box MakeAxisAlignedBox(float3 _center, float3 _radius)
{
  return MakeOrientedBox(_center, _radius, float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0));
}

float MaxComponent(float3 _value)
{
  return max(max(_value.x, _value.y), _value.z);
}

// _invDirection is read only when ORIENTED is 0. On a hit, _distance is in units of |_direction| and _normal faces back
// along the ray. With CAN_START_IN_BOX 0, a ray that starts inside the box reports no hit (§4.2, item 5).
bool IntersectBox(Box _box, float3 _origin, float3 _direction, float3 _invDirection, out float _distance, out float3 _normal)
{
  float3 origin = _origin - _box.center;
  float3 direction = _direction;
#if ORIENTED
  // World to box: the transpose of box.rot, spelled out rather than written as GLSL's v * M.
  origin = float3(dot(origin, _box.axisX), dot(origin, _box.axisY), dot(origin, _box.axisZ));
  direction = float3(dot(direction, _box.axisX), dot(direction, _box.axisY), dot(direction, _box.axisZ));
#endif

#if CAN_START_IN_BOX
  float winding = (MaxComponent(abs(origin) * _box.invRadius) < 1.0) ? -1.0 : 1.0;
#else
  float winding = 1.0;
#endif

  // HLSL's sign() returns int3; GLSL's returns a float vector.
  float3 sgn = -float3(sign(direction));

  // Distance to the three candidate front faces. A zero direction component divides by zero here, and the tests below
  // then rely on IEEE infinities and NaN comparisons (§4.2, item 6).
  float3 d = _box.radius * winding * sgn - origin;
#if ORIENTED
  d /= direction;
#else
  d *= _invDirection;
#endif

  // Is each candidate hit in front of the origin and on its face? The face includes its edges, so that a ray on the seam
  // between two voxels hits both (§4.2, item 12); the paper's test is strict.
  bool hitX = (d.x >= 0.0) && all(abs(origin.yz + direction.yz * d.x) <= _box.radius.yz);
  bool hitY = (d.y >= 0.0) && all(abs(origin.zx + direction.zx * d.y) <= _box.radius.zx);
  bool hitZ = (d.z >= 0.0) && all(abs(origin.xy + direction.xy * d.z) <= _box.radius.xy);

  // Keep exactly one axis, carrying the sign of the face normal.
  sgn = hitX ? float3(sgn.x, 0.0, 0.0) : (hitY ? float3(0.0, sgn.y, 0.0) : float3(0.0, 0.0, hitZ ? sgn.z : 0.0));

  _distance = (sgn.x != 0.0) ? d.x : ((sgn.y != 0.0) ? d.y : d.z);
#if ORIENTED
  _normal = _box.axisX * sgn.x + _box.axisY * sgn.y + _box.axisZ * sgn.z;
#else
  _normal = sgn;
#endif
  return any(sgn != 0.0);
}
