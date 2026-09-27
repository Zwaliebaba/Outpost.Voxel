#include "pch.h"

#include "VoxFile.h"

#include <cstring>
#include <string>

namespace VoxelCoreTests
{
namespace
{

void AppendInt32(Bytes& _bytes, std::int32_t _value)
{
  std::array<std::uint8_t, 4> little{};
  std::memcpy(little.data(), &_value, sizeof(_value));
  _bytes.insert(_bytes.end(), little.begin(), little.end());
}

void AppendString(Bytes& _bytes, std::string_view _text)
{
  AppendInt32(_bytes, static_cast<std::int32_t>(_text.size()));
  _bytes.insert(_bytes.end(), _text.begin(), _text.end());
}

void AppendAttributes(Bytes& _bytes, const VoxelCore::VoxAttributes& _attributes)
{
  AppendInt32(_bytes, static_cast<std::int32_t>(_attributes.size()));
  for (const auto& [key, value] : _attributes)
  {
    AppendString(_bytes, key);
    AppendString(_bytes, value);
  }
}

} // namespace

Bytes Chunk(std::string_view _id, const Bytes& _content, const Bytes& _children)
{
  Bytes chunk(_id.begin(), _id.end());
  AppendInt32(chunk, static_cast<std::int32_t>(_content.size()));
  AppendInt32(chunk, static_cast<std::int32_t>(_children.size()));
  chunk.insert(chunk.end(), _content.begin(), _content.end());
  chunk.insert(chunk.end(), _children.begin(), _children.end());
  return chunk;
}

Bytes SizeChunk(VoxelCore::Int3 _size)
{
  Bytes content;
  AppendInt32(content, _size.x);
  AppendInt32(content, _size.y);
  AppendInt32(content, _size.z);
  return Chunk("SIZE", content);
}

Bytes VoxelsChunk(std::span<const FileVoxel> _voxels)
{
  Bytes content;
  AppendInt32(content, static_cast<std::int32_t>(_voxels.size()));
  for (const FileVoxel& voxel : _voxels)
  {
    content.insert(content.end(), {voxel.x, voxel.y, voxel.z, voxel.color});
  }
  return Chunk("XYZI", content);
}

Bytes PaletteChunk()
{
  Bytes content(256 * sizeof(std::uint32_t), 0);
  for (std::size_t i = 0; i < EGA_PALETTE.size(); ++i)
  {
    std::memcpy(content.data() + 4 * i, EGA_PALETTE[i].data(), 4);
  }
  return Chunk("RGBA", content);
}

Bytes MaterialChunk(std::int32_t _id, const VoxelCore::VoxAttributes& _attributes)
{
  Bytes content;
  AppendInt32(content, _id);
  AppendAttributes(content, _attributes);
  return Chunk("MATL", content);
}

Bytes TransformChunk(std::int32_t _id, const VoxelCore::VoxAttributes& _attributes, std::int32_t _child, std::int32_t _layer,
                     const std::vector<VoxelCore::VoxAttributes>& _frames)
{
  Bytes content;
  AppendInt32(content, _id);
  AppendAttributes(content, _attributes);
  AppendInt32(content, _child);
  AppendInt32(content, -1); // the reserved id
  AppendInt32(content, _layer);
  AppendInt32(content, static_cast<std::int32_t>(_frames.size()));
  for (const VoxelCore::VoxAttributes& frame : _frames)
  {
    AppendAttributes(content, frame);
  }
  return Chunk("nTRN", content);
}

Bytes GroupChunk(std::int32_t _id, const VoxelCore::VoxAttributes& _attributes, const std::vector<std::int32_t>& _children)
{
  Bytes content;
  AppendInt32(content, _id);
  AppendAttributes(content, _attributes);
  AppendInt32(content, static_cast<std::int32_t>(_children.size()));
  for (const std::int32_t child : _children)
  {
    AppendInt32(content, child);
  }
  return Chunk("nGRP", content);
}

Bytes ShapeChunk(std::int32_t _id, const std::vector<std::int32_t>& _models)
{
  Bytes content;
  AppendInt32(content, _id);
  AppendAttributes(content, {});
  AppendInt32(content, static_cast<std::int32_t>(_models.size()));
  for (const std::int32_t model : _models)
  {
    AppendInt32(content, model);
    AppendAttributes(content, {});
  }
  return Chunk("nSHP", content);
}

Bytes LayerChunk(std::int32_t _id, const VoxelCore::VoxAttributes& _attributes)
{
  Bytes content;
  AppendInt32(content, _id);
  AppendAttributes(content, _attributes);
  AppendInt32(content, -1); // the reserved id
  return Chunk("LAYR", content);
}

Bytes RenderObjectChunk(const VoxelCore::VoxAttributes& _attributes)
{
  Bytes content;
  AppendAttributes(content, _attributes);
  return Chunk("rOBJ", content);
}

Bytes VoxFile(const std::vector<Bytes>& _chunks, std::int32_t _version)
{
  Bytes children;
  for (const Bytes& chunk : _chunks)
  {
    children.insert(children.end(), chunk.begin(), chunk.end());
  }
  Bytes file{'V', 'O', 'X', ' '};
  AppendInt32(file, _version);
  const Bytes mainChunk = Chunk("MAIN", {}, children);
  file.insert(file.end(), mainChunk.begin(), mainChunk.end());
  return file;
}

std::vector<Bytes> OneModelChunks(VoxelCore::Int3 _size, std::span<const FileVoxel> _voxels, std::string_view _translation)
{
  return {SizeChunk(_size),
          VoxelsChunk(_voxels),
          TransformChunk(0, {}, 1, -1, {{}}),
          GroupChunk(1, {}, {2}),
          TransformChunk(2, {}, 3, 0, {{{"_t", std::string(_translation)}}}),
          ShapeChunk(3, {0}),
          PaletteChunk()};
}

} // namespace VoxelCoreTests
