#pragma once

#include <cstdint>
#include <span>

namespace NeuronCore
{

// The PCG hash of Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 9(3), 2020: the voxel index view's colors
// and the detonation's randomness (Design/Archive/SampleRenderer.md §11, Design/SpaceScene.md §5.5). The twin of PcgHash
// in Shader/Hash.hlsli (R15).
[[nodiscard]] constexpr std::uint32_t PcgHash(std::uint32_t _value) noexcept
{
  const std::uint32_t state = _value * 747796405u + 2891336453u;
  const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

// 64-bit FNV-1a over _bytes: the hash a welcome's manifest gives each model's file, which a client checks against its
// own file's (Design/SpaceScene.md §6.2). Not a cryptographic hash: it catches two copies of a file that differ, not an
// attacker.
[[nodiscard]] constexpr std::uint64_t Fnv1aHash64(std::span<const std::uint8_t> _bytes) noexcept
{
  std::uint64_t hash = 0xCBF29CE484222325ull;
  for (const std::uint8_t byte : _bytes)
  {
    hash ^= byte;
    hash *= 0x00000100000001B3ull;
  }
  return hash;
}

} // namespace NeuronCore
