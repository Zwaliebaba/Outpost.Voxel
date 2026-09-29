#include "pch.h"

#include "Catalogue.h"
#include "TestSupport.h"

#include "NvfModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <set>
#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{
namespace
{

// Every module kind, in ModuleKind's order.
constexpr std::array<GameCore::ModuleKind, 10> MODULE_KINDS{
  GameCore::ModuleKind::Command, GameCore::ModuleKind::Reactor, GameCore::ModuleKind::Engine, GameCore::ModuleKind::Weapon,
  GameCore::ModuleKind::Mining,  GameCore::ModuleKind::Cargo,   GameCore::ModuleKind::Sensor, GameCore::ModuleKind::Shipyard,
  GameCore::ModuleKind::Lab,     GameCore::ModuleKind::Refinery};

// The asteroids the generator writes beside the designs (Design/MvpPlan.md, phase 1).
constexpr std::array<std::string_view, 3> ASTEROIDS{"AsteroidA", "AsteroidB", "AsteroidC"};

// The extent of _model's one part, in voxels.
[[nodiscard]] NeuronCore::Int3 ExtentOf(const NeuronCore::NvfModel& _model)
{
  Assert::AreEqual(std::size_t{1}, _model.parts.size(), L"a generated model is a single part");
  NeuronCore::Int3 lower{255, 255, 255};
  NeuronCore::Int3 upper{0, 0, 0};
  for (const std::uint32_t packed : _model.records)
  {
    const NeuronCore::VoxelRecord record = NeuronCore::UnpackVoxelRecord(packed);
    lower = {std::min<std::int32_t>(lower.x, record.x), std::min<std::int32_t>(lower.y, record.y),
             std::min<std::int32_t>(lower.z, record.z)};
    upper = {std::max<std::int32_t>(upper.x, record.x), std::max<std::int32_t>(upper.y, record.y),
             std::max<std::int32_t>(upper.z, record.z)};
  }
  return upper - lower + NeuronCore::Int3{1, 1, 1};
}

} // namespace

// The catalogue's tables (Design/MvpPlan.md §8) and the models in GameData they name.
TEST_CLASS(CatalogueTests)
{
public:
  // A module's model fits the box of its mount's size, so that a module never reaches into its hull (G37).
  TEST_METHOD(EveryModuleHasAModelWithinItsBox)
  {
    for (const GameCore::ModuleSpec& module : GameCore::Modules())
    {
      const NeuronCore::Int3 extent = ExtentOf(LoadModel(module.name));
      const NeuronCore::Int3 box = GameCore::MountBox(module.size);
      Assert::IsTrue(extent.x <= box.x && extent.y <= box.y && extent.z <= box.z,
                     std::format(L"{} is {} x {} x {}, within its box", Widen(module.name), extent.x, extent.y, extent.z).c_str());
    }
  }

  TEST_METHOD(EveryDesignHasAHull)
  {
    for (const GameCore::DesignSpec& design : GameCore::Designs())
    {
      Assert::IsTrue(!LoadModel(design.hull).hardpoints.empty(), std::format(L"{}'s hull has mounts", Widen(design.name)).c_str());
    }
  }

  TEST_METHOD(EveryAsteroidLoads)
  {
    std::set<std::size_t> voxelCounts;
    for (const std::string_view asteroid : ASTEROIDS)
    {
      const NeuronCore::NvfModel model = LoadModel(asteroid);
      Assert::IsTrue(model.hardpoints.empty(), std::format(L"{} has no mounts", Widen(asteroid)).c_str());
      voxelCounts.insert(model.records.size());
    }
    Assert::AreEqual(ASTEROIDS.size(), voxelCounts.size(), L"three rocks, of three sizes");
  }

  TEST_METHOD(NamesAreUnique)
  {
    std::set<std::string_view> modules;
    for (const GameCore::ModuleSpec& module : GameCore::Modules())
    {
      Assert::IsTrue(modules.insert(module.name).second, Widen(module.name).c_str());
      Assert::IsTrue(GameCore::FindModule(module.name) == &module, Widen(module.name).c_str());
    }
    std::set<std::string_view> designs;
    for (const GameCore::DesignSpec& design : GameCore::Designs())
    {
      Assert::IsTrue(designs.insert(design.name).second, Widen(design.name).c_str());
      Assert::IsTrue(GameCore::FindDesign(design.name) == &design, Widen(design.name).c_str());
    }
    Assert::IsTrue(GameCore::FindModule("Railgun") == nullptr);
    Assert::IsTrue(GameCore::FindDesign("Dreadnought") == nullptr);
  }

  // A mount's name carries its type and size (Design/ADR/ADR-025), and each spelling reads back as what wrote it.
  TEST_METHOD(MountNamesRoundTrip)
  {
    for (const GameCore::ModuleKind kind : MODULE_KINDS)
    {
      const std::string_view type = GameCore::HardpointTypeOf(kind);
      Assert::IsTrue(NeuronCore::IsNvfHardpointName(std::string(type) + ".s.main"), Widen(type).c_str());
      Assert::IsTrue(GameCore::ModuleKindOf(type) == kind, Widen(type).c_str());
    }
    for (const GameCore::MountSize size : {GameCore::MountSize::Small, GameCore::MountSize::Large})
    {
      Assert::IsTrue(GameCore::MountSizeOf(GameCore::MountSizeSegment(size)) == size);
      const NeuronCore::Int3 box = GameCore::MountBox(size);
      Assert::IsTrue(box.x % 2 == 1 && box.y % 2 == 1 && box.z % 2 == 1, L"odd on every axis, so that its center is a voxel's");
    }
    Assert::IsFalse(GameCore::ModuleKindOf("turret").has_value());
    Assert::IsFalse(GameCore::MountSizeOf("m").has_value());
  }

  // Each kind's classes run from the smallest, so that a design takes the least class that holds it.
  TEST_METHOD(SizeClassesGrow)
  {
    const GameCore::SizeClassSpec& frigate = GameCore::SizeClassOf(GameCore::SizeClass::Frigate);
    const GameCore::SizeClassSpec& capital = GameCore::SizeClassOf(GameCore::SizeClass::Capital);
    Assert::IsTrue(frigate.boxVoxels.x <= capital.boxVoxels.x && frigate.boxVoxels.y <= capital.boxVoxels.y &&
                   frigate.boxVoxels.z <= capital.boxVoxels.z && frigate.voxelBudget < capital.voxelBudget);
    Assert::IsTrue(frigate.speedCapUnitsPerSecond > capital.speedCapUnitsPerSecond, L"a capital ship is slower");
    Assert::IsTrue(frigate.commandPoints < capital.commandPoints, L"and costs more of the budget");
  }
};

} // namespace GameCoreTests
