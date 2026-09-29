#pragma once

#include "Box.h"
#include "Float3.h"
#include "RigidTransform.h"
#include "VoxelRecord.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace NeuronCore
{

// How far a translation may take a model from the origin on any axis. Far beyond any real scene, and near enough that
// every voxel centre, an integer plus a half, is exact in single precision.
inline constexpr std::int32_t MAX_TRANSLATION = 1 << 20;

// Why a .vox file was refused (Design/Archive/SampleRenderer.md §7.1). The reader refuses by name anything the design has no
// tested answer for, rather than guessing at it.
enum class VoxError : std::uint8_t
{
  FileNotFound,
  ReadFailed,
  NotAVoxFile,
  UnsupportedVersion,
  Truncated,
  MalformedChunk,
  ModelTooLarge,
  VoxelOutOfBounds,
  DuplicateVoxel,
  ColorOutOfRange,
  MissingPalette,
  MissingSceneGraph,
  BadSceneGraph,
  UnsupportedRotation,
  UnsupportedAnimation,
  TranslationOutOfRange
};

[[nodiscard]] const char* VoxErrorName(VoxError _error) noexcept;

// A dictionary as the format stores one: string keys to string values.
using VoxAttributes = std::map<std::string, std::string, std::less<>>;

// One palette entry: the file's sRGB bytes, and its material (§7.2).
struct PaletteEntry
{
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
  std::uint8_t alpha;
  bool emissive; // the material's _type is _emit
  float emit;    // the material's _emit, 0 when absent
  float flux;    // the material's _flux, 0 when absent
};

// One placed model: a range of the record buffer, and where it sits (§3, §7.5). A model turns about its centre voxel,
// the voxel floor(size / 2), which stays in the cell origin + floor(size / 2) however it is turned: voxel v lies in the
// cell origin + floor(size / 2) + rotation(v - floor(size / 2)). Only a model odd in every dimension can be turned, since
// only then is its centre voxel its middle (Design/Archive/NeuronVoxelFormat.md §5, §6.1).
struct ModelInstance
{
  Int3 origin; // world position of the minimum corner of voxel (0, 0, 0) as the model lies unturned: the translation minus
               // floor(size / 2)
  Int3 size;   // the model's SIZE, in voxels
  std::uint32_t firstRecord;
  std::uint32_t recordCount;
  Rotation rotation = IDENTITY_ROTATION; // one of the cube's 24 rotations, in the engine's axes
  std::string name;                      // the _name of the transform node that places it; empty when it has none
};

// A validated MagicaVoxel scene, flattened into what the renderer draws. Its voxels, sizes and origins are in the
// engine's axes, Direct3D's, left-handed with +Y up (Design/Archive/NeuronVoxelFormat.md §12): the reader swaps MagicaVoxel's
// y and z as it reads, and keeps the records in the file's order.
struct VoxModel
{
  std::int32_t version;
  std::vector<std::uint32_t> records; // R14, instance after instance
  std::vector<ModelInstance> instances;
  std::array<PaletteEntry, PALETTE_ENTRY_COUNT> palette;
  std::vector<VoxAttributes> renderObjects; // the rOBJ chunks, verbatim: the lighting interprets them
};

// Accepts versions 150 and 200, skips chunks it does not know, follows the scene graph from node 0, applies
// translations, keeps the name of the node that places each model, and skips hidden nodes and layers. Accepts a
// rotation other than the identity only on the node that places a model odd in every dimension, and turns it into the
// engine's axes; refuses any other, and a reflection. Refuses more than one frame or model per node, a color entry
// outside 1-16, a model larger than 256, a voxel outside its model or on top of another, a translation that takes a
// model further than MAX_TRANSLATION from the origin, and any size or count that disagrees with its chunk.
[[nodiscard]] std::expected<VoxModel, VoxError> ParseVoxModel(std::span<const std::uint8_t> _bytes);

// A .vox file's bytes, whole: what ParseVoxModel reads, and what a welcome's manifest hashes (Design/Archive/SpaceScene.md §6.2).
[[nodiscard]] std::expected<std::vector<std::uint8_t>, VoxError> ReadVoxFile(const std::filesystem::path& _path);

[[nodiscard]] std::expected<VoxModel, VoxError> LoadVoxModel(const std::filesystem::path& _path);

// The axis-aligned unit box whose minimum corner is _minCorner: how every voxel of an intact model is drawn.
[[nodiscard]] Box CellBox(Int3 _minCorner) noexcept;

// The box that _record, a packed record of _instance, is drawn as while the model is intact. _instance is unturned: the
// renderer draws no turned instance (Design/Archive/NeuronVoxelFormat.md §6.1).
[[nodiscard]] Box VoxelBox(const ModelInstance& _instance, std::uint32_t _record) noexcept;

// The box around every voxel of a model, in the model's own space: each voxel is the unit cell at its minimum corner, its
// part's origin and turn included. An entity stands where the middle of its model's box is (Design/Archive/SpaceScene.md §5.1), so the
// server that places it and the client that draws it measure the box through this one function.
struct VoxelBounds
{
  Int3 lower;
  Int3 upper; // one past the last cell on each axis
};

// _model's VoxelBounds, or nothing when it holds no voxel.
[[nodiscard]] std::optional<VoxelBounds> OccupiedBounds(const VoxModel& _model) noexcept;

} // namespace NeuronCore
