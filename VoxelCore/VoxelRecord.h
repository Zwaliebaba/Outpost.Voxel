#pragma once

#include <cstdint>

namespace VoxelCore
{

// R14: the palette has sixteen entries, and the owner fixed it there (Design/SampleRenderer.md D5).
inline constexpr std::uint32_t PALETTE_ENTRY_COUNT = 16;

// One voxel as the GPU reads it, unpacked (R14, Design/SampleRenderer.md §7.1).
struct VoxelRecord
{
  std::uint8_t x; // model coordinates, in voxels
  std::uint8_t y;
  std::uint8_t z;
  std::uint8_t color; // the palette entry minus one: 0 to 15
};

// The C++ twins of PackVoxelRecord and UnpackVoxelRecord in Packing.hlsli (R15). Bits 0-23 hold x, y and z, bits
// 24-27 the color, and bits 28-31 are zero. The loader never hands over a color above 15; the mask only keeps a bad
// one from reaching bits that do not belong to it.
[[nodiscard]] constexpr std::uint32_t PackVoxelRecord(VoxelRecord _record) noexcept
{
  return static_cast<std::uint32_t>(_record.x) | (static_cast<std::uint32_t>(_record.y) << 8u) |
         (static_cast<std::uint32_t>(_record.z) << 16u) | ((static_cast<std::uint32_t>(_record.color) & 0xFu) << 24u);
}

[[nodiscard]] constexpr VoxelRecord UnpackVoxelRecord(std::uint32_t _packed) noexcept
{
  return {static_cast<std::uint8_t>(_packed & 0xFFu), static_cast<std::uint8_t>((_packed >> 8u) & 0xFFu),
          static_cast<std::uint8_t>((_packed >> 16u) & 0xFFu), static_cast<std::uint8_t>((_packed >> 24u) & 0xFu)};
}

static_assert(sizeof(PackVoxelRecord({})) == 4, "R14: a voxel record is 32 bits");
static_assert(UnpackVoxelRecord(PackVoxelRecord({255, 255, 255, 15})).color == 15);
static_assert(PackVoxelRecord({255, 255, 255, 15}) >> 28u == 0u, "R14: bits 28-31 stay zero");

} // namespace VoxelCore
