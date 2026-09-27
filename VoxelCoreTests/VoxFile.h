#pragma once

#include "Float3.h"
#include "VoxModel.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace VoxelCoreTests
{

using Bytes = std::vector<std::uint8_t>;

// A voxel as XYZI stores it: the color is the file's palette entry, 1-255.
struct FileVoxel
{
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t z;
  std::uint8_t color;
};

// The sixteen EGA colors, as MilitaryStation.vox holds them in palette entries 1-16.
inline constexpr std::array<std::array<std::uint8_t, 4>, 16> EGA_PALETTE{{
  {0, 0, 0, 255},
  {0, 0, 170, 255},
  {0, 170, 0, 255},
  {0, 170, 170, 255},
  {170, 0, 0, 255},
  {170, 0, 170, 255},
  {170, 85, 0, 255},
  {170, 170, 170, 255},
  {85, 85, 85, 255},
  {85, 85, 255, 255},
  {85, 255, 85, 255},
  {85, 255, 255, 255},
  {255, 85, 85, 255},
  {255, 85, 255, 255},
  {255, 255, 85, 255},
  {255, 255, 255, 255},
}};

// Encoders for the chunks of a .vox file, after MagicaVoxel-file-format-vox.txt and -vox-extension.txt. Each returns
// the whole chunk: id, sizes, content and children.
[[nodiscard]] Bytes Chunk(std::string_view _id, const Bytes& _content, const Bytes& _children = {});
[[nodiscard]] Bytes SizeChunk(VoxelCore::Int3 _size);
[[nodiscard]] Bytes VoxelsChunk(std::span<const FileVoxel> _voxels);
[[nodiscard]] Bytes PaletteChunk(); // EGA in entries 1-16, black in the rest
[[nodiscard]] Bytes MaterialChunk(std::int32_t _id, const VoxelCore::VoxAttributes& _attributes);
[[nodiscard]] Bytes TransformChunk(std::int32_t _id, const VoxelCore::VoxAttributes& _attributes, std::int32_t _child, std::int32_t _layer,
                                   const std::vector<VoxelCore::VoxAttributes>& _frames);
[[nodiscard]] Bytes GroupChunk(std::int32_t _id, const VoxelCore::VoxAttributes& _attributes, const std::vector<std::int32_t>& _children);
[[nodiscard]] Bytes ShapeChunk(std::int32_t _id, const std::vector<std::int32_t>& _models);
[[nodiscard]] Bytes LayerChunk(std::int32_t _id, const VoxelCore::VoxAttributes& _attributes);
[[nodiscard]] Bytes RenderObjectChunk(const VoxelCore::VoxAttributes& _attributes);

// A whole file: the header, then MAIN with _chunks as its children.
[[nodiscard]] Bytes VoxFile(const std::vector<Bytes>& _chunks, std::int32_t _version = 200);

// The chunks MagicaVoxel writes for a scene of one model: SIZE and XYZI, the scene graph nTRN 0 -> nGRP 1 -> nTRN 2 ->
// nSHP 3 with _translation on node 2, and the palette.
[[nodiscard]] std::vector<Bytes> OneModelChunks(VoxelCore::Int3 _size, std::span<const FileVoxel> _voxels, std::string_view _translation);

} // namespace VoxelCoreTests
