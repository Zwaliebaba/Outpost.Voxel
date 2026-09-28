#pragma once

#include <cstdint>

namespace NeuronCore
{

// One sRGB-encoded byte as a linear value, by the exact sRGB curve rather than a gamma of 2.2 (Design/Archive/SampleRenderer.md
// §7.2). 0 maps to 0 and 255 to 1.
[[nodiscard]] float SrgbToLinear(std::uint8_t _encoded) noexcept;

} // namespace NeuronCore
