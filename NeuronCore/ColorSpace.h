#pragma once

#include "Float3.h"

#include <cstdint>

namespace NeuronCore
{

// One sRGB-encoded byte as a linear value, by the exact sRGB curve rather than a gamma of 2.2 (Design/Archive/SampleRenderer.md
// §7.2). 0 maps to 0 and 255 to 1.
[[nodiscard]] float SrgbToLinear(std::uint8_t _encoded) noexcept;

// The luminance of a linear Rec. 709 color, with the weights of its Y row: the stars' unit (Design/SpaceScene.md §11.2)
// and Karis's average in bloom's first halving (§12.2). The twin of Luminance in Shader/Bloom.hlsli (R15).
[[nodiscard]] constexpr float Luminance(Float3 _color) noexcept
{
  return Dot(_color, {0.2126f, 0.7152f, 0.0722f});
}

} // namespace NeuronCore
