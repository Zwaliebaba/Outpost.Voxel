#pragma once

#include "Float3.h"

#include <cstdint>

namespace NeuronCore
{

// The world-space normal as the visibility buffer stores it: an octahedral map, two 16-bit snorm values in one uint
// (Design/Archive/SampleRenderer.md §7.3). The C++ twins of PackOctahedralNormal and UnpackOctahedralNormal in Packing.hlsli
// (R15). The six axis directions, which is every normal of the intact station, survive the round trip exactly.
[[nodiscard]] std::uint32_t PackOctahedralNormal(Float3 _normal) noexcept;
[[nodiscard]] Float3 UnpackOctahedralNormal(std::uint32_t _packed) noexcept;

} // namespace NeuronCore
