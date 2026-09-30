#pragma once

#include "Float3.h"
#include "VoxelRecord.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace GameCore
{

// The game's data (Design/GameConcept.md §5, Design/MvpPlan.md §8): the armor classes, the modules, the size classes and
// the MVP's designs, as tables in code until a data format is decided (Design/ADR/ADR-026). A module's and a hull's
// voxels are their models' in GameData; what they do is here, never in their files (Design/Archive/NeuronVoxelFormat.md N8).

// A hull voxel's armor class, which its palette entry names (G13).
enum class MaterialClass : std::uint8_t
{
  Light,
  Heavy
};

// A class's figures. Research raises its toughness where it stands, and never its density or its price (G55).
struct Material
{
  float density;      // mass per voxel, in the mass of a light voxel
  float toughness;    // the damage a voxel takes before it goes
  float priceCredits; // per voxel
};

// The box a mount holds, and so the module it takes (Design/ADR/ADR-027).
enum class MountSize : std::uint8_t
{
  Small,
  Large
};

// What a module does, which its mount's hardpoint type names.
enum class ModuleKind : std::uint8_t
{
  Command,
  Reactor,
  Engine,
  Weapon,
  Mining,
  Cargo,
  Sensor,
  Shipyard,
  Lab,
  Refinery
};

// One module of the catalogue, known by its model's name in GameData. Masses are in the mass of a light voxel, and
// thrust in that mass times units a second squared.
struct ModuleSpec
{
  std::string_view name;
  ModuleKind kind;
  MountSize size;
  float mass;
  float powerSupply;      // a reactor's
  float powerDraw;        // everything else's
  float thrust;           // an engine's, pushing the ship against the way the engine faces
  float sensorRangeUnits; // a sensor's
  float cargoOreVoxels;   // a hold's
  float priceCredits;
};

// How a weapon's shot travels (Design/GameConcept.md §8.1): a shell flies straight at a finite speed, and a beam reaches
// its end at once, every tick it fires.
enum class ShotKind : std::uint8_t
{
  Shell,
  Beam
};

// What a weapon module does (Design/ADR/ADR-035), by its model's name. Its range is from its muzzle to the target's
// nearest voxel (G47). A hit lands on the first voxel its line meets and spends its damage on the voxels within its reach
// of that one, nearest first, each up to what the voxel has left; whatever is left after all of them go carries on along
// the line (G39).
struct WeaponSpec
{
  std::string_view module;
  ShotKind shot;
  float rangeUnits;
  float damage;          // a shell's; a beam's in a second
  float reachUnits;      // about the voxel a hit lands on, from center to center
  float intervalSeconds; // between shells; 0 for a beam, which fires every tick
  float shellSpeedUnitsPerSecond;
};

// Whether a design flies or stands. Each has size classes of its own (G40).
enum class DesignKind : std::uint8_t
{
  Ship,
  Structure
};

// A size class (G40): a box and a voxel budget within it, the speed and turn a ship of it is held to, and the command
// points it costs (G56). A design takes the first class of its kind, in the catalogue's order, that holds it.
enum class SizeClass : std::uint8_t
{
  Frigate,
  Capital,
  Station
};

struct SizeClassSpec
{
  std::string_view name;
  DesignKind kind;
  NeuronCore::Int3 boxVoxels; // what a design's hull and its mounts' boxes fit in, unturned
  std::uint32_t voxelBudget;  // hull voxels at most
  float speedCapUnitsPerSecond;
  float turnRateCapRadiansPerSecond;
  std::uint32_t commandPoints;
};

// One entry of a design's fit: the module at a mount, both by name.
struct FitEntry
{
  std::string_view mount;  // the hardpoint's name, e.g. weapon.s.port
  std::string_view module; // the module's model name, e.g. MassDriver
};

// A design's game data (the concept's §5.2): its hull's model, whether it flies, the armor class of each palette entry,
// and its one fit, which is its default variant (Design/MvpPlan.md §2.2).
struct DesignSpec
{
  std::string_view name;
  std::string_view hull; // the hull's model name in GameData
  DesignKind kind;
  std::array<MaterialClass, NeuronCore::PALETTE_ENTRY_COUNT> materials; // by palette entry, entry 1 first
  std::span<const FitEntry> fit;
};

inline constexpr std::size_t SIZE_CLASS_COUNT = 3;

// The share of an engine's thrust it can turn across the ship's length to turn it (Design/ADR/ADR-026).
inline constexpr float VECTORED_THRUST_SHARE = 0.25f;

// How fast a shipyard turns credits into a ship: a design's build time is its price over this. Set so that the core's one
// shipyard builds a budget of 20 command points in the opponent's mix, six miners, four gunships, two lancers and two
// cruisers, in about 10.6 minutes: sooner than the 12 to 15 in which six miners pay for it (Design/MvpPlan.md §8.3, §8.5),
// so that income paces production rather than the yard.
inline constexpr float BUILD_CREDITS_PER_SECOND = 150.0f;

[[nodiscard]] const Material& MaterialOf(MaterialClass _class) noexcept;
[[nodiscard]] const SizeClassSpec& SizeClassOf(SizeClass _class) noexcept;

[[nodiscard]] std::span<const ModuleSpec> Modules() noexcept;
[[nodiscard]] const ModuleSpec* FindModule(std::string_view _name) noexcept;

// The weapon a module of the catalogue is, if it is one: the mass driver's shells and the laser's beam.
[[nodiscard]] std::span<const WeaponSpec> Weapons() noexcept;
[[nodiscard]] const WeaponSpec* FindWeapon(std::string_view _module) noexcept;

// The MVP's library: the miner, the gunship, the lancer, the cruiser, and the skirmish's station core (G48).
[[nodiscard]] std::span<const DesignSpec> Designs() noexcept;
[[nodiscard]] const DesignSpec* FindDesign(std::string_view _name) noexcept;

// The hardpoint type a module of _kind mounts on, as a mount's name spells it: command, reactor, engine and so on; and
// back.
[[nodiscard]] std::string_view HardpointTypeOf(ModuleKind _kind) noexcept;
[[nodiscard]] std::optional<ModuleKind> ModuleKindOf(std::string_view _hardpointType) noexcept;

// A mount's size as its name's second segment spells it, s or l (Design/ADR/ADR-027); and back.
[[nodiscard]] std::string_view MountSizeSegment(MountSize _size) noexcept;
[[nodiscard]] std::optional<MountSize> MountSizeOf(std::string_view _segment) noexcept;

// The box a mount of _size holds, x by y by z in the mount's frame, +Z the way it faces: odd on every axis, so that its
// center is a voxel's (Design/ADR/ADR-027).
[[nodiscard]] NeuronCore::Int3 MountBox(MountSize _size) noexcept;

// Whether a module of _kind works along a line out of the hull, which the hull must leave clear for it to work (G38).
[[nodiscard]] bool WorksAlongLine(ModuleKind _kind) noexcept;

} // namespace GameCore
