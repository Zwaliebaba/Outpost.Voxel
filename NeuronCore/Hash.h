#pragma once

#include <cstdint>

namespace NeuronCore
{

// The PCG hash of Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 9(3), 2020: the voxel index view's colors
// and the explosion's randomness (Design/Archive/SampleRenderer.md §11, §12). The twin of PcgHash in Shader/Hash.hlsli (R15).
[[nodiscard]] constexpr std::uint32_t PcgHash(std::uint32_t _value) noexcept
{
  const std::uint32_t state = _value * 747796405u + 2891336453u;
  const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

} // namespace NeuronCore
