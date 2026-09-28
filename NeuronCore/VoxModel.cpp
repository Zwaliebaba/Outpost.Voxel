#include "pch.h"

#include "VoxModel.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace NeuronCore
{
namespace
{

static_assert(std::endian::native == std::endian::little, "the .vox format is little-endian, and so is this reader");

constexpr std::int32_t MAX_MODEL_EXTENT = 256;
constexpr std::int32_t MAX_SCENE_DEPTH = 64;
constexpr std::string_view IDENTITY_ROTATION = "4"; // _r for the identity: row 0 picks x, row 1 picks y, nothing negated

// MagicaVoxel's axes are right-handed with +Z up; the engine's are Direct3D's, left-handed with +Y up and +Z forward
// (Design/NeuronVoxelFormat.md §4.1). Swapping y and z converts either way. This reader is the one place the engine
// does it, before anything else sees a coordinate: every size, voxel and translation is swapped as it is read.
[[nodiscard]] constexpr Int3 FromMagicaVoxelAxes(Int3 _vector) noexcept
{
  return {_vector.x, _vector.z, _vector.y};
}

using ParseResult = std::expected<void, VoxError>;

// Reads little-endian values from a span. The first read past the end sets Failed(), and every later read returns
// nothing, so that a parser can read a whole record and check once.
class ByteReader
{
public:
  explicit ByteReader(std::span<const std::uint8_t> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed;
  }

  [[nodiscard]] std::size_t Remaining() const noexcept
  {
    return m_bytes.size() - m_offset;
  }

  // Everything was read, and nothing more than there was.
  [[nodiscard]] bool Exhausted() const noexcept
  {
    return !m_failed && Remaining() == 0;
  }

  [[nodiscard]] std::span<const std::uint8_t> Bytes(std::size_t _count) noexcept
  {
    if (m_failed || _count > Remaining())
    {
      m_failed = true;
      return {};
    }
    const std::span<const std::uint8_t> bytes = m_bytes.subspan(m_offset, _count);
    m_offset += _count;
    return bytes;
  }

  [[nodiscard]] std::int32_t Int32() noexcept
  {
    std::int32_t value = 0;
    const std::span<const std::uint8_t> bytes = Bytes(sizeof(value));
    if (!bytes.empty())
    {
      std::memcpy(&value, bytes.data(), sizeof(value));
    }
    return value;
  }

  // A count of things that each take at least _bytesEach more bytes; a count the rest of the chunk cannot hold fails.
  [[nodiscard]] std::size_t Count(std::size_t _bytesEach) noexcept
  {
    const std::int32_t count = Int32();
    if (count < 0 || static_cast<std::size_t>(count) > Remaining() / _bytesEach)
    {
      m_failed = true;
      return 0;
    }
    return static_cast<std::size_t>(count);
  }

  [[nodiscard]] std::string String()
  {
    const std::span<const std::uint8_t> bytes = Bytes(Count(1));
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  }

  [[nodiscard]] VoxAttributes Attributes()
  {
    VoxAttributes attributes;
    const std::size_t count = Count(8);
    for (std::size_t i = 0; i < count && !m_failed; ++i)
    {
      std::string key = String();
      std::string value = String();
      attributes.insert_or_assign(std::move(key), std::move(value));
    }
    return attributes;
  }

private:
  std::span<const std::uint8_t> m_bytes;
  std::size_t m_offset = 0;
  bool m_failed = false;
};

struct Chunk
{
  std::string_view id;
  std::span<const std::uint8_t> content;
  std::span<const std::uint8_t> children;
};

// A transform keeps only the two attributes placement reads, unparsed, so that nothing of a hidden node is parsed. It
// does not keep its frame's dictionary: moving an MSVC std::map can allocate, and so throw, and a move that can throw
// is one clang-tidy's bugprone-exception-escape refuses.
struct TransformNode
{
  std::int32_t child;
  std::int32_t layer;
  bool hidden;
  std::size_t frameCount;
  std::optional<std::string> translation; // the first frame's _t
  std::optional<std::string> rotation;    // the first frame's _r
};

struct GroupNode
{
  bool hidden;
  std::vector<std::int32_t> children;
};

struct ShapeNode
{
  std::vector<std::int32_t> models;
};

struct RawModel
{
  Int3 size;
  std::vector<VoxelRecord> voxels;
};

// Everything the chunks say, before the scene graph is followed.
struct SceneParts
{
  std::vector<RawModel> models;
  std::optional<Int3> pendingSize; // a SIZE still waiting for its XYZI
  std::map<std::int32_t, TransformNode> transforms;
  std::map<std::int32_t, GroupNode> groups;
  std::map<std::int32_t, ShapeNode> shapes;
  std::set<std::int32_t> hiddenLayers;
  std::array<PaletteEntry, PALETTE_ENTRY_COUNT> palette{};
  bool hasPalette = false;
  std::vector<VoxAttributes> renderObjects;
};

[[nodiscard]] std::optional<Chunk> NextChunk(ByteReader& _reader) noexcept
{
  const std::span<const std::uint8_t> id = _reader.Bytes(4);
  const std::int32_t contentBytes = _reader.Int32();
  const std::int32_t childrenBytes = _reader.Int32();
  if (_reader.Failed() || contentBytes < 0 || childrenBytes < 0)
  {
    return std::nullopt;
  }
  Chunk chunk{{reinterpret_cast<const char*>(id.data()), id.size()},
              _reader.Bytes(static_cast<std::size_t>(contentBytes)),
              _reader.Bytes(static_cast<std::size_t>(childrenBytes))};
  if (_reader.Failed())
  {
    return std::nullopt;
  }
  return chunk;
}

[[nodiscard]] bool IsHidden(const VoxAttributes& _attributes)
{
  const auto hidden = _attributes.find("_hidden");
  return hidden != _attributes.end() && hidden->second == "1";
}

[[nodiscard]] std::optional<std::string> Attribute(const VoxAttributes& _attributes, std::string_view _key)
{
  const auto found = _attributes.find(_key);
  return found != _attributes.end() ? std::optional<std::string>(found->second) : std::nullopt;
}

[[nodiscard]] std::optional<float> ParseFloat(std::string_view _text) noexcept
{
  float value = 0.0f;
  const char* first = _text.data();
  const char* last = first + _text.size();
  const auto [next, error] = std::from_chars(first, last, value);
  if (error != std::errc{} || next != last)
  {
    return std::nullopt;
  }
  return value;
}

// A translation as _t stores it, "x y z" in MagicaVoxel's axes, in the engine's.
[[nodiscard]] std::optional<Int3> ParseTranslation(std::string_view _text) noexcept
{
  std::array<std::int32_t, 3> values{};
  const char* cursor = _text.data();
  const char* end = _text.data() + _text.size();
  for (std::size_t i = 0; i < values.size(); ++i)
  {
    if (i > 0)
    {
      if (cursor == end || *cursor != ' ')
      {
        return std::nullopt;
      }
      ++cursor;
    }
    const auto [next, error] = std::from_chars(cursor, end, values[i]);
    if (error != std::errc{})
    {
      return std::nullopt;
    }
    cursor = next;
  }
  if (cursor != end)
  {
    return std::nullopt;
  }
  return FromMagicaVoxelAxes({values[0], values[1], values[2]});
}

// _a + _b, when every component stays within MAX_TRANSLATION. Both are within int32, so the sum cannot overflow int64.
[[nodiscard]] std::optional<Int3> AddTranslation(Int3 _a, Int3 _b) noexcept
{
  const std::array<std::int64_t, 3> sum{std::int64_t{_a.x} + _b.x, std::int64_t{_a.y} + _b.y, std::int64_t{_a.z} + _b.z};
  if (std::ranges::any_of(sum, [](std::int64_t _value) { return _value < -MAX_TRANSLATION || _value > MAX_TRANSLATION; }))
  {
    return std::nullopt;
  }
  return Int3{static_cast<std::int32_t>(sum[0]), static_cast<std::int32_t>(sum[1]), static_cast<std::int32_t>(sum[2])};
}

[[nodiscard]] ParseResult ReadSize(std::span<const std::uint8_t> _content, SceneParts& _parts) noexcept
{
  ByteReader reader(_content);
  const Int3 size{reader.Int32(), reader.Int32(), reader.Int32()};
  if (!reader.Exhausted() || _parts.pendingSize || size.x < 1 || size.y < 1 || size.z < 1)
  {
    return std::unexpected(VoxError::MalformedChunk);
  }
  if (size.x > MAX_MODEL_EXTENT || size.y > MAX_MODEL_EXTENT || size.z > MAX_MODEL_EXTENT)
  {
    return std::unexpected(VoxError::ModelTooLarge);
  }
  _parts.pendingSize = FromMagicaVoxelAxes(size);
  return {};
}

[[nodiscard]] ParseResult ReadVoxels(std::span<const std::uint8_t> _content, SceneParts& _parts)
{
  ByteReader reader(_content);
  const std::size_t count = reader.Count(4);
  const std::span<const std::uint8_t> bytes = reader.Bytes(4 * count);
  if (!reader.Exhausted() || !_parts.pendingSize)
  {
    return std::unexpected(VoxError::MalformedChunk);
  }

  RawModel model{*_parts.pendingSize, {}};
  _parts.pendingSize.reset();
  const auto sizeX = static_cast<std::size_t>(model.size.x);
  const auto sizeY = static_cast<std::size_t>(model.size.y);
  std::vector<bool> occupied(sizeX * sizeY * static_cast<std::size_t>(model.size.z));
  model.voxels.reserve(count);
  for (std::size_t i = 0; i < count; ++i)
  {
    // XYZI stores x, y and z in MagicaVoxel's axes; the engine's y is the file's z (FromMagicaVoxelAxes).
    const std::uint8_t x = bytes[4 * i];
    const std::uint8_t y = bytes[4 * i + 2];
    const std::uint8_t z = bytes[4 * i + 1];
    const std::uint32_t color = bytes[4 * i + 3];
    if (x >= model.size.x || y >= model.size.y || z >= model.size.z)
    {
      return std::unexpected(VoxError::VoxelOutOfBounds);
    }
    if (color < 1u || color > PALETTE_ENTRY_COUNT)
    {
      return std::unexpected(VoxError::ColorOutOfRange);
    }
    const std::size_t cell = x + sizeX * (y + sizeY * z);
    if (occupied[cell])
    {
      return std::unexpected(VoxError::DuplicateVoxel);
    }
    occupied[cell] = true;
    model.voxels.push_back({x, y, z, static_cast<std::uint8_t>(color - 1)});
  }
  _parts.models.push_back(std::move(model));
  return {};
}

[[nodiscard]] ParseResult ReadPalette(std::span<const std::uint8_t> _content, SceneParts& _parts) noexcept
{
  if (_content.size() != 256 * sizeof(std::uint32_t))
  {
    return std::unexpected(VoxError::MalformedChunk);
  }
  // The chunk's first color is palette entry 1; entry 0 is never drawn.
  for (std::size_t i = 0; i < _parts.palette.size(); ++i)
  {
    PaletteEntry& entry = _parts.palette[i];
    entry.red = _content[4 * i];
    entry.green = _content[4 * i + 1];
    entry.blue = _content[4 * i + 2];
    entry.alpha = _content[4 * i + 3];
  }
  _parts.hasPalette = true;
  return {};
}

[[nodiscard]] ParseResult ReadMaterial(std::span<const std::uint8_t> _content, SceneParts& _parts)
{
  ByteReader reader(_content);
  const std::int32_t id = reader.Int32();
  const VoxAttributes attributes = reader.Attributes();
  if (!reader.Exhausted())
  {
    return std::unexpected(VoxError::MalformedChunk);
  }
  if (id < 1 || id > static_cast<std::int32_t>(PALETTE_ENTRY_COUNT))
  {
    return {}; // a material for an entry no voxel can use
  }
  PaletteEntry& entry = _parts.palette[static_cast<std::size_t>(id - 1)];
  const auto type = attributes.find("_type");
  entry.emissive = type != attributes.end() && type->second == "_emit";
  for (const auto& [key, target] : {std::pair{"_emit", &entry.emit}, std::pair{"_flux", &entry.flux}})
  {
    const auto found = attributes.find(key);
    if (found == attributes.end())
    {
      continue;
    }
    const std::optional<float> value = ParseFloat(found->second);
    if (!value)
    {
      return std::unexpected(VoxError::MalformedChunk);
    }
    *target = *value;
  }
  return {};
}

[[nodiscard]] ParseResult AddNode(std::int32_t _id, const SceneParts& _parts)
{
  const bool taken = _parts.transforms.contains(_id) || _parts.groups.contains(_id) || _parts.shapes.contains(_id);
  return taken ? ParseResult{std::unexpected(VoxError::BadSceneGraph)} : ParseResult{};
}

[[nodiscard]] ParseResult ReadTransform(std::span<const std::uint8_t> _content, SceneParts& _parts)
{
  ByteReader reader(_content);
  const std::int32_t id = reader.Int32();
  TransformNode node{};
  node.hidden = IsHidden(reader.Attributes());
  node.child = reader.Int32();
  static_cast<void>(reader.Bytes(4)); // a reserved id, always -1
  node.layer = reader.Int32();
  node.frameCount = reader.Count(4);
  for (std::size_t i = 0; i < node.frameCount && !reader.Failed(); ++i)
  {
    const VoxAttributes frame = reader.Attributes();
    if (i == 0)
    {
      node.translation = Attribute(frame, "_t");
      node.rotation = Attribute(frame, "_r");
    }
  }
  if (!reader.Exhausted())
  {
    return std::unexpected(VoxError::MalformedChunk);
  }
  if (const ParseResult added = AddNode(id, _parts); !added)
  {
    return added;
  }
  _parts.transforms.emplace(id, std::move(node));
  return {};
}

[[nodiscard]] ParseResult ReadGroup(std::span<const std::uint8_t> _content, SceneParts& _parts)
{
  ByteReader reader(_content);
  const std::int32_t id = reader.Int32();
  GroupNode node{};
  node.hidden = IsHidden(reader.Attributes());
  const std::size_t count = reader.Count(4);
  node.children.reserve(count);
  for (std::size_t i = 0; i < count; ++i)
  {
    node.children.push_back(reader.Int32());
  }
  if (!reader.Exhausted())
  {
    return std::unexpected(VoxError::MalformedChunk);
  }
  if (const ParseResult added = AddNode(id, _parts); !added)
  {
    return added;
  }
  _parts.groups.emplace(id, std::move(node));
  return {};
}

[[nodiscard]] ParseResult ReadShape(std::span<const std::uint8_t> _content, SceneParts& _parts)
{
  ByteReader reader(_content);
  const std::int32_t id = reader.Int32();
  static_cast<void>(reader.Attributes());
  ShapeNode node{};
  const std::size_t count = reader.Count(8);
  node.models.reserve(count);
  for (std::size_t i = 0; i < count && !reader.Failed(); ++i)
  {
    node.models.push_back(reader.Int32());
    static_cast<void>(reader.Attributes()); // _f, the frame a model belongs to
  }
  if (!reader.Exhausted())
  {
    return std::unexpected(VoxError::MalformedChunk);
  }
  if (const ParseResult added = AddNode(id, _parts); !added)
  {
    return added;
  }
  _parts.shapes.emplace(id, std::move(node));
  return {};
}

[[nodiscard]] ParseResult ReadLayer(std::span<const std::uint8_t> _content, SceneParts& _parts)
{
  ByteReader reader(_content);
  const std::int32_t id = reader.Int32();
  const bool hidden = IsHidden(reader.Attributes());
  static_cast<void>(reader.Bytes(4)); // a reserved id, always -1
  if (!reader.Exhausted())
  {
    return std::unexpected(VoxError::MalformedChunk);
  }
  if (hidden)
  {
    _parts.hiddenLayers.insert(id);
  }
  return {};
}

[[nodiscard]] ParseResult ReadRenderObject(std::span<const std::uint8_t> _content, SceneParts& _parts)
{
  ByteReader reader(_content);
  VoxAttributes attributes = reader.Attributes();
  if (!reader.Exhausted())
  {
    return std::unexpected(VoxError::MalformedChunk);
  }
  _parts.renderObjects.push_back(std::move(attributes));
  return {};
}

[[nodiscard]] ParseResult ReadChunk(const Chunk& _chunk, SceneParts& _parts)
{
  if (_chunk.id == "SIZE")
  {
    return ReadSize(_chunk.content, _parts);
  }
  if (_chunk.id == "XYZI")
  {
    return ReadVoxels(_chunk.content, _parts);
  }
  if (_chunk.id == "RGBA")
  {
    return ReadPalette(_chunk.content, _parts);
  }
  if (_chunk.id == "MATL")
  {
    return ReadMaterial(_chunk.content, _parts);
  }
  if (_chunk.id == "nTRN")
  {
    return ReadTransform(_chunk.content, _parts);
  }
  if (_chunk.id == "nGRP")
  {
    return ReadGroup(_chunk.content, _parts);
  }
  if (_chunk.id == "nSHP")
  {
    return ReadShape(_chunk.content, _parts);
  }
  if (_chunk.id == "LAYR")
  {
    return ReadLayer(_chunk.content, _parts);
  }
  if (_chunk.id == "rOBJ")
  {
    return ReadRenderObject(_chunk.content, _parts);
  }
  return {}; // META, rCAM, NOTE, IMAP, PACK and anything newer: nothing the renderer reads
}

// A model placed by the scene graph: its index among the chunks, and the translation of its centre.
struct Placement
{
  std::size_t model;
  Int3 translation;
};

[[nodiscard]] ParseResult Place(const SceneParts& _parts, std::int32_t _node, Int3 _translation, std::int32_t _depth,
                                std::set<std::int32_t>& _visited, std::vector<Placement>& _placements)
{
  if (_depth > MAX_SCENE_DEPTH || !_visited.insert(_node).second)
  {
    return std::unexpected(VoxError::BadSceneGraph);
  }
  if (const auto transform = _parts.transforms.find(_node); transform != _parts.transforms.end())
  {
    const TransformNode& node = transform->second;
    if (node.hidden || _parts.hiddenLayers.contains(node.layer))
    {
      return {};
    }
    if (node.frameCount != 1)
    {
      return std::unexpected(VoxError::UnsupportedAnimation);
    }
    if (node.rotation && *node.rotation != IDENTITY_ROTATION)
    {
      return std::unexpected(VoxError::UnsupportedRotation);
    }
    Int3 translation = _translation;
    if (node.translation)
    {
      const std::optional<Int3> parsed = ParseTranslation(*node.translation);
      if (!parsed)
      {
        return std::unexpected(VoxError::MalformedChunk);
      }
      const std::optional<Int3> sum = AddTranslation(translation, *parsed);
      if (!sum)
      {
        return std::unexpected(VoxError::TranslationOutOfRange);
      }
      translation = *sum;
    }
    return Place(_parts, node.child, translation, _depth + 1, _visited, _placements);
  }
  if (const auto group = _parts.groups.find(_node); group != _parts.groups.end())
  {
    if (group->second.hidden)
    {
      return {};
    }
    for (const std::int32_t child : group->second.children)
    {
      if (const ParseResult placed = Place(_parts, child, _translation, _depth + 1, _visited, _placements); !placed)
      {
        return placed;
      }
    }
    return {};
  }
  if (const auto shape = _parts.shapes.find(_node); shape != _parts.shapes.end())
  {
    if (shape->second.models.size() != 1)
    {
      return std::unexpected(VoxError::UnsupportedAnimation);
    }
    const std::int32_t model = shape->second.models.front();
    if (model < 0 || static_cast<std::size_t>(model) >= _parts.models.size())
    {
      return std::unexpected(VoxError::BadSceneGraph);
    }
    _placements.push_back({static_cast<std::size_t>(model), _translation});
    return {};
  }
  return std::unexpected(VoxError::BadSceneGraph);
}

} // namespace

const char* VoxErrorName(VoxError _error) noexcept
{
  switch (_error)
  {
  case VoxError::FileNotFound:
    return "FileNotFound";
  case VoxError::ReadFailed:
    return "ReadFailed";
  case VoxError::NotAVoxFile:
    return "NotAVoxFile";
  case VoxError::UnsupportedVersion:
    return "UnsupportedVersion";
  case VoxError::Truncated:
    return "Truncated";
  case VoxError::MalformedChunk:
    return "MalformedChunk";
  case VoxError::ModelTooLarge:
    return "ModelTooLarge";
  case VoxError::VoxelOutOfBounds:
    return "VoxelOutOfBounds";
  case VoxError::DuplicateVoxel:
    return "DuplicateVoxel";
  case VoxError::ColorOutOfRange:
    return "ColorOutOfRange";
  case VoxError::MissingPalette:
    return "MissingPalette";
  case VoxError::MissingSceneGraph:
    return "MissingSceneGraph";
  case VoxError::BadSceneGraph:
    return "BadSceneGraph";
  case VoxError::UnsupportedRotation:
    return "UnsupportedRotation";
  case VoxError::UnsupportedAnimation:
    return "UnsupportedAnimation";
  case VoxError::TranslationOutOfRange:
    return "TranslationOutOfRange";
  }
  return "Unknown";
}

std::expected<VoxModel, VoxError> ParseVoxModel(std::span<const std::uint8_t> _bytes)
{
  ByteReader file(_bytes);
  const std::span<const std::uint8_t> magic = file.Bytes(4);
  const std::int32_t version = file.Int32();
  if (file.Failed() || std::string_view{reinterpret_cast<const char*>(magic.data()), magic.size()} != "VOX ")
  {
    return std::unexpected(VoxError::NotAVoxFile);
  }
  if (version != 150 && version != 200)
  {
    return std::unexpected(VoxError::UnsupportedVersion);
  }

  const std::optional<Chunk> main = NextChunk(file);
  if (!main)
  {
    return std::unexpected(VoxError::Truncated);
  }
  if (main->id != "MAIN")
  {
    return std::unexpected(VoxError::NotAVoxFile);
  }
  if (!main->content.empty() || file.Remaining() != 0)
  {
    return std::unexpected(VoxError::MalformedChunk); // MAIN holds nothing itself, and nothing follows it
  }

  SceneParts parts;
  ByteReader children(main->children);
  while (children.Remaining() > 0)
  {
    const std::optional<Chunk> chunk = NextChunk(children);
    if (!chunk)
    {
      return std::unexpected(VoxError::Truncated);
    }
    if (const ParseResult read = ReadChunk(*chunk, parts); !read)
    {
      return std::unexpected(read.error());
    }
  }
  if (parts.pendingSize)
  {
    return std::unexpected(VoxError::MalformedChunk);
  }
  if (!parts.hasPalette)
  {
    return std::unexpected(VoxError::MissingPalette);
  }
  if (!parts.transforms.contains(0))
  {
    return std::unexpected(VoxError::MissingSceneGraph);
  }

  std::vector<Placement> placements;
  std::set<std::int32_t> visited;
  if (const ParseResult placed = Place(parts, 0, {0, 0, 0}, 0, visited, placements); !placed)
  {
    return std::unexpected(placed.error());
  }

  VoxModel model{};
  model.version = version;
  model.palette = parts.palette;
  model.renderObjects = std::move(parts.renderObjects);
  std::size_t recordCount = 0;
  for (const Placement& placement : placements)
  {
    recordCount += parts.models[placement.model].voxels.size();
  }
  model.records.reserve(recordCount);
  model.instances.reserve(placements.size());
  for (const Placement& placement : placements)
  {
    const RawModel& raw = parts.models[placement.model];
    const Int3 origin = placement.translation - Int3{raw.size.x / 2, raw.size.y / 2, raw.size.z / 2};
    model.instances.push_back(
      {origin, raw.size, static_cast<std::uint32_t>(model.records.size()), static_cast<std::uint32_t>(raw.voxels.size())});
    for (const VoxelRecord& voxel : raw.voxels)
    {
      model.records.push_back(PackVoxelRecord(voxel));
    }
  }
  return model;
}

std::expected<std::vector<std::uint8_t>, VoxError> ReadVoxFile(const std::filesystem::path& _path)
{
  std::ifstream file(_path, std::ios::binary | std::ios::ate);
  if (!file.is_open())
  {
    return std::unexpected(VoxError::FileNotFound);
  }
  const std::streamoff size = file.tellg();
  if (size < 0)
  {
    return std::unexpected(VoxError::ReadFailed);
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
  {
    return std::unexpected(VoxError::ReadFailed);
  }
  return bytes;
}

std::expected<VoxModel, VoxError> LoadVoxModel(const std::filesystem::path& _path)
{
  return ReadVoxFile(_path).and_then([](const std::vector<std::uint8_t>& _bytes) { return ParseVoxModel(_bytes); });
}

Box CellBox(Int3 _minCorner) noexcept
{
  const Float3 center{static_cast<float>(_minCorner.x) + 0.5f, static_cast<float>(_minCorner.y) + 0.5f,
                      static_cast<float>(_minCorner.z) + 0.5f};
  return MakeAxisAlignedBox(center, {0.5f, 0.5f, 0.5f});
}

Box VoxelBox(const ModelInstance& _instance, std::uint32_t _record) noexcept
{
  const VoxelRecord voxel = UnpackVoxelRecord(_record);
  return CellBox(_instance.origin + Int3{voxel.x, voxel.y, voxel.z});
}

std::optional<VoxelBounds> OccupiedBounds(const VoxModel& _model) noexcept
{
  std::optional<VoxelBounds> bounds;
  for (const ModelInstance& instance : _model.instances)
  {
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      const VoxelRecord voxel = UnpackVoxelRecord(_model.records[instance.firstRecord + i]);
      const Int3 corner = instance.origin + Int3{voxel.x, voxel.y, voxel.z};
      const Int3 beyond = corner + Int3{1, 1, 1};
      if (!bounds)
      {
        bounds = VoxelBounds{corner, beyond};
        continue;
      }
      bounds->lower = {std::min(bounds->lower.x, corner.x), std::min(bounds->lower.y, corner.y), std::min(bounds->lower.z, corner.z)};
      bounds->upper = {std::max(bounds->upper.x, beyond.x), std::max(bounds->upper.y, beyond.y), std::max(bounds->upper.z, beyond.z)};
    }
  }
  return bounds;
}

} // namespace NeuronCore
