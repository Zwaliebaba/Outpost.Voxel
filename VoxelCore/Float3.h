#pragma once

#include <cmath>
#include <cstdint>

namespace VoxelCore
{

// Scalar mirrors of HLSL's float2, float3 and float4. They are plain aggregates without SIMD, so that a C++ twin of a
// shader (R15) reads line for line like the HLSL it checks and computes in the same single precision.
struct Float2
{
  float x;
  float y;
};

struct Float3
{
  float x;
  float y;
  float z;
};

struct Float4
{
  float x;
  float y;
  float z;
  float w;
};

// Integer coordinates, in voxels.
struct Int3
{
  std::int32_t x;
  std::int32_t y;
  std::int32_t z;
};

[[nodiscard]] constexpr Float2 operator+(Float2 _a, Float2 _b) noexcept
{
  return {_a.x + _b.x, _a.y + _b.y};
}

[[nodiscard]] constexpr Float2 operator-(Float2 _a, Float2 _b) noexcept
{
  return {_a.x - _b.x, _a.y - _b.y};
}

[[nodiscard]] constexpr Int3 operator+(Int3 _a, Int3 _b) noexcept
{
  return {_a.x + _b.x, _a.y + _b.y, _a.z + _b.z};
}

[[nodiscard]] constexpr Int3 operator-(Int3 _a, Int3 _b) noexcept
{
  return {_a.x - _b.x, _a.y - _b.y, _a.z - _b.z};
}

[[nodiscard]] constexpr Float3 operator+(Float3 _a, Float3 _b) noexcept
{
  return {_a.x + _b.x, _a.y + _b.y, _a.z + _b.z};
}

[[nodiscard]] constexpr Float3 operator-(Float3 _a, Float3 _b) noexcept
{
  return {_a.x - _b.x, _a.y - _b.y, _a.z - _b.z};
}

[[nodiscard]] constexpr Float3 operator-(Float3 _a) noexcept
{
  return {-_a.x, -_a.y, -_a.z};
}

// Component-wise, as HLSL's * and / are.
[[nodiscard]] constexpr Float3 operator*(Float3 _a, Float3 _b) noexcept
{
  return {_a.x * _b.x, _a.y * _b.y, _a.z * _b.z};
}

[[nodiscard]] constexpr Float3 operator/(Float3 _a, Float3 _b) noexcept
{
  return {_a.x / _b.x, _a.y / _b.y, _a.z / _b.z};
}

[[nodiscard]] constexpr Float3 operator*(Float3 _a, float _scale) noexcept
{
  return {_a.x * _scale, _a.y * _scale, _a.z * _scale};
}

[[nodiscard]] constexpr float Dot(Float3 _a, Float3 _b) noexcept
{
  return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z;
}

[[nodiscard]] constexpr float Dot(Float4 _a, Float4 _b) noexcept
{
  return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z + _a.w * _b.w;
}

[[nodiscard]] constexpr Float3 Cross(Float3 _a, Float3 _b) noexcept
{
  return {_a.y * _b.z - _a.z * _b.y, _a.z * _b.x - _a.x * _b.z, _a.x * _b.y - _a.y * _b.x};
}

[[nodiscard]] inline float Length(Float3 _a) noexcept
{
  return std::sqrt(Dot(_a, _a));
}

// Like HLSL's normalize: a zero vector comes back as NaNs.
[[nodiscard]] inline Float3 Normalize(Float3 _a) noexcept
{
  return _a * (1.0f / Length(_a));
}

[[nodiscard]] inline Float3 Abs(Float3 _a) noexcept
{
  return {std::abs(_a.x), std::abs(_a.y), std::abs(_a.z)};
}

// HLSL's sign: -1, 0 or 1, and 0 for both zeros. HLSL returns an integer; the twins need the float.
[[nodiscard]] constexpr float Sign(float _value) noexcept
{
  return _value > 0.0f ? 1.0f : (_value < 0.0f ? -1.0f : 0.0f);
}

[[nodiscard]] constexpr Float3 Sign(Float3 _a) noexcept
{
  return {Sign(_a.x), Sign(_a.y), Sign(_a.z)};
}

[[nodiscard]] constexpr float MaxComponent(Float3 _a) noexcept
{
  return _a.x > _a.y ? (_a.x > _a.z ? _a.x : _a.z) : (_a.y > _a.z ? _a.y : _a.z);
}

[[nodiscard]] constexpr Float2 Min(Float2 _a, Float2 _b) noexcept
{
  return {_a.x < _b.x ? _a.x : _b.x, _a.y < _b.y ? _a.y : _b.y};
}

[[nodiscard]] constexpr Float2 Max(Float2 _a, Float2 _b) noexcept
{
  return {_a.x > _b.x ? _a.x : _b.x, _a.y > _b.y ? _a.y : _b.y};
}

} // namespace VoxelCore
