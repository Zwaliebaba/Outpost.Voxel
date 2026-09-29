#pragma once

#include "Float3.h"

namespace NeuronCore
{

// A rotation as the images of the three unit axes: the columns of its matrix, as a Box holds its own axes
// (Design/Archive/SpaceScene.md §7.2).
struct Rotation
{
  Float3 axisX;
  Float3 axisY;
  Float3 axisZ;
};

inline constexpr Rotation IDENTITY_ROTATION{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

// A rotation, then a translation: how a placement puts its part into the world (§7.2). It neither scales nor shears.
struct RigidTransform
{
  Rotation rotation;
  Float3 translation;
};

// _vector turned by _rotation: axisX x + axisY y + axisZ z, summed in that order, as the shaders sum it. The twin of
// RotateVector in Shader/Placement.hlsli (R15).
[[nodiscard]] constexpr Float3 RotateVector(const Rotation& _rotation, Float3 _vector) noexcept
{
  return _rotation.axisX * _vector.x + _rotation.axisY * _vector.y + _rotation.axisZ * _vector.z;
}

// _point taken into the world: the translation plus the turned point. The twin of TransformPoint in
// Shader/Placement.hlsli (R15).
[[nodiscard]] constexpr Float3 TransformPoint(const RigidTransform& _transform, Float3 _point) noexcept
{
  return _transform.translation + RotateVector(_transform.rotation, _point);
}

// _vector in the rotation's own axes: turned back, by the transpose.
[[nodiscard]] constexpr Float3 UnrotateVector(const Rotation& _rotation, Float3 _vector) noexcept
{
  return {Dot(_rotation.axisX, _vector), Dot(_rotation.axisY, _vector), Dot(_rotation.axisZ, _vector)};
}

// _point taken from the world back into the space _transform places: how a detonation's blast origin and inherited
// velocity reach a placement's part (§7.7).
[[nodiscard]] constexpr Float3 InverseTransformPoint(const RigidTransform& _transform, Float3 _point) noexcept
{
  return UnrotateVector(_transform.rotation, _point - _transform.translation);
}

// Whether _rotation is exactly the identity.
[[nodiscard]] constexpr bool IsIdentityRotation(const Rotation& _rotation) noexcept
{
  const auto isAxis = [](Float3 _axis, Float3 _expected)
  { return _axis.x == _expected.x && _axis.y == _expected.y && _axis.z == _expected.z; };
  return isAxis(_rotation.axisX, IDENTITY_ROTATION.axisX) && isAxis(_rotation.axisY, IDENTITY_ROTATION.axisY) &&
         isAxis(_rotation.axisZ, IDENTITY_ROTATION.axisZ);
}

// Whether _rotation is one of the cube's 24 proper symmetries: every entry exactly 0 or ±1, and a rotation rather than a
// reflection. A whole placement with one draws with the aligned splat, and every product in its voxels' centers is
// exact (§7.2). A rotation a rounding away from one is not one.
[[nodiscard]] bool IsCubeSymmetry(const Rotation& _rotation) noexcept;

} // namespace NeuronCore
