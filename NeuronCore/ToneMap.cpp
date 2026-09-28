#include "pch.h"

#include "ToneMap.h"

#include <algorithm>

namespace NeuronCore
{
namespace
{

// A 3 × 3 matrix by rows, applied as HLSL's mul(matrix, vector) applies one.
struct Float3x3
{
  Float3 row0;
  Float3 row1;
  Float3 row2;
};

[[nodiscard]] constexpr Float3 Multiply(const Float3x3& _matrix, Float3 _vector) noexcept
{
  return {Dot(_matrix.row0, _vector), Dot(_matrix.row1, _vector), Dot(_matrix.row2, _vector)};
}

// sRGB to XYZ, D65 to D60, to AP1, and the RRT's saturation.
constexpr Float3x3 ACES_INPUT{{0.59719f, 0.35458f, 0.04823f}, {0.07600f, 0.90834f, 0.01566f}, {0.02840f, 0.13383f, 0.83777f}};

// The ODT's saturation, to XYZ, D60 to D65, and to sRGB.
constexpr Float3x3 ACES_OUTPUT{{1.60475f, -0.53108f, -0.07367f}, {-0.10208f, 1.10813f, -0.00605f}, {-0.00327f, -0.07276f, 1.07602f}};

// The RRT and ODT curves, fitted as one rational function per channel.
[[nodiscard]] constexpr float RrtAndOdtFit(float _value) noexcept
{
  const float numerator = _value * (_value + 0.0245786f) - 0.000090537f;
  const float denominator = _value * (0.983729f * _value + 0.4329510f) + 0.238081f;
  return numerator / denominator;
}

} // namespace

Float3 AcesFitted(Float3 _color) noexcept
{
  const Float3 input = Multiply(ACES_INPUT, _color);
  const Float3 fitted{RrtAndOdtFit(input.x), RrtAndOdtFit(input.y), RrtAndOdtFit(input.z)};
  const Float3 output = Multiply(ACES_OUTPUT, fitted);
  return {std::clamp(output.x, 0.0f, 1.0f), std::clamp(output.y, 0.0f, 1.0f), std::clamp(output.z, 0.0f, 1.0f)};
}

Float3 ToneMap(Float3 _color, float _exposure) noexcept
{
  return AcesFitted(_color * _exposure);
}

} // namespace NeuronCore
