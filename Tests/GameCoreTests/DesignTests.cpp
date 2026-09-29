#include "pch.h"

#include "Catalogue.h"
#include "Design.h"
#include "TestSupport.h"

#include "NvfModel.h"
#include "Quaternion.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{
namespace
{

using NeuronCore::Int3;

// The class each design of the MVP takes (Design/MvpPlan.md §8.1).
struct ExpectedClass
{
  std::string_view design;
  GameCore::SizeClass sizeClass;
};

constexpr std::array<ExpectedClass, 5> EXPECTED_CLASSES{{
  {"Miner", GameCore::SizeClass::Frigate},
  {"Gunship", GameCore::SizeClass::Frigate},
  {"Lancer", GameCore::SizeClass::Frigate},
  {"Cruiser", GameCore::SizeClass::Capital},
  {"StationCore", GameCore::SizeClass::Station},
}};

// The gunship's fit, and variations of it that break one rule each.
constexpr std::array<GameCore::FitEntry, 7> GUNSHIP_FIT{{
  {"command.s.core", "CommandModule"},
  {"engine.s.port", "Thruster"},
  {"engine.s.starboard", "Thruster"},
  {"reactor.s.main", "Reactor"},
  {"sensor.s.dorsal", "Sensor"},
  {"weapon.s.port", "MassDriver"},
  {"weapon.s.starboard", "MassDriver"},
}};

[[nodiscard]] bool SameCell(Int3 _a, Int3 _b) noexcept
{
  return _a.x == _b.x && _a.y == _b.y && _a.z == _b.z;
}

[[nodiscard]] std::wstring CellText(Int3 _cell)
{
  return std::format(L"({}, {}, {})", _cell.x, _cell.y, _cell.z);
}

[[nodiscard]] const GameCore::Mount& MountOf(const GameCore::Design& _design, std::string_view _name)
{
  const auto mount = std::ranges::find(_design.mounts, _name, &GameCore::Mount::name);
  Assert::IsTrue(mount != _design.mounts.end(), std::format(L"the design has {}", Widen(_name)).c_str());
  return *mount;
}

[[nodiscard]] NeuronCore::NvfHardpoint& HardpointOf(NeuronCore::NvfModel& _hull, std::string_view _name)
{
  const auto hardpoint = std::ranges::find(_hull.hardpoints, _name, &NeuronCore::NvfHardpoint::name);
  Assert::IsTrue(hardpoint != _hull.hardpoints.end(), std::format(L"the hull has {}", Widen(_name)).c_str());
  return *hardpoint;
}

// A hardpoint as a marker leaves one: at a voxel's center, turned by _rotation.
[[nodiscard]] NeuronCore::NvfHardpoint Hardpoint(std::string _name, NeuronCore::Float3 _position,
                                                 NeuronCore::Quaternion _rotation = {0.0f, 0.0f, 0.0f, 1.0f})
{
  return {std::move(_name), 0, _position, _rotation, true};
}

// _hull validated as the gunship's hull, with _fit or the gunship's own.
[[nodiscard]] std::string Validate(const NeuronCore::NvfModel& _hull, std::span<const GameCore::FitEntry> _fit = GUNSHIP_FIT,
                                   GameCore::DesignKind _kind = GameCore::DesignKind::Ship)
{
  GameCore::DesignSpec spec = *GameCore::FindDesign("Gunship");
  spec.fit = _fit;
  spec.kind = _kind;
  const auto design = GameCore::ValidateDesign(spec, _hull);
  if (!design)
  {
    Logger::WriteMessage(Widen(std::format("{}: {}", GameCore::DesignRefusalName(design.error().refusal), design.error().detail)).c_str());
  }
  return RefusalOf(design);
}

} // namespace

// Design/GameConcept.md §5.5: the MVP's designs pass every check, and a design that breaks one is refused by its name.
TEST_CLASS(DesignTests)
{
public:
  TEST_METHOD(EveryMvpDesignValidates)
  {
    for (const GameCore::DesignSpec& spec : GameCore::Designs())
    {
      const GameCore::Design design = LoadMvpDesign(spec.name);
      Assert::AreEqual(spec.fit.size(), design.mounts.size(), std::format(L"{} fits every mount", Widen(spec.name)).c_str());
    }
  }

  TEST_METHOD(EachDesignTakesThePlansClass)
  {
    for (const ExpectedClass& expected : EXPECTED_CLASSES)
    {
      const GameCore::Design design = LoadMvpDesign(expected.design);
      Assert::AreEqual(std::string(GameCore::SizeClassOf(expected.sizeClass).name),
                       std::string(GameCore::SizeClassOf(design.sizeClass).name), Widen(expected.design).c_str());
    }
  }

  // The generator's facings survive the .vox, the importer and NVF: engines face aft, sensors up, and the beam and
  // edge weapons outward.
  TEST_METHOD(MountsFaceTheWayTheirHullsSay)
  {
    struct ExpectedFacing
    {
      std::string_view design;
      std::string_view mount;
      Int3 facing;
    };
    constexpr std::array<ExpectedFacing, 10> FACINGS{{
      {"Gunship", "engine.s.port", {0, 0, -1}},
      {"Gunship", "weapon.s.starboard", {0, 0, 1}},
      {"Gunship", "sensor.s.dorsal", {0, 1, 0}},
      {"Cruiser", "weapon.s.port", {-1, 0, 0}},
      {"Cruiser", "weapon.s.starboard", {1, 0, 0}},
      {"Cruiser", "engine.s.starboard_ventral", {0, 0, -1}},
      {"StationCore", "lab.l.main", {-1, 0, 0}},
      {"StationCore", "refinery.l.main", {1, 0, 0}},
      {"StationCore", "weapon.s.south", {0, 0, -1}},
      {"StationCore", "sensor.l.array", {0, 1, 0}},
    }};
    for (const ExpectedFacing& expected : FACINGS)
    {
      const Int3 facing = GameCore::Facing(MountOf(LoadMvpDesign(expected.design), expected.mount));
      Assert::IsTrue(SameCell(expected.facing, facing),
                     std::format(L"{}'s {} faces {}", Widen(expected.design), Widen(expected.mount), CellText(facing)).c_str());
    }
  }

  // A mount's box is centered on its mount and runs along its facing: the gunship's port engine, facing aft, fills
  // three cells across and five behind the hull.
  TEST_METHOD(AMountsBoxSurroundsItsCenter)
  {
    const GameCore::Design gunship = LoadMvpDesign("Gunship");
    const GameCore::Mount& engine = MountOf(gunship, "engine.s.port");
    Assert::IsTrue(SameCell({4, 2, -3}, engine.centerCell), CellText(engine.centerCell).c_str());
    const std::vector<Int3> cells = GameCore::MountCells(engine);
    Assert::AreEqual(std::size_t{45}, cells.size());
    const auto [lowest, highest] = std::ranges::minmax(cells, {}, &Int3::z);
    Assert::AreEqual(-5, lowest.z);
    Assert::AreEqual(-1, highest.z, L"against the hull, whose aft face is at z = 0");
    Assert::AreEqual(std::size_t{225}, GameCore::MountCells(MountOf(LoadMvpDesign("Cruiser"), "reactor.l.main")).size());
  }

  // Every weapon, engine, sensor and mining laser of the MVP sees out of its hull (G38).
  TEST_METHOD(EveryLineIsClear)
  {
    for (const GameCore::DesignSpec& spec : GameCore::Designs())
    {
      for (const GameCore::Mount& mount : LoadMvpDesign(spec.name).mounts)
      {
        Assert::IsTrue(!GameCore::WorksAlongLine(mount.kind) || mount.lineClear,
                       std::format(L"{}'s {}", Widen(spec.name), Widen(mount.name)).c_str());
      }
    }
  }

  // A tail that reaches behind the port engine and turns up across its line blocks it: the design is valid, and the
  // engine does nothing (G38).
  TEST_METHOD(ABlockedLineStopsItsModuleWithoutRefusingTheDesign)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    AddPart(hull, "hull/tail", TAIL_TRANSLATION, TAIL_SIZE, TAIL_CELLS);
    GameCore::DesignSpec spec = *GameCore::FindDesign("Gunship");
    const auto design = GameCore::ValidateDesign(spec, hull);
    Assert::AreEqual(std::string("Accepted"), RefusalOf(design));
    Assert::IsFalse(MountOf(*design, "engine.s.port").lineClear, L"the tail crosses the port engine's line");
    Assert::IsTrue(MountOf(*design, "engine.s.starboard").lineClear, L"and not the starboard one's");
  }

  TEST_METHOD(RefusesAHullItCannotRead)
  {
    GameCore::DesignSpec spec = *GameCore::FindDesign("Gunship");
    spec.hull = "NoSuchHull";
    const auto design = GameCore::LoadDesign(spec, GameDataDirectory());
    Assert::AreEqual(std::string("HullUnreadable"), RefusalOf(design));
    Assert::AreEqual(std::string("NoSuchHull.nvf: FileNotFound"), design.error().detail);
  }

  TEST_METHOD(RefusesABadMountName)
  {
    for (const std::string_view name : {"weapon.m.port", "turret.s.port", "weapon.s", "weapon.s.port.upper"})
    {
      NeuronCore::NvfModel hull = LoadModel("Gunship");
      HardpointOf(hull, "weapon.s.port").name = name;
      Assert::AreEqual(std::string("BadMountName"), Validate(hull), Widen(name).c_str());
    }
  }

  TEST_METHOD(RefusesAMountOffAVoxelsCenter)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    HardpointOf(hull, "weapon.s.port").position.x += 0.5f;
    Assert::AreEqual(std::string("MountOffCenter"), Validate(hull));
  }

  TEST_METHOD(RefusesAMountTurnedOffTheAxes)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    const float half = std::numbers::pi_v<float> / 8.0f;
    HardpointOf(hull, "weapon.s.port").rotation = {0.0f, std::sin(half), 0.0f, std::cos(half)};
    Assert::AreEqual(std::string("MountOffAxis"), Validate(hull), L"an eighth of a turn about the vertical");
  }

  TEST_METHOD(RefusesADesignWithoutACommandMount)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    std::erase_if(hull.hardpoints, [](const NeuronCore::NvfHardpoint& _hardpoint) { return _hardpoint.name == "command.s.core"; });
    Assert::AreEqual(std::string("NoCommandMount"), Validate(hull));
  }

  TEST_METHOD(RefusesASecondCommandMount)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    NeuronCore::NvfHardpoint spare = HardpointOf(hull, "command.s.core");
    spare.name = "command.s.spare";
    hull.hardpoints.push_back(spare);
    Assert::AreEqual(std::string("ExtraCommandMount"), Validate(hull));
  }

  // The cruiser is too long for a station's box, and the core too tall for a capital ship's (G40).
  TEST_METHOD(RefusesADesignNoClassOfItsKindHolds)
  {
    GameCore::DesignSpec cruiser = *GameCore::FindDesign("Cruiser");
    cruiser.kind = GameCore::DesignKind::Structure;
    Assert::AreEqual(std::string("NoSizeClass"), RefusalOf(GameCore::LoadDesign(cruiser, GameDataDirectory())));
    GameCore::DesignSpec core = *GameCore::FindDesign("StationCore");
    core.kind = GameCore::DesignKind::Ship;
    Assert::AreEqual(std::string("NoSizeClass"), RefusalOf(GameCore::LoadDesign(core, GameDataDirectory())));
  }

  TEST_METHOD(RefusesPartsThatShareACell)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    constexpr std::array<Int3, 1> PLATE{{{0, 0, 0}}};
    AddPart(hull, "hull/plate", {2, 0, 0}, {1, 1, 1}, PLATE);
    Assert::AreEqual(std::string("PartsOverlap"), Validate(hull), L"the plate's voxel lands on the hull's (2, 0, 0)");
  }

  TEST_METHOD(RefusesAHullInTwoPieces)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    constexpr std::array<Int3, 1> SPLINTER{{{0, 0, 0}}};
    AddPart(hull, "hull/splinter", {0, 0, 40}, {1, 1, 1}, SPLINTER);
    Assert::AreEqual(std::string("NotOnePiece"), Validate(hull));
  }

  TEST_METHOD(RefusesAMountWhoseBoxHoldsHull)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    HardpointOf(hull, "weapon.s.port").position = {4.5f, 2.5f, 20.5f};
    Assert::AreEqual(std::string("MountHoldsHull"), Validate(hull), L"the weapon sunk into the nose");
  }

  TEST_METHOD(RefusesMountsWhoseBoxesOverlap)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    HardpointOf(hull, "weapon.s.starboard").position = {5.5f, 6.5f, 16.5f};
    Assert::AreEqual(std::string("MountsOverlap"), Validate(hull), L"a cell from the port weapon");
  }

  TEST_METHOD(RefusesAMountThatTouchesNoHull)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    HardpointOf(hull, "weapon.s.port").position = {4.5f, 8.5f, 16.5f};
    Assert::AreEqual(std::string("MountDetached"), Validate(hull), L"two cells above the deck");
  }

  TEST_METHOD(RefusesAFitNamingAMountTheHullLacks)
  {
    std::vector<GameCore::FitEntry> fit(GUNSHIP_FIT.begin(), GUNSHIP_FIT.end());
    fit.push_back({"weapon.s.ventral", "MassDriver"});
    Assert::AreEqual(std::string("UnknownMount"), Validate(LoadModel("Gunship"), fit));
  }

  TEST_METHOD(RefusesAFitNamingAMountTwice)
  {
    std::vector<GameCore::FitEntry> fit(GUNSHIP_FIT.begin(), GUNSHIP_FIT.end());
    fit.push_back({"weapon.s.port", "Laser"});
    Assert::AreEqual(std::string("MountFittedTwice"), Validate(LoadModel("Gunship"), fit));
  }

  TEST_METHOD(RefusesAModuleTheCatalogueLacks)
  {
    std::vector<GameCore::FitEntry> fit(GUNSHIP_FIT.begin(), GUNSHIP_FIT.end());
    fit[5].module = "Railgun";
    Assert::AreEqual(std::string("UnknownModule"), Validate(LoadModel("Gunship"), fit));
  }

  // A module of another kind, and one of another size (the concept's §5.5).
  TEST_METHOD(RefusesAModuleThatDoesNotMatchItsMount)
  {
    std::vector<GameCore::FitEntry> kind(GUNSHIP_FIT.begin(), GUNSHIP_FIT.end());
    kind[5].module = "Sensor";
    Assert::AreEqual(std::string("WrongModule"), Validate(LoadModel("Gunship"), kind), L"a sensor on a weapon mount");
    std::vector<GameCore::FitEntry> size(GUNSHIP_FIT.begin(), GUNSHIP_FIT.end());
    size[3].module = "ReactorLarge";
    Assert::AreEqual(std::string("WrongModule"), Validate(LoadModel("Gunship"), size), L"a large reactor on a small mount");
  }

  TEST_METHOD(RefusesAMountLeftEmpty)
  {
    constexpr std::array<GameCore::FitEntry, 6> WITHOUT_SENSOR{{
      {"command.s.core", "CommandModule"},
      {"engine.s.port", "Thruster"},
      {"engine.s.starboard", "Thruster"},
      {"reactor.s.main", "Reactor"},
      {"weapon.s.port", "MassDriver"},
      {"weapon.s.starboard", "MassDriver"},
    }};
    Assert::AreEqual(std::string("UnfittedMount"), Validate(LoadModel("Gunship"), WITHOUT_SENSOR));
  }

  // Two more weapon mounts under the keel, and lasers on all four, draw more than one reactor supplies.
  TEST_METHOD(RefusesAFitItsReactorsCannotPower)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    hull.hardpoints.push_back(Hardpoint("weapon.s.ventral_port", {4.5f, -1.5f, 16.5f}));
    hull.hardpoints.push_back(Hardpoint("weapon.s.ventral_starboard", {8.5f, -1.5f, 16.5f}));
    constexpr std::array<GameCore::FitEntry, 9> LASERS{{
      {"command.s.core", "CommandModule"},
      {"engine.s.port", "Thruster"},
      {"engine.s.starboard", "Thruster"},
      {"reactor.s.main", "Reactor"},
      {"sensor.s.dorsal", "Sensor"},
      {"weapon.s.port", "Laser"},
      {"weapon.s.starboard", "Laser"},
      {"weapon.s.ventral_port", "Laser"},
      {"weapon.s.ventral_starboard", "Laser"},
    }};
    Assert::AreEqual(std::string("PowerShort"), Validate(hull, LASERS));
  }

  // Of two faults, the one checked first is the one named.
  TEST_METHOD(RefusesTheFirstFaultInOrder)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    std::erase_if(hull.hardpoints, [](const NeuronCore::NvfHardpoint& _hardpoint) { return _hardpoint.name == "command.s.core"; });
    HardpointOf(hull, "weapon.s.port").name = "turret.s.port";
    Assert::AreEqual(std::string("BadMountName"), Validate(hull), L"before the command mount is counted");
  }
};

} // namespace GameCoreTests
