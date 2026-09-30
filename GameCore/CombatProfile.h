#pragma once

#include "Catalogue.h"
#include "Design.h"

#include "Composite.h"
#include "Float3.h"
#include "Message.h"
#include "TraceHit.h"
#include "VoxModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace GameCore
{

// Combat's rules that both sides of the boundary read (Design/GameConcept.md §8, Design/ADR/ADR-035).

// A module fails once fewer than this share of its voxels remain (§8.3): a failed command module loses its ship, and a
// failed reactor detonates it.
inline constexpr float FAIL_SHARE = 0.5f;

// The level directions a silhouette is taken along: a turn in equal steps, from +Z toward +X (G57).
inline constexpr std::size_t SILHOUETTE_DIRECTIONS = 16;

// One component of a design's composite: the hull, or the module at one of its mounts, and where its voxels lie among
// the composite's.
struct CombatComponent
{
  std::optional<ModuleKind> module; // none for the hull
  std::uint32_t firstVoxel;
  std::uint32_t voxelCount;
};

// A weapon at one of a design's mounts.
struct CombatWeapon
{
  const WeaponSpec* spec;
  std::uint16_t component;   // its module's, in the composite
  NeuronCore::Float3 muzzle; // the middle of the front face of its mount's box, in the composite's space
  NeuronCore::Int3 facing;   // its mount's: it fires into the half-space ahead of this
};

// What combat knows of a design (ADR-035), from its composite: every voxel, in the order masks count them, and its
// material; its components; its weapons; the bearing it holds a target at; and its silhouettes, which the profile gains
// (G29, G57) from the composite, since the modules' voxels are drawn as the hull's are.
struct CombatProfile
{
  std::vector<NeuronCore::CompositeVoxel> voxels; // in the composite's order
  std::vector<MaterialClass> materials;           // by voxel: a hull voxel's by its palette entry, a module's light
  std::vector<CombatComponent> components;        // in the composite's order: the hull, then each mount's module
  std::vector<CombatWeapon> weapons;              // in the order of their mounts
  float bearingRadians;                           // the angle off the bow it holds its target at, to starboard (+X) positive

  // For each of SILHOUETTE_DIRECTIONS level directions a shooter may look along, the design as it shows from there: the
  // first voxel along each of a grid of parallel rays a voxel apart, once for each ray that meets it, so that a voxel is
  // drawn in proportion to the area it shows.
  std::array<std::vector<std::uint32_t>, SILHOUETTE_DIRECTIONS> silhouettes;
};

// _design's combat profile, from _composite, its composite (DesignComposite), whose components name _models.
[[nodiscard]] CombatProfile ComputeCombatProfile(const Design& _design, std::span<const NeuronCore::VoxModel> _models,
                                                 const NeuronCore::CompositeModel& _composite);

// The silhouette a shooter looking along _direction, in the design's space, sees: the nearest of the
// SILHOUETTE_DIRECTIONS on the plane.
[[nodiscard]] std::size_t SilhouetteDirection(NeuronCore::Float3 _direction) noexcept;

// The components of _composite as a client knows them, from the names of the models they place, _names, by manifest
// index: a component whose model is a module of the catalogue is that module, and any other is hull.
[[nodiscard]] std::vector<CombatComponent> ComponentsOf(std::span<const std::string> _names, std::span<const NeuronCore::VoxModel> _models,
                                                        const NeuronCore::CompositeModel& _composite);

// A ship's condition (G59, the concept's §9): its weakest vital module's integrity, the command module's or a reactor's,
// from 1 whole down to 0 at FAIL_SHARE, and the share of its hull's voxels that remain; 1 for what it lacks.
struct Condition
{
  float vital;
  float hull;
};

// The condition of an entity whose composite has _components and whose voxels _gone are gone, an EntityMask's bits, or
// none.
[[nodiscard]] Condition ConditionOf(std::span<const CombatComponent> _components, std::span<const std::uint8_t> _gone) noexcept;

} // namespace GameCore
