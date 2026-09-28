#include "pch.h"

#include "RigidTransform.h"

#include <cmath>
#include <initializer_list>

namespace NeuronCore
{
namespace
{

[[nodiscard]] bool IsUnitEntry(float _entry) noexcept
{
  return _entry == 0.0f || _entry == 1.0f || _entry == -1.0f;
}

} // namespace

bool IsCubeSymmetry(const Rotation& _rotation) noexcept
{
  // With every entry 0 or ±1, a column of unit length holds exactly one ±1. The cross product of the first two such
  // columns is then exact, and it equals the third only when the three are distinct axes turned rather than reflected.
  for (const Float3 axis : {_rotation.axisX, _rotation.axisY, _rotation.axisZ})
  {
    if (!IsUnitEntry(axis.x) || !IsUnitEntry(axis.y) || !IsUnitEntry(axis.z) ||
        std::abs(axis.x) + std::abs(axis.y) + std::abs(axis.z) != 1.0f)
    {
      return false;
    }
  }
  const Float3 handed = Cross(_rotation.axisX, _rotation.axisY);
  return handed.x == _rotation.axisZ.x && handed.y == _rotation.axisZ.y && handed.z == _rotation.axisZ.z;
}

} // namespace NeuronCore
