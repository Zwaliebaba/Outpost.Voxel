#include "pch.h"

#include "Catalogue.h"

#include <algorithm>
#include <cstddef>
#include <numbers>

namespace GameCore
{
namespace
{

using NeuronCore::Int3;

constexpr float RADIANS_PER_DEGREE = std::numbers::pi_v<float> / 180.0f;

// Tier zero's armor (Design/MvpPlan.md §8.1): a heavy voxel is three times a light one's mass and toughness, and costs
// four times as much.
constexpr std::array<Material, 2> MATERIALS{{
  {1.0f, 10.0f, 1.0f}, // Light
  {3.0f, 30.0f, 4.0f}, // Heavy
}};

// The classes' speeds and turns are ADR-017's, which the owner accepted by eye (Design/MvpPlan.md §8.1). Each kind's
// classes run from the smallest, so that a design takes the least class that holds it.
constexpr std::array<SizeClassSpec, SIZE_CLASS_COUNT> SIZE_CLASSES{{
  {"Frigate", DesignKind::Ship, {32, 16, 48}, 2000, 60.0f, 45.0f * RADIANS_PER_DEGREE, 1},
  {"Capital", DesignKind::Ship, {64, 24, 96}, 12000, 20.0f, 8.0f * RADIANS_PER_DEGREE, 4},
  {"Station", DesignKind::Structure, {64, 40, 64}, 40000, 0.0f, 0.0f, 0},
}};

// The modules, one per kind and size that a design fits (Design/MvpPlan.md §8.1). The thruster is set so that each
// class's ships accelerate near ADR-017's figures, 30 units/s² for a frigate on two and 5 for a capital ship on four;
// power so that every fit runs within its reactor's supply.
constexpr std::array<ModuleSpec, 13> MODULES{{
  // name, kind, size, mass, supply, draw, thrust, sensor range, cargo, price
  {"CommandModule", ModuleKind::Command, MountSize::Small, 30.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 150.0f},
  {"Reactor", ModuleKind::Reactor, MountSize::Small, 60.0f, 25.0f, 0.0f, 0.0f, 0.0f, 0.0f, 200.0f},
  {"ReactorLarge", ModuleKind::Reactor, MountSize::Large, 250.0f, 80.0f, 0.0f, 0.0f, 0.0f, 0.0f, 800.0f},
  {"Thruster", ModuleKind::Engine, MountSize::Small, 40.0f, 0.0f, 4.0f, 30000.0f, 0.0f, 0.0f, 120.0f},
  {"MassDriver", ModuleKind::Weapon, MountSize::Small, 30.0f, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f, 250.0f},
  {"Laser", ModuleKind::Weapon, MountSize::Small, 30.0f, 0.0f, 6.0f, 0.0f, 0.0f, 0.0f, 350.0f},
  {"MiningLaser", ModuleKind::Mining, MountSize::Small, 30.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, 150.0f},
  {"CargoHold", ModuleKind::Cargo, MountSize::Small, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 300.0f, 60.0f},
  {"Sensor", ModuleKind::Sensor, MountSize::Small, 10.0f, 0.0f, 1.0f, 0.0f, 700.0f, 0.0f, 80.0f},
  {"SensorArray", ModuleKind::Sensor, MountSize::Large, 60.0f, 0.0f, 4.0f, 0.0f, 1200.0f, 0.0f, 400.0f},
  {"Shipyard", ModuleKind::Shipyard, MountSize::Large, 400.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f},
  {"Lab", ModuleKind::Lab, MountSize::Large, 300.0f, 0.0f, 15.0f, 0.0f, 0.0f, 0.0f, 0.0f},
  {"Refinery", ModuleKind::Refinery, MountSize::Large, 300.0f, 0.0f, 15.0f, 0.0f, 0.0f, 0.0f, 0.0f},
}};

// The weapons (Design/MvpPlan.md §8.3, Design/ADR/ADR-035): the mass driver's 300 units/s shells out to 600 units, and the
// laser's beam out to 250. Their damage and reach are the smoke check's (ADR-035): a gunship and a lancer win about as
// often as each other, and their duels last 37 to 45 s on average at every range.
constexpr std::array<WeaponSpec, 2> WEAPONS{{
  // module, shot, range, damage, reach, interval, shell speed
  {"MassDriver", ShotKind::Shell, 600.0f, 200.0f, 1.5f, 1.0f, 300.0f},
  {"Laser", ShotKind::Beam, 250.0f, 400.0f, 1.0f, 0.0f, 0.0f},
}};

// The generator's palette convention (Design/ADR/ADR-027): entries 3 and 4 are heavy armor, and every other is light.
constexpr MaterialClass L = MaterialClass::Light;
constexpr MaterialClass H = MaterialClass::Heavy;
constexpr std::array<MaterialClass, NeuronCore::PALETTE_ENTRY_COUNT> HULL_MATERIALS{L, L, H, H, L, L, L, L, L, L, L, L, L, L, L, L};

constexpr std::array<FitEntry, 7> MINER_FIT{{
  {"cargo.s.hold", "CargoHold"},
  {"command.s.core", "CommandModule"},
  {"engine.s.port", "Thruster"},
  {"engine.s.starboard", "Thruster"},
  {"mining.s.front", "MiningLaser"},
  {"reactor.s.main", "Reactor"},
  {"sensor.s.dorsal", "Sensor"},
}};

constexpr std::array<FitEntry, 7> GUNSHIP_FIT{{
  {"command.s.core", "CommandModule"},
  {"engine.s.port", "Thruster"},
  {"engine.s.starboard", "Thruster"},
  {"reactor.s.main", "Reactor"},
  {"sensor.s.dorsal", "Sensor"},
  {"weapon.s.port", "MassDriver"},
  {"weapon.s.starboard", "MassDriver"},
}};

constexpr std::array<FitEntry, 7> LANCER_FIT{{
  {"command.s.core", "CommandModule"},
  {"engine.s.port", "Thruster"},
  {"engine.s.starboard", "Thruster"},
  {"reactor.s.main", "Reactor"},
  {"sensor.s.dorsal", "Sensor"},
  {"weapon.s.port", "Laser"},
  {"weapon.s.starboard", "Laser"},
}};

constexpr std::array<FitEntry, 11> CRUISER_FIT{{
  {"command.s.core", "CommandModule"},
  {"engine.s.port_dorsal", "Thruster"},
  {"engine.s.port_ventral", "Thruster"},
  {"engine.s.starboard_dorsal", "Thruster"},
  {"engine.s.starboard_ventral", "Thruster"},
  {"reactor.l.main", "ReactorLarge"},
  {"sensor.s.dorsal", "Sensor"},
  {"weapon.s.dorsal_aft", "MassDriver"},
  {"weapon.s.dorsal_fore", "MassDriver"},
  {"weapon.s.port", "Laser"},
  {"weapon.s.starboard", "Laser"},
}};

constexpr std::array<FitEntry, 10> STATION_CORE_FIT{{
  {"command.s.core", "CommandModule"},
  {"lab.l.main", "Lab"},
  {"reactor.l.main", "ReactorLarge"},
  {"refinery.l.main", "Refinery"},
  {"sensor.l.array", "SensorArray"},
  {"shipyard.l.dock", "Shipyard"},
  {"weapon.s.east", "MassDriver"},
  {"weapon.s.north", "MassDriver"},
  {"weapon.s.south", "MassDriver"},
  {"weapon.s.west", "MassDriver"},
}};

constexpr std::array<DesignSpec, 5> DESIGNS{{
  {"Miner", "Miner", DesignKind::Ship, HULL_MATERIALS, MINER_FIT},
  {"Gunship", "Gunship", DesignKind::Ship, HULL_MATERIALS, GUNSHIP_FIT},
  {"Lancer", "Lancer", DesignKind::Ship, HULL_MATERIALS, LANCER_FIT},
  {"Cruiser", "Cruiser", DesignKind::Ship, HULL_MATERIALS, CRUISER_FIT},
  {"StationCore", "StationCore", DesignKind::Structure, HULL_MATERIALS, STATION_CORE_FIT},
}};

// The hardpoint types, by ModuleKind.
constexpr std::array<std::string_view, 10> HARDPOINT_TYPES{"command", "reactor", "engine",   "weapon", "mining",
                                                           "cargo",   "sensor",  "shipyard", "lab",    "refinery"};

} // namespace

const Material& MaterialOf(MaterialClass _class) noexcept
{
  return MATERIALS[static_cast<std::size_t>(_class)];
}

const SizeClassSpec& SizeClassOf(SizeClass _class) noexcept
{
  return SIZE_CLASSES[static_cast<std::size_t>(_class)];
}

std::span<const ModuleSpec> Modules() noexcept
{
  return MODULES;
}

const ModuleSpec* FindModule(std::string_view _name) noexcept
{
  const auto found = std::ranges::find(MODULES, _name, &ModuleSpec::name);
  return found == MODULES.end() ? nullptr : &*found;
}

std::span<const WeaponSpec> Weapons() noexcept
{
  return WEAPONS;
}

const WeaponSpec* FindWeapon(std::string_view _module) noexcept
{
  const auto found = std::ranges::find(WEAPONS, _module, &WeaponSpec::module);
  return found == WEAPONS.end() ? nullptr : &*found;
}

std::span<const DesignSpec> Designs() noexcept
{
  return DESIGNS;
}

const DesignSpec* FindDesign(std::string_view _name) noexcept
{
  const auto found = std::ranges::find(DESIGNS, _name, &DesignSpec::name);
  return found == DESIGNS.end() ? nullptr : &*found;
}

std::string_view HardpointTypeOf(ModuleKind _kind) noexcept
{
  const auto index = static_cast<std::size_t>(_kind);
  return index < HARDPOINT_TYPES.size() ? HARDPOINT_TYPES[index] : std::string_view();
}

std::optional<ModuleKind> ModuleKindOf(std::string_view _hardpointType) noexcept
{
  const auto found = std::ranges::find(HARDPOINT_TYPES, _hardpointType);
  if (found == HARDPOINT_TYPES.end())
  {
    return std::nullopt;
  }
  return static_cast<ModuleKind>(found - HARDPOINT_TYPES.begin());
}

std::string_view MountSizeSegment(MountSize _size) noexcept
{
  return _size == MountSize::Small ? "s" : "l";
}

std::optional<MountSize> MountSizeOf(std::string_view _segment) noexcept
{
  if (_segment == "s")
  {
    return MountSize::Small;
  }
  if (_segment == "l")
  {
    return MountSize::Large;
  }
  return std::nullopt;
}

Int3 MountBox(MountSize _size) noexcept
{
  return _size == MountSize::Small ? Int3{3, 3, 5} : Int3{5, 5, 9};
}

bool WorksAlongLine(ModuleKind _kind) noexcept
{
  return _kind == ModuleKind::Engine || _kind == ModuleKind::Weapon || _kind == ModuleKind::Mining || _kind == ModuleKind::Sensor;
}

} // namespace GameCore
