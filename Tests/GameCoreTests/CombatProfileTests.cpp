#include "pch.h"

#include "Catalogue.h"
#include "CombatProfile.h"
#include "Design.h"
#include "DesignComposite.h"
#include "TestSupport.h"

#include "Composite.h"
#include "NvfModel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Int3;

// A design with its composite and the models it names, as the skirmish holds them.
struct Fitted
{
  GameCore::Design design;
  std::vector<std::string> names;
  std::vector<NeuronCore::VoxModel> models;
  NeuronCore::CompositeModel composite;
};

[[nodiscard]] Fitted Fit(std::string_view _name)
{
  Fitted fitted{LoadMvpDesign(_name), {}, {}, {}};
  const auto add = [&fitted](std::string_view _model)
  {
    if (std::ranges::find(fitted.names, _model) == fitted.names.end())
    {
      fitted.names.emplace_back(_model);
      fitted.models.push_back(NeuronCore::FlattenNvfModel(LoadModel(_model)));
    }
  };
  add(fitted.design.spec->hull);
  for (const GameCore::Mount& mount : fitted.design.mounts)
  {
    add(mount.module->name);
  }
  const auto composite = GameCore::DesignComposite(fitted.design, {fitted.names, fitted.models});
  Assert::IsTrue(composite.has_value(), L"the design's composite");
  fitted.composite = composite.value_or(NeuronCore::CompositeModel{});
  return fitted;
}

[[nodiscard]] GameCore::CombatProfile ProfileOf(const Fitted& _fitted)
{
  return GameCore::ComputeCombatProfile(_fitted.design, _fitted.models, _fitted.composite);
}

// An EntityMask's bits for _voxels voxels, with _gone gone.
[[nodiscard]] std::vector<std::uint8_t> Mask(std::uint32_t _voxels, std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> _gone)
{
  std::vector<std::uint8_t> bits((_voxels + 7) / 8, 0);
  for (const auto& [first, count] : _gone)
  {
    for (std::uint32_t voxel = first; voxel < first + count; ++voxel)
    {
      bits[voxel / 8] = static_cast<std::uint8_t>(bits[voxel / 8] | (1u << (voxel % 8)));
    }
  }
  return bits;
}

} // namespace

// Design/ADR/ADR-035: what combat knows of a design, from its composite, and the condition a client reads from a mask.
TEST_CLASS(CombatProfileTests)
{
public:
  // Every voxel of the composite in the order of a mask, each with its material and its component, the components in
  // the composite's order and their kinds the mounts', and a weapon at each weapon mount, firing from its box's front.
  TEST_METHOD(ProfilesEachDesignsComposite)
  {
    const std::map<std::string_view, std::size_t> weapons{{"Miner", 0}, {"Gunship", 2}, {"Lancer", 2}, {"Cruiser", 4}, {"StationCore", 4}};
    for (const GameCore::DesignSpec& spec : GameCore::Designs())
    {
      const Fitted fitted = Fit(spec.name);
      const GameCore::CombatProfile profile = ProfileOf(fitted);
      const std::wstring what = Widen(spec.name);
      Assert::AreEqual(static_cast<std::size_t>(NeuronCore::CompositeVoxelCount(fitted.models, fitted.composite)), profile.voxels.size(),
                       what.c_str());
      Assert::AreEqual(fitted.composite.components.size(), profile.components.size(), (what + L": a component each").c_str());
      std::uint32_t next = 0;
      for (std::size_t component = 0; component < profile.components.size(); ++component)
      {
        const GameCore::CombatComponent& combat = profile.components[component];
        Assert::AreEqual(next, combat.firstVoxel, (what + L": the components run in order").c_str());
        next += combat.voxelCount;
        Assert::IsTrue(component == 0 ? !combat.module.has_value() : combat.module == fitted.design.mounts[component - 1].kind,
                       (what + std::format(L": component {}'s kind", component)).c_str());
      }
      Assert::AreEqual(static_cast<std::uint32_t>(profile.voxels.size()), next, (what + L": and cover every voxel").c_str());
      for (std::size_t voxel = 0; voxel < profile.voxels.size(); ++voxel)
      {
        const NeuronCore::CompositeVoxel& composite = profile.voxels[voxel];
        const GameCore::MaterialClass expected =
          composite.component == 0 ? spec.materials[composite.color] : GameCore::MaterialClass::Light;
        Assert::IsTrue(profile.materials[voxel] == expected, (what + L": a voxel's material").c_str());
      }
      Assert::AreEqual(weapons.at(spec.name), profile.weapons.size(), (what + L": its weapons").c_str());
      for (const GameCore::CombatWeapon& weapon : profile.weapons)
      {
        const GameCore::Mount& mount = fitted.design.mounts[weapon.component - 1];
        Assert::IsTrue(mount.kind == GameCore::ModuleKind::Weapon && weapon.spec == GameCore::FindWeapon(mount.module->name),
                       (what + L": a weapon at a weapon mount, as its module").c_str());
        const Int3 facing = GameCore::Facing(mount);
        const float ahead = 0.5f * static_cast<float>(GameCore::MountBox(mount.size).z);
        const Float3 expected{static_cast<float>(mount.centerCell.x) + 0.5f + static_cast<float>(facing.x) * ahead,
                              static_cast<float>(mount.centerCell.y) + 0.5f + static_cast<float>(facing.y) * ahead,
                              static_cast<float>(mount.centerCell.z) + 0.5f + static_cast<float>(facing.z) * ahead};
        Assert::IsTrue(weapon.muzzle.x == expected.x && weapon.muzzle.y == expected.y && weapon.muzzle.z == expected.z,
                       (what + L": the middle of its box's front face").c_str());
        Assert::IsTrue(weapon.facing.x == facing.x && weapon.facing.y == facing.y && weapon.facing.z == facing.z,
                       (what + L": facing as its mount").c_str());
      }
    }
  }

  // G33: a design holds its target where the most of its weapons bear: the frigates dead ahead, the cruiser, whose lasers
  // face either beam, at 45° to starboard, where both mass drivers and a laser do.
  TEST_METHOD(BearsWhereMostOfItsWeaponsDo)
  {
    constexpr float QUARTER = std::numbers::pi_v<float> / 4.0f;
    Assert::AreEqual(0.0f, ProfileOf(Fit("Gunship")).bearingRadians, L"the gunship");
    Assert::AreEqual(0.0f, ProfileOf(Fit("Lancer")).bearingRadians, L"the lancer");
    Assert::AreEqual(0.0f, ProfileOf(Fit("Miner")).bearingRadians, L"a design without weapons");
    Assert::AreEqual(QUARTER, ProfileOf(Fit("Cruiser")).bearingRadians, 1.0e-6f, L"the cruiser");
  }

  // G57: from each of the directions, a voxel for each column it shows, the nearest to the shooter; looking along -z, from
  // ahead, a column is its x and y, and its voxel the one with the greatest z.
  TEST_METHOD(DrawsTheSilhouetteByTheAreaItShows)
  {
    for (const std::string_view name : {std::string_view("Gunship"), std::string_view("Cruiser")})
    {
      const GameCore::CombatProfile profile = ProfileOf(Fit(name));
      for (std::size_t direction = 0; direction < GameCore::SILHOUETTE_DIRECTIONS; ++direction)
      {
        const std::vector<std::uint32_t>& silhouette = profile.silhouettes[direction];
        Assert::IsFalse(silhouette.empty(), Widen(std::format("{}, direction {}", name, direction)).c_str());
        Assert::IsTrue(std::ranges::all_of(silhouette, [&profile](std::uint32_t _voxel) { return _voxel < profile.voxels.size(); }),
                       L"every entry a voxel");
      }
      std::map<std::pair<std::int32_t, std::int32_t>, std::uint32_t> front;
      for (std::uint32_t voxel = 0; voxel < profile.voxels.size(); ++voxel)
      {
        const Int3 cell = profile.voxels[voxel].cell;
        const auto [slot, added] = front.try_emplace({cell.x, cell.y}, voxel);
        if (!added && cell.z > profile.voxels[slot->second].cell.z)
        {
          slot->second = voxel;
        }
      }
      std::set<std::uint32_t> expected;
      for (const auto& [column, voxel] : front)
      {
        expected.insert(voxel);
      }
      const std::vector<std::uint32_t>& ahead = profile.silhouettes[GameCore::SILHOUETTE_DIRECTIONS / 2];
      Assert::AreEqual(expected.size(), ahead.size(), Widen(std::format("{}: a voxel for each column", name)).c_str());
      Assert::IsTrue(std::set<std::uint32_t>(ahead.begin(), ahead.end()) == expected, Widen(std::format("{}: the nearest", name)).c_str());
    }
  }

  // A shooter's direction takes the nearest of the silhouette's directions, a turn divided in sixteen from +z to +x.
  TEST_METHOD(ChoosesTheNearestDirection)
  {
    const auto at = [](float _degrees)
    {
      const float radians = _degrees * std::numbers::pi_v<float> / 180.0f;
      return GameCore::SilhouetteDirection({std::sin(radians), 0.0f, std::cos(radians)});
    };
    Assert::AreEqual(std::size_t{0}, at(0.0f));
    Assert::AreEqual(std::size_t{0}, at(11.0f));
    Assert::AreEqual(std::size_t{1}, at(12.0f));
    Assert::AreEqual(std::size_t{4}, at(90.0f));
    Assert::AreEqual(std::size_t{8}, at(180.0f));
    Assert::AreEqual(std::size_t{12}, at(-90.0f));
    Assert::AreEqual(std::size_t{15}, at(-22.0f));
    Assert::AreEqual(std::size_t{4}, GameCore::SilhouetteDirection({3.0f, 5.0f, 0.0f}), L"only its bearing on the plane counts");
  }

  // G59: a condition's vital bar runs from 1 whole to 0 at FAIL_SHARE of the weakest vital module, and its hull bar is
  // the share of the hull that remains; a client finds the components by the names of their models.
  TEST_METHOD(ReadsAConditionFromAMask)
  {
    const std::vector<GameCore::CombatComponent> components{{std::nullopt, 0, 100},
                                                            {GameCore::ModuleKind::Command, 100, 20},
                                                            {GameCore::ModuleKind::Reactor, 120, 40},
                                                            {GameCore::ModuleKind::Weapon, 160, 10}};
    const GameCore::Condition whole = GameCore::ConditionOf(components, {});
    Assert::IsTrue(whole.vital == 1.0f && whole.hull == 1.0f, L"whole");
    const GameCore::Condition hit = GameCore::ConditionOf(components, Mask(170, {{0, 25}, {120, 10}, {160, 10}}));
    Assert::AreEqual(0.75f, hit.hull, L"a quarter of the hull gone");
    Assert::AreEqual(0.5f, hit.vital, L"the reactor at 0.75, half way to its failure");
    const GameCore::Condition failing = GameCore::ConditionOf(components, Mask(170, {{100, 12}}));
    Assert::AreEqual(0.0f, failing.vital, L"the command module below its share");

    const Fitted gunship = Fit("Gunship");
    const std::vector<GameCore::CombatComponent> named = GameCore::ComponentsOf(gunship.names, gunship.models, gunship.composite);
    const GameCore::CombatProfile profile = ProfileOf(gunship);
    Assert::AreEqual(profile.components.size(), named.size());
    for (std::size_t component = 0; component < named.size(); ++component)
    {
      Assert::IsTrue(named[component].module == profile.components[component].module &&
                       named[component].firstVoxel == profile.components[component].firstVoxel &&
                       named[component].voxelCount == profile.components[component].voxelCount,
                     std::format(L"component {} as the design has it", component).c_str());
    }
  }

  // Design/MvpPlan.md §8.3: the mass driver's shells fly at 300 units/s out to 600 units, and the laser reaches 250; each
  // is a weapon module of the catalogue.
  TEST_METHOD(ArmsTheWeaponModules)
  {
    const GameCore::WeaponSpec* massDriver = GameCore::FindWeapon("MassDriver");
    const GameCore::WeaponSpec* laser = GameCore::FindWeapon("Laser");
    Assert::IsTrue(massDriver != nullptr && laser != nullptr, L"both weapons");
    Assert::IsTrue(massDriver->shot == GameCore::ShotKind::Shell && laser->shot == GameCore::ShotKind::Beam, L"a shell and a beam");
    Assert::AreEqual(600.0f, massDriver->rangeUnits);
    Assert::AreEqual(300.0f, massDriver->shellSpeedUnitsPerSecond);
    Assert::AreEqual(250.0f, laser->rangeUnits);
    for (const GameCore::WeaponSpec& weapon : GameCore::Weapons())
    {
      const GameCore::ModuleSpec* module = GameCore::FindModule(weapon.module);
      Assert::IsTrue(module != nullptr && module->kind == GameCore::ModuleKind::Weapon, Widen(weapon.module).c_str());
    }
    Assert::IsTrue(GameCore::FindWeapon("Thruster") == nullptr, L"no other module is a weapon");
  }
};

} // namespace GameCoreTests
