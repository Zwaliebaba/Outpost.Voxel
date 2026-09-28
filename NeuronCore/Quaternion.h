#pragma once

#include "RigidTransform.h"

namespace NeuronCore
{

// A rotation as an entity and NVF store one (Design/SpaceScene.md §5.1, §6.2): x, y and z are the axis times
// sin(θ / 2), and w is cos(θ / 2). An entity's rotation turns its model into the world.
struct Quaternion
{
  float x;
  float y;
  float z;
  float w;
};

// The rotation _quaternion describes, as the columns of its matrix. The products are scaled by 2 / |q|² rather than by
// 2, so that a quaternion a rounding away from unit length still gives a rotation rather than a slight scaling. The
// quaternion of each of the cube's 24 rotations, rounded to float, gives that rotation exactly, so a station turned by
// quarter turns draws aligned (§5.1, §7.2); RigidTransformTests holds it to that. A zero quaternion describes no
// rotation and gives NaNs.
[[nodiscard]] constexpr Rotation RotationOf(Quaternion _quaternion) noexcept
{
  const float x = _quaternion.x;
  const float y = _quaternion.y;
  const float z = _quaternion.z;
  const float w = _quaternion.w;
  const float scale = 2.0f / (x * x + y * y + z * z + w * w);
  const float xx = scale * x * x;
  const float yy = scale * y * y;
  const float zz = scale * z * z;
  const float xy = scale * x * y;
  const float xz = scale * x * z;
  const float yz = scale * y * z;
  const float wx = scale * w * x;
  const float wy = scale * w * y;
  const float wz = scale * w * z;
  return {{1.0f - (yy + zz), xy + wz, xz - wy}, {xy - wz, 1.0f - (xx + zz), yz + wx}, {xz + wy, yz - wx, 1.0f - (xx + yy)}};
}

// How far from unit length a stored rotation may be: NVF's tolerance (Design/NeuronVoxelFormat.md), which the protocol
// takes over for an entity's rotation (Design/SpaceScene.md §6.2).
inline constexpr float UNIT_ROTATION_TOLERANCE = 1.0e-4f;

// The quaternion of _rotation, which must be a rotation: Shepperd's method, from the largest of the four diagonal sums,
// normalized and with w >= 0, as NVF and the protocol store one. RotationOf turns it back into _rotation to rounding.
[[nodiscard]] Quaternion QuaternionOf(const Rotation& _rotation) noexcept;

// Whether _quaternion is a rotation as NVF and the protocol store one: of unit length within UNIT_ROTATION_TOLERANCE,
// and with w >= 0, so that each rotation has one spelling.
[[nodiscard]] bool IsUnitRotation(Quaternion _quaternion) noexcept;

} // namespace NeuronCore
