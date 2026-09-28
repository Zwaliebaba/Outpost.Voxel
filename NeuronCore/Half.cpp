#include "pch.h"

#include "Half.h"

#include <bit>

namespace NeuronCore
{
namespace
{

// Single-precision bit patterns: infinity; the largest half, 65504; the least normal half, 2^-14; and the least
// subnormal half, 2^-24.
constexpr std::uint32_t FLOAT_INFINITY = 0x7F800000u;
constexpr std::uint32_t HALF_LARGEST = 0x477FE000u;
constexpr std::uint32_t HALF_LEAST_NORMAL = 0x38800000u;
constexpr std::uint32_t HALF_LEAST_SUBNORMAL = 0x33800000u;

// The exponent bias between the two formats, 127 - 15, in place.
constexpr std::uint32_t REBIAS = 112u << 23u;

// The mantissa bits a half drops.
constexpr std::uint32_t DROPPED_BITS = 13u;

} // namespace

std::uint16_t FloatToHalf(float _value) noexcept
{
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(_value);
  const std::uint32_t sign = (bits >> 16u) & 0x8000u;
  const std::uint32_t magnitude = bits & 0x7FFFFFFFu;
  if (magnitude > FLOAT_INFINITY)
  {
    return static_cast<std::uint16_t>(sign | 0x7E00u);
  }
  if (magnitude == FLOAT_INFINITY)
  {
    return static_cast<std::uint16_t>(sign | 0x7C00u);
  }
  if (magnitude >= HALF_LARGEST)
  {
    return static_cast<std::uint16_t>(sign | 0x7BFFu);
  }
  if (magnitude >= HALF_LEAST_NORMAL)
  {
    // Rebiased, and the bits a half has no room for dropped.
    return static_cast<std::uint16_t>(sign | ((magnitude - REBIAS) >> DROPPED_BITS));
  }
  if (magnitude < HALF_LEAST_SUBNORMAL)
  {
    return static_cast<std::uint16_t>(sign);
  }
  // A subnormal: the mantissa, with its leading one, in whole units of 2^-24.
  const std::uint32_t exponent = magnitude >> 23u;
  const std::uint32_t mantissa = (magnitude & 0x7FFFFFu) | 0x800000u;
  return static_cast<std::uint16_t>(sign | (mantissa >> (126u - exponent)));
}

float HalfToFloat(std::uint16_t _half) noexcept
{
  const std::uint32_t sign = static_cast<std::uint32_t>(_half & 0x8000u) << 16u;
  const std::uint32_t exponent = (_half >> 10u) & 0x1Fu;
  const std::uint32_t mantissa = _half & 0x3FFu;
  if (exponent == 0u)
  {
    // Zero or subnormal: mantissa × 2^-24, exact in single precision.
    const float magnitude = static_cast<float>(mantissa) * 5.9604644775390625e-8f;
    return sign != 0u ? -magnitude : magnitude;
  }
  const std::uint32_t bits = exponent == 0x1Fu ? (sign | FLOAT_INFINITY | (mantissa << DROPPED_BITS))
                                               : (sign | ((exponent << 23u) + REBIAS) | (mantissa << DROPPED_BITS));
  return std::bit_cast<float>(bits);
}

} // namespace NeuronCore
