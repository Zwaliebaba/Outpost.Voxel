#include "pch.h"

#include "Catalogue.h"
#include "Design.h"
#include "Profile.h"
#include "TestSupport.h"

#include "NvfModel.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{
namespace
{

constexpr float DEGREES_PER_RADIAN = 180.0f / std::numbers::pi_v<float>;

// How far a pinned figure may move, as a share of it: the sums run in double and in one order, so GCC and MSVC agree to
// far better than this, and a change to a design, a module or a rule moves some figure by more.
constexpr float PINNED_SHARE = 1.0e-3f;

// How far a class's figures may land from the plan's targets (Design/MvpPlan.md §8.1).
constexpr float TARGET_SHARE = 0.25f;

// Each design's profile as this phase measured it (Design/MvpPlan.md §8.1), which ProfilesArePinned holds it to.
struct PinnedProfile
{
  std::string_view design;
  std::uint32_t voxels;
  std::uint32_t heavyVoxels;
  float mass;
  float accelerationUnitsPerSecondSquared;
  float turnAccelerationRadiansPerSecondSquared;
  float halfTurnSeconds;
  float sensorRangeUnits;
  float powerDraw;
  float powerSupply;
  float priceCredits;
  std::uint32_t commandPoints;
};

constexpr std::array<PinnedProfile, 5> PINNED{{
  {"Miner", 1210, 130, 1720.0f, 34.884f, 2.5101f, 4.3130f, 700.0f, 14.0f, 25.0f, 2480.0f, 1},
  {"Gunship", 1155, 498, 2391.0f, 25.094f, 1.6327f, 4.4811f, 700.0f, 19.0f, 25.0f, 3819.0f, 1},
  {"Lancer", 1119, 395, 2149.0f, 27.920f, 1.3016f, 4.6030f, 700.0f, 23.0f, 25.0f, 3674.0f, 1},
  {"Cruiser", 9017, 5707, 21001.0f, 5.7140f, 0.15189f, 23.419f, 700.0f, 39.0f, 80.0f, 28848.0f, 4},
  {"StationCore", 28955, 5264, 40943.0f, 0.0f, 0.0f, 0.0f, 1200.0f, 72.0f, 80.0f, 47097.0f, 0},
}};

// The plan's targets for a class's ships: acceleration in units/s² (Design/MvpPlan.md §8.1).
[[nodiscard]] float AccelerationTarget(GameCore::SizeClass _class) noexcept
{
  return _class == GameCore::SizeClass::Frigate ? 30.0f : 5.0f;
}

void AssertNear(float _expected, float _actual, std::wstring_view _what)
{
  Assert::AreEqual(_expected, _actual, std::abs(_expected) * PINNED_SHARE, std::wstring(_what).c_str());
}

[[nodiscard]] GameCore::Profile ProfileOf(std::string_view _design)
{
  return GameCore::ComputeProfile(LoadMvpDesign(_design));
}

} // namespace

// G18 and G29: what each design can do follows from its voxels, materials and modules (Design/GameConcept.md §5.4).
TEST_CLASS(ProfileTests)
{
public:
  // The table the phase's pull request quotes.
  TEST_METHOD(PrintsEachDesignsProfile)
  {
    std::wstring table = L"| Design | Class | Voxels | Heavy | Mass | Acceleration, units/s² | Turn acceleration, °/s² | Turn, °/s | Half "
                         L"turn, s | Speed, units/s "
                         L"| Sensor, units | Power | Price, credits | Build, s | Command points "
                         L"|\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const GameCore::DesignSpec& spec : GameCore::Designs())
    {
      const GameCore::Profile profile = ProfileOf(spec.name);
      table += std::format(
        L"| {} | {} | {} | {} | {:.0f} | {:.1f} | {:.1f} | {:.0f} | {:.1f} | {:.0f} | {:.0f} | {:.0f} of {:.0f} | {:.0f} | {:.1f} | {} |\n",
        Widen(spec.name), Widen(GameCore::SizeClassOf(profile.sizeClass).name), profile.voxels, profile.heavyVoxels, profile.mass,
        profile.accelerationUnitsPerSecondSquared, profile.turnAccelerationRadiansPerSecondSquared * DEGREES_PER_RADIAN,
        profile.turnRateRadiansPerSecond * DEGREES_PER_RADIAN, profile.halfTurnSeconds, profile.speedUnitsPerSecond,
        profile.sensorRangeUnits, profile.powerDraw, profile.powerSupply, profile.priceCredits, profile.buildSeconds,
        profile.commandPoints);
    }
    Logger::WriteMessage(table.c_str());
  }

  TEST_METHOD(ProfilesArePinned)
  {
    for (const PinnedProfile& pinned : PINNED)
    {
      const GameCore::Profile profile = ProfileOf(pinned.design);
      const std::wstring design = Widen(pinned.design);
      Assert::AreEqual(pinned.voxels, profile.voxels, design.c_str());
      Assert::AreEqual(pinned.heavyVoxels, profile.heavyVoxels, design.c_str());
      Assert::AreEqual(pinned.commandPoints, profile.commandPoints, design.c_str());
      AssertNear(pinned.mass, profile.mass, design + L"'s mass");
      AssertNear(pinned.accelerationUnitsPerSecondSquared, profile.accelerationUnitsPerSecondSquared, design + L"'s acceleration");
      AssertNear(pinned.turnAccelerationRadiansPerSecondSquared, profile.turnAccelerationRadiansPerSecondSquared, design + L"'s turn");
      AssertNear(pinned.sensorRangeUnits, profile.sensorRangeUnits, design + L"'s sensor");
      AssertNear(pinned.powerDraw, profile.powerDraw, design + L"'s draw");
      AssertNear(pinned.powerSupply, profile.powerSupply, design + L"'s supply");
      AssertNear(pinned.priceCredits, profile.priceCredits, design + L"'s price");
      AssertNear(pinned.priceCredits / GameCore::BUILD_CREDITS_PER_SECOND, profile.buildSeconds, design + L"'s build time");
      if (pinned.halfTurnSeconds > 0.0f)
      {
        AssertNear(pinned.halfTurnSeconds, profile.halfTurnSeconds, design + L"'s half turn");
      }
      else
      {
        Assert::IsTrue(std::isinf(profile.halfTurnSeconds), (design + L" never turns").c_str());
      }
    }
  }

  // Each ship accelerates within a quarter of its class's target, and turns half a turn within a quarter of the time
  // its class's rate alone would take (Design/MvpPlan.md §8.1).
  TEST_METHOD(ShipsLandNearTheirClassesTargets)
  {
    for (const GameCore::DesignSpec& spec : GameCore::Designs())
    {
      if (spec.kind != GameCore::DesignKind::Ship)
      {
        continue;
      }
      const GameCore::Profile profile = ProfileOf(spec.name);
      const GameCore::SizeClassSpec& sizeClass = GameCore::SizeClassOf(profile.sizeClass);
      const float target = AccelerationTarget(profile.sizeClass);
      Assert::AreEqual(target, profile.accelerationUnitsPerSecondSquared, target * TARGET_SHARE,
                       std::format(L"{} accelerates at {}", Widen(spec.name), profile.accelerationUnitsPerSecondSquared).c_str());
      const float capOnly = std::numbers::pi_v<float> / sizeClass.turnRateCapRadiansPerSecond;
      Assert::AreEqual(capOnly, profile.halfTurnSeconds, capOnly * TARGET_SHARE,
                       std::format(L"{} turns half a turn in {} s", Widen(spec.name), profile.halfTurnSeconds).c_str());
      Assert::AreEqual(sizeClass.speedCapUnitsPerSecond, profile.speedUnitsPerSecond);
    }
  }

  // Every fit runs within its reactors, which validation already holds it to.
  TEST_METHOD(EveryFitRunsWithinItsSupply)
  {
    for (const GameCore::DesignSpec& spec : GameCore::Designs())
    {
      const GameCore::Profile profile = ProfileOf(spec.name);
      Assert::IsTrue(profile.powerDraw <= profile.powerSupply, Widen(spec.name).c_str());
      Assert::AreEqual(0u, profile.blockedLines, Widen(spec.name).c_str());
    }
  }

  // Heavy armor weighs three times light and costs four times as much (G13): the gunship in light armor alone is lighter,
  // quicker and cheaper, and in heavy alone heavier, slower and dearer.
  TEST_METHOD(ArmorWeighsAndCosts)
  {
    const GameCore::DesignSpec gunship = *GameCore::FindDesign("Gunship");
    const NeuronCore::NvfModel hull = LoadModel("Gunship");
    const GameCore::Profile profile = GameCore::ComputeProfile(LoadMvpDesign("Gunship"));
    GameCore::DesignSpec light = gunship;
    light.materials.fill(GameCore::MaterialClass::Light);
    const auto lightDesign = GameCore::ValidateDesign(light, hull);
    Assert::AreEqual(std::string("Accepted"), RefusalOf(lightDesign));
    const GameCore::Profile lightProfile = GameCore::ComputeProfile(*lightDesign);
    GameCore::DesignSpec heavy = gunship;
    heavy.materials.fill(GameCore::MaterialClass::Heavy);
    const auto heavyDesign = GameCore::ValidateDesign(heavy, hull);
    Assert::AreEqual(std::string("Accepted"), RefusalOf(heavyDesign));
    const GameCore::Profile heavyProfile = GameCore::ComputeProfile(*heavyDesign);

    Assert::AreEqual(0u, lightProfile.heavyVoxels);
    Assert::AreEqual(heavyProfile.voxels, heavyProfile.heavyVoxels);
    const float lightVoxels = static_cast<float>(profile.voxels - profile.heavyVoxels);
    const float heavyVoxels = static_cast<float>(profile.heavyVoxels);
    AssertNear(2.0f * heavyVoxels, profile.mass - lightProfile.mass, L"a heavy voxel weighs 3, and a light one 1");
    AssertNear(2.0f * lightVoxels, heavyProfile.mass - profile.mass, L"a heavy voxel weighs 3, and a light one 1");
    AssertNear(3.0f * heavyVoxels, profile.priceCredits - lightProfile.priceCredits, L"a heavy voxel costs 4, and a light one 1");
    AssertNear(3.0f * lightVoxels, heavyProfile.priceCredits - profile.priceCredits, L"a heavy voxel costs 4, and a light one 1");
    Assert::IsTrue(lightProfile.accelerationUnitsPerSecondSquared > profile.accelerationUnitsPerSecondSquared &&
                   profile.accelerationUnitsPerSecondSquared > heavyProfile.accelerationUnitsPerSecondSquared);
    Assert::IsTrue(lightProfile.turnAccelerationRadiansPerSecondSquared > profile.turnAccelerationRadiansPerSecondSquared &&
                   profile.turnAccelerationRadiansPerSecondSquared > heavyProfile.turnAccelerationRadiansPerSecondSquared);
  }

  // A long hull has more inertia and turns more slowly (the concept's §5.4): the lancer against the gunship.
  TEST_METHOD(ALongHullTurnsMoreSlowly)
  {
    const GameCore::Profile gunship = ProfileOf("Gunship");
    const GameCore::Profile lancer = ProfileOf("Lancer");
    Assert::IsTrue(lancer.inertia.y > gunship.inertia.y, L"the needle is harder to swing");
    Assert::IsTrue(lancer.turnAccelerationRadiansPerSecondSquared < gunship.turnAccelerationRadiansPerSecondSquared);
    Assert::IsTrue(lancer.halfTurnSeconds > gunship.halfTurnSeconds);
  }

  // An engine whose line the hull blocks gives no thrust and no turn, and counts as blocked (G38).
  TEST_METHOD(ABlockedEngineGivesNothing)
  {
    NeuronCore::NvfModel hull = LoadModel("Gunship");
    AddPart(hull, "hull/tail", TAIL_TRANSLATION, TAIL_SIZE, TAIL_CELLS);
    const GameCore::DesignSpec& spec = *GameCore::FindDesign("Gunship");
    const auto design = GameCore::ValidateDesign(spec, hull);
    Assert::AreEqual(std::string("Accepted"), RefusalOf(design));
    const GameCore::Profile profile = GameCore::ComputeProfile(*design);
    const GameCore::ModuleSpec& thruster = *GameCore::FindModule("Thruster");
    Assert::AreEqual(1u, profile.blockedLines);
    AssertNear(thruster.thrust, profile.thrustPositive.z, L"one thruster's thrust, not two");
    Assert::IsTrue(profile.turnAccelerationRadiansPerSecondSquared < ProfileOf("Gunship").turnAccelerationRadiansPerSecondSquared);
  }

  // An aft engine pushes the ship forward, and only forward.
  TEST_METHOD(AnEnginePushesAgainstTheWayItFaces)
  {
    const GameCore::Profile cruiser = ProfileOf("Cruiser");
    AssertNear(4.0f * GameCore::FindModule("Thruster")->thrust, cruiser.thrustPositive.z, L"four thrusters forward");
    Assert::AreEqual(0.0f, cruiser.thrustNegative.z);
    Assert::AreEqual(0.0f, cruiser.thrustPositive.x + cruiser.thrustNegative.x + cruiser.thrustPositive.y + cruiser.thrustNegative.y);
  }

  // The core stands: no thrust, no speed, no turn, and no command points (G56).
  TEST_METHOD(AStructureNeitherMovesNorTurns)
  {
    const GameCore::Profile core = ProfileOf("StationCore");
    Assert::AreEqual(0.0f, core.accelerationUnitsPerSecondSquared);
    Assert::AreEqual(0.0f, core.speedUnitsPerSecond);
    Assert::IsTrue(std::isinf(core.halfTurnSeconds));
    Assert::AreEqual(0u, core.commandPoints);
  }
};

} // namespace GameCoreTests
