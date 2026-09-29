#pragma once

#include <cstdint>

namespace NeuronCore
{

// IEEE 754 half precision, as a DXGI_FORMAT_R16G16B16A16_FLOAT target stores each channel: the HDR color and bloom's
// chain (Design/Archive/SpaceScene.md §12.2). A twin that stands in for what the GPU stores rounds through these.

// The half the GPU stores for _value, by Direct3D's rules for converting a float to a narrower one: rounded toward zero,
// not to the nearest, so that from the largest half, 65504, on it stays the largest half rather than becoming infinity.
// Infinity stays infinity, and a NaN a NaN.
[[nodiscard]] std::uint16_t FloatToHalf(float _value) noexcept;

// _half's value, exactly: every half is a float.
[[nodiscard]] float HalfToFloat(std::uint16_t _half) noexcept;

// _value as the GPU stores it in a half.
[[nodiscard]] inline float RoundToHalf(float _value) noexcept
{
  return HalfToFloat(FloatToHalf(_value));
}

} // namespace NeuronCore
