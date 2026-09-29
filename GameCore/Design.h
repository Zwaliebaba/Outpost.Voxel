#pragma once

#include "Catalogue.h"

#include "Float3.h"
#include "NvfModel.h"
#include "RigidTransform.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace GameCore
{

// Why a design was refused (Design/GameConcept.md §5.5), in the order ValidateDesign checks: the hull's file, each
// mount's hardpoint, the command mount, the size class, the hull's voxels, each mount's box, and then the fit. The
// server refuses a design by these names whenever one enters a match, since it never trusts a client.
enum class DesignRefusal : std::uint8_t
{
  HullUnreadable,    // the hull's file is missing, or NVF's reader refused it
  BadMountName,      // a hardpoint not named <type>.<size>.<label>, with a type and a size the catalogue knows (ADR-025)
  MountOffCenter,    // a mount's position is not a voxel's center
  MountOffAxis,      // a mount's turn is not one of the cube's 24
  NoCommandMount,    // no mount for a command module
  ExtraCommandMount, // more than one
  NoSizeClass,       // no class of the design's kind holds its hull and its mounts' boxes, and its voxels (G40)
  PartsOverlap,      // two parts put a voxel in one cell (G37)
  NotOnePiece,       // the hull's voxels are not one piece, face to face
  MountHoldsHull,    // a mount's box holds a hull voxel (G37)
  MountsOverlap,     // two mounts' boxes share a cell (G37)
  MountDetached,     // a mount's box shares no face with a hull voxel (G37)
  UnknownMount,      // the fit names a mount the hull lacks
  MountFittedTwice,  // the fit names a mount twice
  UnknownModule,     // the fit names a module the catalogue lacks
  WrongModule,       // a module whose kind or size is not its mount's
  UnfittedMount,     // a mount the fit leaves empty: a fit is the module at each mount (G12)
  PowerShort         // the modules draw more power than the reactors supply
};

[[nodiscard]] const char* DesignRefusalName(DesignRefusal _refusal) noexcept;

struct DesignError
{
  DesignRefusal refusal;
  std::string detail; // which design, mount or module, and where
};

// One hull voxel in the model's space: its cell, and its record's color, which is its palette entry minus one (R14).
struct HullVoxel
{
  NeuronCore::Int3 cell;
  std::uint8_t color;
};

// A mount as the hull places it, and the module the fit puts there.
struct Mount
{
  std::string name; // its hardpoint's, e.g. weapon.s.port
  ModuleKind kind;
  MountSize size;
  NeuronCore::Int3 centerCell; // the cell at its box's center, in the model's space
  NeuronCore::Rotation turn;   // from its frame to the model's: one of the cube's 24, exactly
  bool lineClear;              // for a module that works along a line, whether that line leaves the hull clear (G38)
  const ModuleSpec* module;
};

// A design that passed every check: its hull's voxels, its mounts and the class that holds it.
struct Design
{
  const DesignSpec* spec;
  SizeClass sizeClass;
  std::vector<HullVoxel> voxels; // part after part, each part's in its records' order
  std::vector<Mount> mounts;     // in its hull's hardpoint order, which the writer keeps by name
};

// The unit axis _mount's +Z turns to: the way it faces, and the way its line runs.
[[nodiscard]] NeuronCore::Int3 Facing(const Mount& _mount) noexcept;

// The cells _mount's box fills, in the model's space.
[[nodiscard]] std::vector<NeuronCore::Int3> MountCells(const Mount& _mount);

// _hull, a model NVF's reader accepts, as _spec's hull, fitted with _spec's fit: the design, or the first refusal in
// DesignRefusal's order. _spec must outlive the design.
[[nodiscard]] std::expected<Design, DesignError> ValidateDesign(const DesignSpec& _spec, const NeuronCore::NvfModel& _hull);

// _spec's hull read from <_gameData>/<hull>.nvf, then validated.
[[nodiscard]] std::expected<Design, DesignError> LoadDesign(const DesignSpec& _spec, const std::filesystem::path& _gameData);

} // namespace GameCore
