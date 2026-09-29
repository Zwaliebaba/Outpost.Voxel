#pragma once

#include "Float3.h"
#include "Quaternion.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NeuronCore
{

// The Neuron Voxel Format, the game's own voxel model format (Design/Archive/NeuronVoxelFormat.md §4, Design/ADR/ADR-019). A
// model is a tree of rigid parts, each a voxel grid in the R14 record, sharing one 16-entry palette, plus named
// hardpoints. Model space is the engine's, Direct3D's: left-handed, +Y up, +Z forward, one unit per voxel edge (N9).
// This is the C++ implementation; the Blender extension's NvfFormat.py is the other, and Tools/Golden/Golden.nvf holds
// the two to the same bytes (N7).

inline constexpr std::uint16_t NVF_VERSION_MAJOR = 1;
inline constexpr std::uint16_t NVF_VERSION_MINOR = 0;

// What a part's parent index holds for part 0, the root (§4.3).
inline constexpr std::uint32_t NVF_NO_PARENT = 0xFFFFFFFFu;

// Limits, so that a bad file fails fast rather than allocating (§4.4).
inline constexpr std::uint32_t NVF_MAX_PARTS = 1024;
inline constexpr std::uint32_t NVF_MAX_HARDPOINTS = 4096;
inline constexpr std::uint32_t NVF_MAX_STRING_BYTES = 64 * 1024;
inline constexpr std::int32_t NVF_MAX_PART_EXTENT = 256;

// How far a part's origin may lie from model space's on any axis: the sum of the translations from part 0 down to it.
// A .vox the reader accepts places every model within MAX_TRANSLATION + 128 of the origin, so this admits every import,
// keeps every voxel centre exact in single precision, and keeps any sum of translations inside int32 (ADR-019).
inline constexpr std::int32_t NVF_MAX_PART_ORIGIN = 1 << 21;

// A name's segments are 1 to this many of [a-z0-9_] (§4.1).
inline constexpr std::size_t NVF_MAX_SEGMENT_CHARS = 31;

// The records as they lie in the file: little-endian, and every chunk's content 16-byte aligned (§4.2, §4.3). Each
// struct is the truth for its record, and the static_asserts below hold it to the offsets §4 gives, as R16 holds a
// layout shared with HLSL.
struct NvfFileHeader
{
  std::array<char, 4> magic; // "NVF "
  std::uint16_t versionMajor;
  std::uint16_t versionMinor;
  std::uint32_t chunkCount; // chunks after the header
  std::uint32_t reserved;
};

struct NvfChunkHeader
{
  std::array<char, 4> id;
  std::uint32_t sizeBytes;    // the content only, without the padding to the next multiple of 16
  std::uint32_t elementCount; // records in the content; for STRS, its byte count
  std::uint32_t reserved;
};

struct NvfPaletteRecord
{
  std::array<std::uint8_t, 4> rgba; // the file's sRGB bytes and alpha
  std::uint32_t flags;              // bit 0: emissive
  float emit;
  float flux;
};

struct NvfPartRecord
{
  std::uint32_t nameOffset;                // the part's full path, in STRS
  std::uint32_t parentIndex;               // NVF_NO_PARENT for part 0; otherwise below this part's index
  std::array<std::uint16_t, 3> sizeVoxels; // 1 to 256 on each axis
  std::uint16_t flags;                     // bit 0: PivotAuthored
  std::array<std::int32_t, 3> translation; // this part's space in its parent's; part 0's in model space
  std::array<float, 3> pivot;              // in part space
  std::uint32_t firstVoxel;
  std::uint32_t voxelCount;
};

struct NvfHardpointRecord
{
  std::uint32_t nameOffset;
  std::uint32_t partIndex;
  std::uint32_t flags;           // bit 0: FromVox
  std::array<float, 3> position; // in part space, in voxels
  std::array<float, 4> rotation; // x, y, z, w: from the hardpoint's frame to part space, unit, w >= 0
  std::array<std::uint32_t, 2> reserved;
};

static_assert(sizeof(NvfFileHeader) == 16);
static_assert(offsetof(NvfFileHeader, magic) == 0);
static_assert(offsetof(NvfFileHeader, versionMajor) == 4);
static_assert(offsetof(NvfFileHeader, versionMinor) == 6);
static_assert(offsetof(NvfFileHeader, chunkCount) == 8);
static_assert(offsetof(NvfFileHeader, reserved) == 12);

static_assert(sizeof(NvfChunkHeader) == 16);
static_assert(offsetof(NvfChunkHeader, id) == 0);
static_assert(offsetof(NvfChunkHeader, sizeBytes) == 4);
static_assert(offsetof(NvfChunkHeader, elementCount) == 8);
static_assert(offsetof(NvfChunkHeader, reserved) == 12);

static_assert(sizeof(NvfPaletteRecord) == 16);
static_assert(offsetof(NvfPaletteRecord, rgba) == 0);
static_assert(offsetof(NvfPaletteRecord, flags) == 4);
static_assert(offsetof(NvfPaletteRecord, emit) == 8);
static_assert(offsetof(NvfPaletteRecord, flux) == 12);

static_assert(sizeof(NvfPartRecord) == 48);
static_assert(offsetof(NvfPartRecord, nameOffset) == 0);
static_assert(offsetof(NvfPartRecord, parentIndex) == 4);
static_assert(offsetof(NvfPartRecord, sizeVoxels) == 8);
static_assert(offsetof(NvfPartRecord, flags) == 14);
static_assert(offsetof(NvfPartRecord, translation) == 16);
static_assert(offsetof(NvfPartRecord, pivot) == 28);
static_assert(offsetof(NvfPartRecord, firstVoxel) == 40);
static_assert(offsetof(NvfPartRecord, voxelCount) == 44);

static_assert(sizeof(NvfHardpointRecord) == 48);
static_assert(offsetof(NvfHardpointRecord, nameOffset) == 0);
static_assert(offsetof(NvfHardpointRecord, partIndex) == 4);
static_assert(offsetof(NvfHardpointRecord, flags) == 8);
static_assert(offsetof(NvfHardpointRecord, position) == 12);
static_assert(offsetof(NvfHardpointRecord, rotation) == 24);
static_assert(offsetof(NvfHardpointRecord, reserved) == 40);

// Why an .nvf file was refused (§4.4). The reader refuses by name, and NvfFormat.py raises the same names, in the same
// order of checking, so that both implementations name the same fault in the same file (ADR-019).
enum class NvfError : std::uint8_t
{
  FileNotFound,
  ReadFailed,
  WriteFailed,
  NotAnNvfFile,
  UnsupportedVersion,
  Truncated,
  MalformedChunk,
  MissingChunk,
  ChunkOutOfOrder,
  BadString,
  DuplicateName,
  BadPartTree,
  PartTooLarge,
  TranslationOutOfRange,
  BadVoxelRange,
  VoxelOutOfBounds,
  DuplicateVoxel,
  ReservedBitsSet,
  BadHardpointPart,
  NotFinite,
  NotUnitRotation
};

[[nodiscard]] const char* NvfErrorName(NvfError _error) noexcept;

// One rigid part: a voxel grid, placed by a translation in its parent's space and never rotated at rest (N2). Part
// space's origin is the minimum corner of voxel (0, 0, 0), with model space's axes (§4.1).
struct NvfPart
{
  std::string path;         // segments joined by '/', its parent's path plus one segment, e.g. hull/turret
  std::uint32_t parent;     // NVF_NO_PARENT for part 0, otherwise a part before this one
  Int3 size;                // in voxels, 1 to 256 on each axis
  Int3 translation;         // this part's space in its parent's; part 0's in model space
  Float3 pivot;             // in part space: where the game turns the part about
  bool pivotAuthored;       // the pivot was set in Blender, and a re-import keeps it (§6.2)
  std::uint32_t firstVoxel; // this part's records in NvfModel::records
  std::uint32_t voxelCount; // at least 1
};

// A named point with a free orientation where something is mounted (N3). Its frame's +Z points out of the mount, +Y is
// up and +X right (§4.1).
struct NvfHardpoint
{
  std::string name;    // two or more segments joined by '.', the first its type, e.g. engine.main
  std::uint32_t part;  // the part it is mounted on
  Float3 position;     // in its part's space, in voxels
  Quaternion rotation; // from its frame to its part's space: unit, w >= 0
  bool fromVox;        // it came from a marker in the .vox, which a re-import replaces (§6.2)
};

// A whole model, as the reader returns it and the writer takes it.
struct NvfModel
{
  std::array<PaletteEntry, PALETTE_ENTRY_COUNT> palette;
  std::vector<NvfPart> parts;           // part 0 first, every parent before its children
  std::vector<std::uint32_t> records;   // R14, part after part, each part's in the order the .vox gave them
  std::vector<NvfHardpoint> hardpoints; // the writer stores them in name order
  // The ids of the chunks this reader skipped, in file order. A tool that rewrites a file refuses a model that has any,
  // since a newer chunk may refer to parts or hardpoints by index (§4.6).
  std::vector<std::string> unknownChunks;
};

// A hardpoint's type: its name's first segment (§4.3). The type is not stored apart from the name, so the two cannot
// disagree.
[[nodiscard]] std::string_view HardpointType(const NvfHardpoint& _hardpoint) noexcept;

// Whether _path is a part path, and _name a hardpoint name, as §4.1 spells them.
[[nodiscard]] bool IsNvfPartPath(std::string_view _path) noexcept;
[[nodiscard]] bool IsNvfHardpointName(std::string_view _name) noexcept;

// Reads and validates a whole file. Accepts any minor version of major version 1 and skips chunks it does not know,
// wherever they lie; refuses everything else §4.4 names, checking in the order ADR-019 fixes and reporting the first
// fault.
[[nodiscard]] std::expected<NvfModel, NvfError> ParseNvfModel(std::span<const std::uint8_t> _bytes);

// An .nvf file's bytes, whole: what ParseNvfModel reads, and what a welcome's manifest hashes (Design/ADR/ADR-028).
[[nodiscard]] std::expected<std::vector<std::uint8_t>, NvfError> ReadNvfFile(const std::filesystem::path& _path);

[[nodiscard]] std::expected<NvfModel, NvfError> LoadNvfModel(const std::filesystem::path& _path);

// _model, which NVF's reader accepts, as the renderer and the server's measures take a model (Design/ADR/ADR-028): each
// part an unturned instance named by its path, at its origin in model space, which is the sum of the translations from
// part 0 down to it, with its records in the part's order; the palette as it is; version 0 and no render objects, since
// it came from no .vox. Hardpoints are the game's, and stay behind. For a model NvfImport made from a .vox, it gives
// ParseVoxModel's model of that .vox, markers aside, part for instance in path order.
[[nodiscard]] VoxModel FlattenNvfModel(const NvfModel& _model);

// The bytes of _model, as version 1.0 lays them out: the five chunks in order, hardpoints in name order, and strings in
// first-use order, so that every writer produces the same bytes for the same model (§4.3). Refuses, with the reader's
// name, a model the reader would refuse, so that nothing it writes fails to read back. Ignores unknownChunks.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, NvfError> SerializeNvfModel(const NvfModel& _model);

// Writes _model to a temporary file beside _path and renames it over _path, so that a failure never leaves half a
// file behind.
[[nodiscard]] std::expected<void, NvfError> SaveNvfModel(const NvfModel& _model, const std::filesystem::path& _path);

} // namespace NeuronCore
