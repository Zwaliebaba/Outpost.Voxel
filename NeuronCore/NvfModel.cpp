#include "pch.h"

#include "NvfModel.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace NeuronCore
{
namespace
{

static_assert(std::endian::native == std::endian::little, "NVF is little-endian, and the records are copied as they lie");

constexpr std::array<char, 4> MAGIC{'N', 'V', 'F', ' '};
constexpr std::size_t ALIGNMENT_BYTES = 16;

// The five chunks of version 1.0, in the order they must appear (§4.2).
enum class KnownChunk : std::uint8_t
{
  Strings,
  Palette,
  Parts,
  Voxels,
  Hardpoints
};

constexpr std::array<std::string_view, 5> KNOWN_CHUNK_IDS{"STRS", "PALT", "PART", "VOXL", "HPNT"};
constexpr std::size_t KNOWN_CHUNK_COUNT = KNOWN_CHUNK_IDS.size();

constexpr std::uint32_t PALETTE_EMISSIVE = 1u;
constexpr std::uint16_t PART_PIVOT_AUTHORED = 1u;
constexpr std::uint32_t HARDPOINT_FROM_VOX = 1u;

// A voxel record's bits 28-31, which R14 keeps zero.
constexpr std::uint32_t RESERVED_RECORD_BITS = 0xF0000000u;

// The reserved hardpoint type: `pivot` names a part's pivot marker in the .vox, never a hardpoint (§4.1, §5).
constexpr std::string_view PIVOT_TYPE = "pivot";

using Parsed = std::expected<NvfModel, NvfError>;

struct ChunkSpan
{
  std::string_view id;
  std::uint32_t elementCount;
  std::span<const std::uint8_t> content; // sizeBytes long, the padding excluded
};

[[nodiscard]] constexpr std::uint64_t Padded(std::uint64_t _sizeBytes) noexcept
{
  return (_sizeBytes + ALIGNMENT_BYTES - 1) / ALIGNMENT_BYTES * ALIGNMENT_BYTES;
}

template <typename Record> [[nodiscard]] Record ReadRecord(std::span<const std::uint8_t> _content, std::size_t _index) noexcept
{
  Record record{};
  std::memcpy(&record, _content.data() + _index * sizeof(Record), sizeof(Record));
  return record;
}

template <typename Record> void AppendRecord(std::vector<std::uint8_t>& _bytes, const Record& _record)
{
  const auto* first = reinterpret_cast<const std::uint8_t*>(&_record);
  _bytes.insert(_bytes.end(), first, first + sizeof(Record));
}

[[nodiscard]] bool IsFinite(float _value) noexcept
{
  return std::isfinite(_value);
}

[[nodiscard]] bool IsFinite(const std::array<float, 3>& _values) noexcept
{
  return std::ranges::all_of(_values, [](float _value) { return std::isfinite(_value); });
}

[[nodiscard]] bool IsFinite(const std::array<float, 4>& _values) noexcept
{
  return std::ranges::all_of(_values, [](float _value) { return std::isfinite(_value); });
}

// One segment of a name: 1 to 31 of [a-z0-9_] (§4.1).
[[nodiscard]] bool IsSegment(std::string_view _segment) noexcept
{
  return !_segment.empty() && _segment.size() <= NVF_MAX_SEGMENT_CHARS &&
         std::ranges::all_of(_segment,
                             [](char _char) { return (_char >= 'a' && _char <= 'z') || (_char >= '0' && _char <= '9') || _char == '_'; });
}

// Splits _name at _separator and asks each segment IsSegment; counts the segments.
[[nodiscard]] std::size_t CountSegments(std::string_view _name, char _separator) noexcept
{
  std::size_t count = 0;
  while (true)
  {
    const std::size_t end = _name.find(_separator);
    if (!IsSegment(_name.substr(0, end)))
    {
      return 0;
    }
    ++count;
    if (end == std::string_view::npos)
    {
      return count;
    }
    _name.remove_prefix(end + 1);
  }
}

// Well-formed UTF-8, as the Unicode Standard's Table 3-7 defines it: no overlong form, no surrogate, nothing above
// U+10FFFF. Python's strict decoder draws the same line.
[[nodiscard]] bool IsUtf8(std::span<const std::uint8_t> _bytes) noexcept
{
  std::size_t i = 0;
  while (i < _bytes.size())
  {
    const std::uint8_t lead = _bytes[i];
    if (lead < 0x80u)
    {
      ++i;
      continue;
    }
    // The range the byte after the lead must fall in, and how many continuation bytes follow the lead in all.
    std::uint8_t low = 0x80u;
    std::uint8_t high = 0xBFu;
    std::size_t continuations = 0;
    if (lead >= 0xC2u && lead <= 0xDFu)
    {
      continuations = 1;
    }
    else if (lead == 0xE0u)
    {
      low = 0xA0u;
      continuations = 2;
    }
    else if ((lead >= 0xE1u && lead <= 0xECu) || lead == 0xEEu || lead == 0xEFu)
    {
      continuations = 2;
    }
    else if (lead == 0xEDu)
    {
      high = 0x9Fu;
      continuations = 2;
    }
    else if (lead == 0xF0u)
    {
      low = 0x90u;
      continuations = 3;
    }
    else if (lead >= 0xF1u && lead <= 0xF3u)
    {
      continuations = 3;
    }
    else if (lead == 0xF4u)
    {
      high = 0x8Fu;
      continuations = 3;
    }
    else
    {
      return false;
    }
    if (_bytes.size() - i <= continuations)
    {
      return false;
    }
    for (std::size_t k = 1; k <= continuations; ++k)
    {
      const std::uint8_t next = _bytes[i + k];
      if (k == 1 ? (next < low || next > high) : (next < 0x80u || next > 0xBFu))
      {
        return false;
      }
    }
    i += continuations + 1;
  }
  return true;
}

// The string at _offset in STRS, whose content ends with a NUL: the offset must be a string's first byte.
[[nodiscard]] std::optional<std::string_view> StringAt(std::span<const std::uint8_t> _strings, std::uint32_t _offset) noexcept
{
  if (_offset >= _strings.size() || (_offset > 0 && _strings[_offset - 1] != 0))
  {
    return std::nullopt;
  }
  const auto* first = reinterpret_cast<const char*>(_strings.data()) + _offset;
  return std::string_view(first, std::strlen(first));
}

// Frames the file: the header, then chunkCount chunks, each a header, its content and zero padding to 16 bytes, and
// nothing after the last. Reports the first fault, in file order.
[[nodiscard]] std::expected<std::vector<ChunkSpan>, NvfError> FrameChunks(std::span<const std::uint8_t> _bytes)
{
  if (_bytes.size() < MAGIC.size() || std::memcmp(_bytes.data(), MAGIC.data(), MAGIC.size()) != 0)
  {
    return std::unexpected(NvfError::NotAnNvfFile);
  }
  if (_bytes.size() < sizeof(NvfFileHeader))
  {
    return std::unexpected(NvfError::Truncated);
  }
  const auto header = ReadRecord<NvfFileHeader>(_bytes, 0);
  if (header.versionMajor != NVF_VERSION_MAJOR)
  {
    return std::unexpected(NvfError::UnsupportedVersion);
  }
  if (header.reserved != 0)
  {
    return std::unexpected(NvfError::MalformedChunk);
  }

  std::vector<ChunkSpan> chunks;
  std::size_t offset = sizeof(NvfFileHeader);
  for (std::uint32_t i = 0; i < header.chunkCount; ++i)
  {
    if (_bytes.size() - offset < sizeof(NvfChunkHeader))
    {
      return std::unexpected(NvfError::Truncated);
    }
    const auto chunk = ReadRecord<NvfChunkHeader>(_bytes.subspan(offset), 0);
    const std::string_view id(reinterpret_cast<const char*>(_bytes.data()) + offset, chunk.id.size());
    offset += sizeof(NvfChunkHeader);
    const std::uint64_t padded = Padded(chunk.sizeBytes);
    if (_bytes.size() - offset < padded)
    {
      return std::unexpected(NvfError::Truncated);
    }
    if (chunk.reserved != 0)
    {
      return std::unexpected(NvfError::MalformedChunk);
    }
    const std::span<const std::uint8_t> padding = _bytes.subspan(offset + chunk.sizeBytes, padded - chunk.sizeBytes);
    if (std::ranges::any_of(padding, [](std::uint8_t _byte) { return _byte != 0; }))
    {
      return std::unexpected(NvfError::MalformedChunk);
    }
    chunks.push_back({id, chunk.elementCount, _bytes.subspan(offset, chunk.sizeBytes)});
    offset += padded;
  }
  if (offset != _bytes.size())
  {
    return std::unexpected(NvfError::MalformedChunk);
  }
  return chunks;
}

// Whether _chunk's size is _count records of _recordBytes.
[[nodiscard]] bool HoldsRecords(const ChunkSpan& _chunk, std::size_t _recordBytes) noexcept
{
  return _chunk.content.size() == std::uint64_t{_chunk.elementCount} * _recordBytes;
}

[[nodiscard]] Parsed Parse(std::span<const std::uint8_t> _bytes)
{
  const auto framed = FrameChunks(_bytes);
  if (!framed)
  {
    return std::unexpected(framed.error());
  }

  // The five known chunks, each once and in order; any other chunk is skipped, wherever it lies (§4.2, §4.6).
  NvfModel model{};
  std::array<const ChunkSpan*, KNOWN_CHUNK_COUNT> known{};
  std::size_t next = 0;
  for (const ChunkSpan& chunk : *framed)
  {
    const auto found = std::ranges::find(KNOWN_CHUNK_IDS, chunk.id);
    if (found == KNOWN_CHUNK_IDS.end())
    {
      model.unknownChunks.emplace_back(chunk.id);
      continue;
    }
    const auto index = static_cast<std::size_t>(found - KNOWN_CHUNK_IDS.begin());
    if (index < next)
    {
      return std::unexpected(NvfError::ChunkOutOfOrder);
    }
    known[index] = &chunk;
    next = index + 1;
  }
  if (std::ranges::any_of(known, [](const ChunkSpan* _chunk) { return _chunk == nullptr; }))
  {
    return std::unexpected(NvfError::MissingChunk);
  }

  // STRS: NUL-terminated UTF-8.
  const ChunkSpan& stringChunk = *known[static_cast<std::size_t>(KnownChunk::Strings)];
  if (stringChunk.elementCount != stringChunk.content.size() || stringChunk.content.size() > NVF_MAX_STRING_BYTES)
  {
    return std::unexpected(NvfError::MalformedChunk);
  }
  const std::span<const std::uint8_t> strings = stringChunk.content;
  if ((!strings.empty() && strings.back() != 0) || !IsUtf8(strings))
  {
    return std::unexpected(NvfError::BadString);
  }

  // PALT: sixteen entries.
  const ChunkSpan& paletteChunk = *known[static_cast<std::size_t>(KnownChunk::Palette)];
  if (paletteChunk.elementCount != PALETTE_ENTRY_COUNT || !HoldsRecords(paletteChunk, sizeof(NvfPaletteRecord)))
  {
    return std::unexpected(NvfError::MalformedChunk);
  }
  for (std::size_t i = 0; i < PALETTE_ENTRY_COUNT; ++i)
  {
    const auto record = ReadRecord<NvfPaletteRecord>(paletteChunk.content, i);
    if ((record.flags & ~PALETTE_EMISSIVE) != 0)
    {
      return std::unexpected(NvfError::MalformedChunk);
    }
    if (!IsFinite(record.emit) || !IsFinite(record.flux))
    {
      return std::unexpected(NvfError::NotFinite);
    }
    model.palette[i] = {record.rgba[0], record.rgba[1], record.rgba[2], record.rgba[3], (record.flags & PALETTE_EMISSIVE) != 0,
                        record.emit,    record.flux};
  }

  // PART: a tree whose parents come first, each part's voxels following the last part's.
  const ChunkSpan& partChunk = *known[static_cast<std::size_t>(KnownChunk::Parts)];
  if (!HoldsRecords(partChunk, sizeof(NvfPartRecord)) || partChunk.elementCount > NVF_MAX_PARTS)
  {
    return std::unexpected(NvfError::MalformedChunk);
  }
  if (partChunk.elementCount == 0)
  {
    return std::unexpected(NvfError::BadPartTree);
  }
  model.parts.reserve(partChunk.elementCount);
  std::vector<std::array<std::int64_t, 3>> origins;
  origins.reserve(partChunk.elementCount);
  std::set<std::string_view> paths;
  std::uint64_t nextVoxel = 0;
  for (std::uint32_t i = 0; i < partChunk.elementCount; ++i)
  {
    const auto record = ReadRecord<NvfPartRecord>(partChunk.content, i);
    const std::optional<std::string_view> path = StringAt(strings, record.nameOffset);
    if (!path || !IsNvfPartPath(*path))
    {
      return std::unexpected(NvfError::BadString);
    }
    const std::size_t lastSlash = path->rfind('/');
    if (i == 0)
    {
      if (record.parentIndex != NVF_NO_PARENT || lastSlash != std::string_view::npos)
      {
        return std::unexpected(NvfError::BadPartTree);
      }
    }
    else if (record.parentIndex >= i || lastSlash == std::string_view::npos ||
             path->substr(0, lastSlash) != model.parts[record.parentIndex].path)
    {
      return std::unexpected(NvfError::BadPartTree);
    }
    if (!paths.insert(*path).second)
    {
      return std::unexpected(NvfError::DuplicateName);
    }
    if (std::ranges::any_of(record.sizeVoxels, [](std::uint16_t _extent) { return _extent == 0; }))
    {
      return std::unexpected(NvfError::MalformedChunk);
    }
    if (std::ranges::any_of(record.sizeVoxels, [](std::uint16_t _extent) { return _extent > NVF_MAX_PART_EXTENT; }))
    {
      return std::unexpected(NvfError::PartTooLarge);
    }
    if ((record.flags & ~PART_PIVOT_AUTHORED) != 0)
    {
      return std::unexpected(NvfError::MalformedChunk);
    }
    std::array<std::int64_t, 3> origin{};
    for (std::size_t axis = 0; axis < origin.size(); ++axis)
    {
      origin[axis] = (i == 0 ? 0 : origins[record.parentIndex][axis]) + record.translation[axis];
    }
    if (std::ranges::any_of(origin, [](std::int64_t _value) { return _value < -NVF_MAX_PART_ORIGIN || _value > NVF_MAX_PART_ORIGIN; }))
    {
      return std::unexpected(NvfError::TranslationOutOfRange);
    }
    origins.push_back(origin);
    if (!IsFinite(record.pivot))
    {
      return std::unexpected(NvfError::NotFinite);
    }
    if (record.firstVoxel != nextVoxel || record.voxelCount == 0)
    {
      return std::unexpected(NvfError::BadVoxelRange);
    }
    nextVoxel += record.voxelCount;
    model.parts.push_back({.path = std::string(*path),
                           .parent = record.parentIndex,
                           .size = {record.sizeVoxels[0], record.sizeVoxels[1], record.sizeVoxels[2]},
                           .translation = {record.translation[0], record.translation[1], record.translation[2]},
                           .pivot = {record.pivot[0], record.pivot[1], record.pivot[2]},
                           .pivotAuthored = (record.flags & PART_PIVOT_AUTHORED) != 0,
                           .firstVoxel = record.firstVoxel,
                           .voxelCount = record.voxelCount});
  }

  // VOXL: every part's records, inside its size, no position twice within a part.
  const ChunkSpan& voxelChunk = *known[static_cast<std::size_t>(KnownChunk::Voxels)];
  if (!HoldsRecords(voxelChunk, sizeof(std::uint32_t)))
  {
    return std::unexpected(NvfError::MalformedChunk);
  }
  if (voxelChunk.elementCount != nextVoxel)
  {
    return std::unexpected(NvfError::BadVoxelRange);
  }
  model.records.resize(voxelChunk.elementCount);
  std::memcpy(model.records.data(), voxelChunk.content.data(), voxelChunk.content.size());
  std::vector<std::uint32_t> positions;
  for (const NvfPart& part : model.parts)
  {
    const std::span<const std::uint32_t> records = std::span(model.records).subspan(part.firstVoxel, part.voxelCount);
    if (std::ranges::any_of(records, [](std::uint32_t _record) { return (_record & RESERVED_RECORD_BITS) != 0; }))
    {
      return std::unexpected(NvfError::ReservedBitsSet);
    }
    const auto outside = [&part](std::uint32_t _record)
    {
      const VoxelRecord voxel = UnpackVoxelRecord(_record);
      return voxel.x >= part.size.x || voxel.y >= part.size.y || voxel.z >= part.size.z;
    };
    if (std::ranges::any_of(records, outside))
    {
      return std::unexpected(NvfError::VoxelOutOfBounds);
    }
    // Bits 0-23 are the position, so two records with equal low bits share a voxel.
    positions.assign(records.begin(), records.end());
    for (std::uint32_t& position : positions)
    {
      position &= 0x00FFFFFFu;
    }
    std::ranges::sort(positions);
    if (std::ranges::adjacent_find(positions) != positions.end())
    {
      return std::unexpected(NvfError::DuplicateVoxel);
    }
  }

  // HPNT: named points on parts.
  const ChunkSpan& hardpointChunk = *known[static_cast<std::size_t>(KnownChunk::Hardpoints)];
  if (!HoldsRecords(hardpointChunk, sizeof(NvfHardpointRecord)) || hardpointChunk.elementCount > NVF_MAX_HARDPOINTS)
  {
    return std::unexpected(NvfError::MalformedChunk);
  }
  model.hardpoints.reserve(hardpointChunk.elementCount);
  std::set<std::string_view> names;
  for (std::uint32_t i = 0; i < hardpointChunk.elementCount; ++i)
  {
    const auto record = ReadRecord<NvfHardpointRecord>(hardpointChunk.content, i);
    const std::optional<std::string_view> name = StringAt(strings, record.nameOffset);
    if (!name || !IsNvfHardpointName(*name))
    {
      return std::unexpected(NvfError::BadString);
    }
    if (!names.insert(*name).second)
    {
      return std::unexpected(NvfError::DuplicateName);
    }
    if (record.partIndex >= model.parts.size())
    {
      return std::unexpected(NvfError::BadHardpointPart);
    }
    if ((record.flags & ~HARDPOINT_FROM_VOX) != 0 || record.reserved[0] != 0 || record.reserved[1] != 0)
    {
      return std::unexpected(NvfError::MalformedChunk);
    }
    if (!IsFinite(record.position) || !IsFinite(record.rotation))
    {
      return std::unexpected(NvfError::NotFinite);
    }
    const Quaternion rotation{record.rotation[0], record.rotation[1], record.rotation[2], record.rotation[3]};
    if (!IsUnitRotation(rotation))
    {
      return std::unexpected(NvfError::NotUnitRotation);
    }
    model.hardpoints.push_back({.name = std::string(*name),
                                .part = record.partIndex,
                                .position = {record.position[0], record.position[1], record.position[2]},
                                .rotation = rotation,
                                .fromVox = (record.flags & HARDPOINT_FROM_VOX) != 0});
  }
  return model;
}

void AppendChunk(std::vector<std::uint8_t>& _bytes, std::string_view _id, std::uint32_t _elementCount,
                 std::span<const std::uint8_t> _content)
{
  NvfChunkHeader header{};
  std::copy_n(_id.begin(), header.id.size(), header.id.begin());
  header.sizeBytes = static_cast<std::uint32_t>(_content.size());
  header.elementCount = _elementCount;
  AppendRecord(_bytes, header);
  _bytes.insert(_bytes.end(), _content.begin(), _content.end());
  _bytes.resize(Padded(_bytes.size()), 0);
}

} // namespace

const char* NvfErrorName(NvfError _error) noexcept
{
  switch (_error)
  {
  case NvfError::FileNotFound:
    return "FileNotFound";
  case NvfError::ReadFailed:
    return "ReadFailed";
  case NvfError::WriteFailed:
    return "WriteFailed";
  case NvfError::NotAnNvfFile:
    return "NotAnNvfFile";
  case NvfError::UnsupportedVersion:
    return "UnsupportedVersion";
  case NvfError::Truncated:
    return "Truncated";
  case NvfError::MalformedChunk:
    return "MalformedChunk";
  case NvfError::MissingChunk:
    return "MissingChunk";
  case NvfError::ChunkOutOfOrder:
    return "ChunkOutOfOrder";
  case NvfError::BadString:
    return "BadString";
  case NvfError::DuplicateName:
    return "DuplicateName";
  case NvfError::BadPartTree:
    return "BadPartTree";
  case NvfError::PartTooLarge:
    return "PartTooLarge";
  case NvfError::TranslationOutOfRange:
    return "TranslationOutOfRange";
  case NvfError::BadVoxelRange:
    return "BadVoxelRange";
  case NvfError::VoxelOutOfBounds:
    return "VoxelOutOfBounds";
  case NvfError::DuplicateVoxel:
    return "DuplicateVoxel";
  case NvfError::ReservedBitsSet:
    return "ReservedBitsSet";
  case NvfError::BadHardpointPart:
    return "BadHardpointPart";
  case NvfError::NotFinite:
    return "NotFinite";
  case NvfError::NotUnitRotation:
    return "NotUnitRotation";
  }
  return "Unknown";
}

std::string_view HardpointType(const NvfHardpoint& _hardpoint) noexcept
{
  const std::string_view name = _hardpoint.name;
  return name.substr(0, name.find('.'));
}

bool IsNvfPartPath(std::string_view _path) noexcept
{
  return CountSegments(_path, '/') >= 1;
}

bool IsNvfHardpointName(std::string_view _name) noexcept
{
  return CountSegments(_name, '.') >= 2 && _name.substr(0, _name.find('.')) != PIVOT_TYPE;
}

std::expected<NvfModel, NvfError> ParseNvfModel(std::span<const std::uint8_t> _bytes)
{
  return Parse(_bytes);
}

std::expected<NvfModel, NvfError> LoadNvfModel(const std::filesystem::path& _path)
{
  std::ifstream file(_path, std::ios::binary | std::ios::ate);
  if (!file.is_open())
  {
    return std::unexpected(NvfError::FileNotFound);
  }
  const std::streamoff size = file.tellg();
  if (size < 0)
  {
    return std::unexpected(NvfError::ReadFailed);
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
  {
    return std::unexpected(NvfError::ReadFailed);
  }
  return ParseNvfModel(bytes);
}

std::expected<std::vector<std::uint8_t>, NvfError> SerializeNvfModel(const NvfModel& _model)
{
  // A size the file's 16 bits cannot hold would wrap as it narrows; refuse it first, as the reader would the wider value.
  for (const NvfPart& part : _model.parts)
  {
    for (const std::int32_t extent : {part.size.x, part.size.y, part.size.z})
    {
      if (extent < 1)
      {
        return std::unexpected(NvfError::MalformedChunk);
      }
      if (extent > NVF_MAX_PART_EXTENT)
      {
        return std::unexpected(NvfError::PartTooLarge);
      }
    }
  }
  if (_model.parts.size() > NVF_MAX_PARTS || _model.hardpoints.size() > NVF_MAX_HARDPOINTS ||
      _model.records.size() > std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint32_t))
  {
    return std::unexpected(NvfError::MalformedChunk);
  }

  // Hardpoints in name order, so that whichever tool wrote the table, the bytes are the same (§4.3, ADR-019).
  std::vector<const NvfHardpoint*> hardpoints;
  hardpoints.reserve(_model.hardpoints.size());
  for (const NvfHardpoint& hardpoint : _model.hardpoints)
  {
    hardpoints.push_back(&hardpoint);
  }
  std::ranges::stable_sort(hardpoints, [](const NvfHardpoint* _a, const NvfHardpoint* _b) { return _a->name < _b->name; });

  // Strings in first-use order, each once: part paths in part order, then hardpoint names in hardpoint order.
  std::vector<std::uint8_t> strings;
  std::map<std::string_view, std::uint32_t, std::less<>> offsets;
  const auto offsetOf = [&strings, &offsets](std::string_view _name)
  {
    const auto [found, inserted] = offsets.try_emplace(_name, static_cast<std::uint32_t>(strings.size()));
    if (inserted)
    {
      strings.insert(strings.end(), _name.begin(), _name.end());
      strings.push_back(0);
    }
    return found->second;
  };

  std::vector<std::uint8_t> parts;
  parts.reserve(_model.parts.size() * sizeof(NvfPartRecord));
  for (const NvfPart& part : _model.parts)
  {
    NvfPartRecord record{};
    record.nameOffset = offsetOf(part.path);
    record.parentIndex = part.parent;
    record.sizeVoxels = {static_cast<std::uint16_t>(part.size.x), static_cast<std::uint16_t>(part.size.y),
                         static_cast<std::uint16_t>(part.size.z)};
    record.flags = part.pivotAuthored ? PART_PIVOT_AUTHORED : std::uint16_t{0};
    record.translation = {part.translation.x, part.translation.y, part.translation.z};
    record.pivot = {part.pivot.x, part.pivot.y, part.pivot.z};
    record.firstVoxel = part.firstVoxel;
    record.voxelCount = part.voxelCount;
    AppendRecord(parts, record);
  }

  std::vector<std::uint8_t> hardpointBytes;
  hardpointBytes.reserve(hardpoints.size() * sizeof(NvfHardpointRecord));
  for (const NvfHardpoint* hardpoint : hardpoints)
  {
    NvfHardpointRecord record{};
    record.nameOffset = offsetOf(hardpoint->name);
    record.partIndex = hardpoint->part;
    record.flags = hardpoint->fromVox ? HARDPOINT_FROM_VOX : 0u;
    record.position = {hardpoint->position.x, hardpoint->position.y, hardpoint->position.z};
    record.rotation = {hardpoint->rotation.x, hardpoint->rotation.y, hardpoint->rotation.z, hardpoint->rotation.w};
    AppendRecord(hardpointBytes, record);
  }

  std::vector<std::uint8_t> palette;
  palette.reserve(PALETTE_ENTRY_COUNT * sizeof(NvfPaletteRecord));
  for (const PaletteEntry& entry : _model.palette)
  {
    AppendRecord(palette, NvfPaletteRecord{.rgba = {entry.red, entry.green, entry.blue, entry.alpha},
                                           .flags = entry.emissive ? PALETTE_EMISSIVE : 0u,
                                           .emit = entry.emit,
                                           .flux = entry.flux});
  }

  std::vector<std::uint8_t> bytes;
  NvfFileHeader header{};
  header.magic = MAGIC;
  header.versionMajor = NVF_VERSION_MAJOR;
  header.versionMinor = NVF_VERSION_MINOR;
  header.chunkCount = static_cast<std::uint32_t>(KNOWN_CHUNK_COUNT);
  AppendRecord(bytes, header);
  AppendChunk(bytes, KNOWN_CHUNK_IDS[0], static_cast<std::uint32_t>(strings.size()), strings);
  AppendChunk(bytes, KNOWN_CHUNK_IDS[1], PALETTE_ENTRY_COUNT, palette);
  AppendChunk(bytes, KNOWN_CHUNK_IDS[2], static_cast<std::uint32_t>(_model.parts.size()), parts);
  const std::span<const std::uint8_t> records(reinterpret_cast<const std::uint8_t*>(_model.records.data()),
                                              _model.records.size() * sizeof(std::uint32_t));
  AppendChunk(bytes, KNOWN_CHUNK_IDS[3], static_cast<std::uint32_t>(_model.records.size()), records);
  AppendChunk(bytes, KNOWN_CHUNK_IDS[4], static_cast<std::uint32_t>(hardpoints.size()), hardpointBytes);

  // Whatever the writer emits, the reader must accept; a model it would refuse is refused here, by the same name.
  if (const Parsed check = Parse(bytes); !check)
  {
    return std::unexpected(check.error());
  }
  return bytes;
}

std::expected<void, NvfError> SaveNvfModel(const NvfModel& _model, const std::filesystem::path& _path)
{
  const auto bytes = SerializeNvfModel(_model);
  if (!bytes)
  {
    return std::unexpected(bytes.error());
  }
  std::filesystem::path temporary = _path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    file.close();
    if (!file)
    {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return std::unexpected(NvfError::WriteFailed);
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, _path, error);
  if (error)
  {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return std::unexpected(NvfError::WriteFailed);
  }
  return {};
}

} // namespace NeuronCore
