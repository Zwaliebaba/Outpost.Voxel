#include "pch.h"

#include "OctahedralNormal.h"

#include <algorithm>
#include <cmath>

namespace NeuronCore
{
namespace
{

constexpr float SNORM16_SCALE = 32767.0f;

[[nodiscard]] float SignNotZero(float _value) noexcept
{
  return _value >= 0.0f ? 1.0f : -1.0f;
}

// std::nearbyint rounds half to even under the default rounding mode, as HLSL's round does.
[[nodiscard]] std::uint32_t PackSnorm16(float _value) noexcept
{
  const float scaled = std::nearbyint(std::clamp(_value, -1.0f, 1.0f) * SNORM16_SCALE);
  return static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled)));
}

[[nodiscard]] float UnpackSnorm16(std::uint32_t _bits) noexcept
{
  const auto value = static_cast<std::int16_t>(static_cast<std::uint16_t>(_bits & 0xFFFFu));
  return std::max(static_cast<float>(value) / SNORM16_SCALE, -1.0f);
}

} // namespace

std::uint32_t PackOctahedralNormal(Float3 _normal) noexcept
{
  const float manhattan = std::abs(_normal.x) + std::abs(_normal.y) + std::abs(_normal.z);
  float x = _normal.x / manhattan;
  float y = _normal.y / manhattan;
  if (_normal.z < 0.0f)
  {
    const float foldedX = (1.0f - std::abs(y)) * SignNotZero(x);
    const float foldedY = (1.0f - std::abs(x)) * SignNotZero(y);
    x = foldedX;
    y = foldedY;
  }
  return PackSnorm16(x) | (PackSnorm16(y) << 16u);
}

Float3 UnpackOctahedralNormal(std::uint32_t _packed) noexcept
{
  Float3 normal{UnpackSnorm16(_packed), UnpackSnorm16(_packed >> 16u), 0.0f};
  normal.z = 1.0f - std::abs(normal.x) - std::abs(normal.y);
  const float fold = std::max(-normal.z, 0.0f);
  normal.x += normal.x >= 0.0f ? -fold : fold;
  normal.y += normal.y >= 0.0f ? -fold : fold;
  return Normalize(normal);
}

} // namespace NeuronCore
