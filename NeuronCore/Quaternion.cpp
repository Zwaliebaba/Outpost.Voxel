#include "pch.h"

#include "Quaternion.h"

#include <cmath>

namespace NeuronCore
{

Quaternion QuaternionOf(const Rotation& _rotation) noexcept
{
  // The matrix's entries, m[row][column], the columns being the rotation's axes.
  const float m00 = _rotation.axisX.x;
  const float m10 = _rotation.axisX.y;
  const float m20 = _rotation.axisX.z;
  const float m01 = _rotation.axisY.x;
  const float m11 = _rotation.axisY.y;
  const float m21 = _rotation.axisY.z;
  const float m02 = _rotation.axisZ.x;
  const float m12 = _rotation.axisZ.y;
  const float m22 = _rotation.axisZ.z;

  // Four times the largest component comes from the largest of the four sums, which keeps the square root away from
  // zero; the others follow from the off-diagonal entries.
  Quaternion quaternion{};
  const float trace = m00 + m11 + m22;
  if (trace > 0.0f)
  {
    const float scale = 2.0f * std::sqrt(1.0f + trace);
    quaternion = {(m21 - m12) / scale, (m02 - m20) / scale, (m10 - m01) / scale, 0.25f * scale};
  }
  else if (m00 > m11 && m00 > m22)
  {
    const float scale = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
    quaternion = {0.25f * scale, (m01 + m10) / scale, (m02 + m20) / scale, (m21 - m12) / scale};
  }
  else if (m11 > m22)
  {
    const float scale = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
    quaternion = {(m01 + m10) / scale, 0.25f * scale, (m12 + m21) / scale, (m02 - m20) / scale};
  }
  else
  {
    const float scale = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
    quaternion = {(m02 + m20) / scale, (m12 + m21) / scale, 0.25f * scale, (m10 - m01) / scale};
  }

  const float length =
    std::sqrt(quaternion.x * quaternion.x + quaternion.y * quaternion.y + quaternion.z * quaternion.z + quaternion.w * quaternion.w);
  const float sign = quaternion.w < 0.0f ? -1.0f : 1.0f;
  const float scale = sign / length;
  return {quaternion.x * scale, quaternion.y * scale, quaternion.z * scale, quaternion.w * scale};
}

bool IsUnitRotation(Quaternion _quaternion) noexcept
{
  const float length = std::sqrt(_quaternion.x * _quaternion.x + _quaternion.y * _quaternion.y + _quaternion.z * _quaternion.z +
                                 _quaternion.w * _quaternion.w);
  return std::abs(length - 1.0f) <= UNIT_ROTATION_TOLERANCE && _quaternion.w >= 0.0f;
}

} // namespace NeuronCore
