#pragma once

#include "NvfModel.h"
#include "Quaternion.h"
#include "RigidTransform.h"
#include "VoxModel.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace NeuronCore
{

// Why a .vox could not become an .nvf (Design/Archive/NeuronVoxelFormat.md §5, §6.2, Design/ADR/ADR-019). Each refusal names
// the node, or for a merge the hardpoint, that it is about.
enum class NvfImportRefusal : std::uint8_t
{
  MalformedName,     // a node name that is not a part path, <part>@<hardpoint> or <part>@pivot (§5)
  UnnamedPart,       // an unnamed model beside another part: `main` names a file's only part, and no other
  NoPart,            // no part at all
  SecondRoot,        // a second part of one segment: part 0 is the only root (§4.3)
  MissingPart,       // a part whose parent path names no part, or a marker whose part does not exist
  DuplicateName,     // two parts of one path, two markers of one hardpoint name, or two pivot markers on one part
  RotatedPart,       // a part turned in MagicaVoxel: parts are never rotated at rest (N2)
  EvenMarker,        // a marker even in some dimension, whose centre voxel is not its middle (§5)
  TooManyParts,      // more than NVF_MAX_PARTS
  TooManyHardpoints, // more than NVF_MAX_HARDPOINTS, markers and carried-over hardpoints together
  UnknownChunks,     // the previous .nvf holds chunks this version does not know, which a rewrite would lose (§4.6)
  OrphanHardpoint,   // a hardpoint authored in Blender whose part is gone
  NameClash          // a hardpoint authored in Blender shares a marker's name: Blender owns it, so the marker goes
};

[[nodiscard]] const char* NvfImportRefusalName(NvfImportRefusal _refusal) noexcept;

// One refusal, and the node or hardpoint it names.
struct NvfImportError
{
  NvfImportRefusal refusal;
  std::string node;
};

// The .vox _vox as an NVF model (§6.2). Models become parts, and markers become hardpoints and pivots, by their node
// names (§5). Parts are ordered by path, which puts every parent before its children, and each keeps its records in the
// order the .vox gave them. A part's default pivot is its geometric centre, size / 2.
//
// With a previous version of the file, every hardpoint authored in Blender is carried over to its part, found by path,
// and every hardpoint that came from a marker is dropped for the markers the .vox has now. A pivot set in Blender
// survives unless a marker now sets it, and goes with its part if the part is gone.
//
// Returns every refusal it finds, in the order of the .vox's models, so that an artist can fix a file in one pass.
[[nodiscard]] std::expected<NvfModel, std::vector<NvfImportError>> ImportVoxModel(const VoxModel& _vox, const NvfModel* _previous);

// _model as text, for review (§3, §6.3): its palette, then each part with its parent, size, translation, pivot and
// voxels, then each hardpoint with its type, part, position and rotation, one line each and in file order. Floats are
// written as their shortest exact decimals, so that two dumps differ wherever the files do.
[[nodiscard]] std::string DumpNvfModel(const NvfModel& _model);

// The quaternion of _rotation from a fixed table of the cube's 24 rotations, so that a marker's orientation is exact and
// the same on every run (§6.2): components of 0, ±½, ±√½ rounded to float, and ±1, with w > 0, or for a half turn w = 0
// and the first nonzero of x, y and z positive. Nothing for a rotation that is not one of the 24.
[[nodiscard]] std::optional<Quaternion> CubeRotationQuaternion(const Rotation& _rotation) noexcept;

} // namespace NeuronCore
