#include "pch.h"

#include "SkirmishLayout.h"

#include "Float3.h"
#include "RigidTransform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{
namespace
{

using GameCore::Anchor;
using NeuronCore::Int3;

// The seeds the symmetry is held to (Design/MvpPlan.md §5, phase 2).
constexpr std::uint32_t SYMMETRY_SEEDS = 20;

[[nodiscard]] bool SameRotation(const NeuronCore::Rotation& _a, const NeuronCore::Rotation& _b) noexcept
{
  const auto same = [](NeuronCore::Float3 _u, NeuronCore::Float3 _v) { return _u.x == _v.x && _u.y == _v.y && _u.z == _v.z; };
  return same(_a.axisX, _b.axisX) && same(_a.axisY, _b.axisY) && same(_a.axisZ, _b.axisZ);
}

[[nodiscard]] bool SameAnchor(const Anchor& _a, const Anchor& _b) noexcept
{
  return _a.position.x == _b.position.x && _a.position.y == _b.position.y && _a.position.z == _b.position.z &&
         SameRotation(_a.turn, _b.turn);
}

[[nodiscard]] double Distance(Int3 _a, Int3 _b) noexcept
{
  const double x = static_cast<double>(_a.x) - _b.x;
  const double y = static_cast<double>(_a.y) - _b.y;
  const double z = static_cast<double>(_a.z) - _b.z;
  return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] std::size_t HalfAsteroids() noexcept
{
  std::size_t count = 0;
  for (const GameCore::FieldSpec& field : GameCore::HALF_FIELDS)
  {
    count += field.asteroids;
  }
  return count;
}

} // namespace

// Design/MvpPlan.md §2.1 and §8.4, Design/GameConcept.md §7.1 and G66, Design/ADR/ADR-030: the skirmish's layout from its
// seed, symmetric under a half turn with the sides exchanged, aligned, and where the plan puts it.
TEST_CLASS(SkirmishLayoutTests)
{
public:
  // Everything of the layout has its image under the half turn, of the same design or model, with the sides exchanged:
  // looked for among all of it, not only where the generator put it.
  TEST_METHOD(IsItsOwnHalfTurn)
  {
    for (std::uint32_t seed = 1; seed <= SYMMETRY_SEEDS; ++seed)
    {
      const GameCore::SkirmishLayout layout = GameCore::MakeSkirmishLayout(seed);
      for (const GameCore::LayoutUnit& unit : layout.units)
      {
        const Anchor image = GameCore::HalfTurn(unit.anchor);
        const auto mirrored = std::ranges::count_if(
          layout.units, [&](const GameCore::LayoutUnit& _other)
          { return _other.design == unit.design && _other.side == 3 - unit.side && SameAnchor(_other.anchor, image); });
        Assert::AreEqual(1, static_cast<int>(mirrored), std::format(L"seed {}: a unit's image, of the other side", seed).c_str());
      }
      for (const GameCore::LayoutAsteroid& asteroid : layout.asteroids)
      {
        const Anchor image = GameCore::HalfTurn(asteroid.anchor);
        const auto mirrored = std::ranges::count_if(layout.asteroids, [&](const GameCore::LayoutAsteroid& _other)
                                                    { return _other.model == asteroid.model && SameAnchor(_other.anchor, image); });
        Assert::AreEqual(1, static_cast<int>(mirrored), std::format(L"seed {}: an asteroid's image", seed).c_str());
      }
      // And the half turn is its own inverse, so the images are the layout again.
      for (const GameCore::LayoutAsteroid& asteroid : layout.asteroids)
      {
        Assert::IsTrue(SameAnchor(asteroid.anchor, GameCore::HalfTurn(GameCore::HalfTurn(asteroid.anchor))), L"two half turns");
      }
    }
  }

  // The cores at ±1,800 on x, each side's two miners and two gunships in front of its core, and everything facing the
  // other side.
  TEST_METHOD(StartsEachSideAtItsCore)
  {
    const GameCore::SkirmishLayout layout = GameCore::MakeSkirmishLayout(7);
    Assert::AreEqual(std::size_t{10}, layout.units.size(), L"a core and four ships a side");
    for (const std::uint8_t side : {std::uint8_t{1}, std::uint8_t{2}})
    {
      const float facing = side == 1 ? 1.0f : -1.0f;
      std::size_t miners = 0;
      std::size_t gunships = 0;
      std::size_t cores = 0;
      for (const GameCore::LayoutUnit& unit : layout.units)
      {
        if (unit.side != side)
        {
          continue;
        }
        Assert::IsTrue(unit.anchor.turn.axisZ.x == facing && unit.anchor.turn.axisZ.y == 0.0f && unit.anchor.turn.axisZ.z == 0.0f,
                       L"its front toward the other side");
        Assert::IsTrue(unit.anchor.turn.axisY.y == 1.0f, L"upright");
        cores += unit.design == GameCore::CORE_DESIGN ? 1 : 0;
        miners += unit.design == "Miner" ? 1 : 0;
        gunships += unit.design == "Gunship" ? 1 : 0;
        const std::int32_t coreX = side == 1 ? -1800 : 1800;
        if (unit.design == GameCore::CORE_DESIGN)
        {
          Assert::IsTrue(unit.anchor.position.x == coreX && unit.anchor.position.y == 0 && unit.anchor.position.z == 0, L"the core");
        }
        else
        {
          Assert::IsTrue((unit.anchor.position.x - coreX) * static_cast<std::int32_t>(facing) > 100, L"in front of its core");
        }
      }
      Assert::AreEqual(std::size_t{1}, cores);
      Assert::AreEqual(std::size_t{2}, miners);
      Assert::AreEqual(std::size_t{2}, gunships);
    }
  }

  // Design/ADR/ADR-035: the battle a skirmish stages keeps the cores and the fields, and gives each side its whole
  // command budget of 20 points in combat ships in place of its own, facing the other side, out of each other's sensors.
  TEST_METHOD(StagesABattleOfTheWholeBudget)
  {
    const std::map<std::string_view, std::uint32_t> commandPoints{{"Gunship", 1}, {"Lancer", 1}, {"Cruiser", 4}};
    const GameCore::SkirmishLayout skirmish = GameCore::MakeSkirmishLayout(7);
    const GameCore::SkirmishLayout battle = GameCore::MakeBattleLayout(7);
    Assert::AreEqual(skirmish.asteroids.size(), battle.asteroids.size(), L"the same fields");
    for (std::size_t asteroid = 0; asteroid < battle.asteroids.size(); ++asteroid)
    {
      Assert::IsTrue(SameAnchor(skirmish.asteroids[asteroid].anchor, battle.asteroids[asteroid].anchor), L"and asteroids");
    }
    const std::size_t sideUnits = battle.units.size() / 2;
    for (std::size_t unit = 0; unit < sideUnits; ++unit)
    {
      Assert::IsTrue(battle.units[unit + sideUnits].design == battle.units[unit].design &&
                       SameAnchor(battle.units[unit + sideUnits].anchor, GameCore::HalfTurn(battle.units[unit].anchor)),
                     L"side 2's the half turn of side 1's");
    }
    std::uint32_t points = 0;
    std::int32_t nearest = std::numeric_limits<std::int32_t>::max();
    for (const GameCore::LayoutUnit& unit : battle.units)
    {
      if (unit.side != 1)
      {
        continue;
      }
      if (unit.design == GameCore::CORE_DESIGN)
      {
        Assert::IsTrue(unit.anchor.position.x == GameCore::CORE_ANCHOR.x, L"the core where it was");
        continue;
      }
      points += commandPoints.at(unit.design);
      nearest = std::min(nearest, -2 * unit.anchor.position.x);
      Assert::IsTrue(unit.anchor.turn.axisZ.x == 1.0f, L"facing side 2");
    }
    Assert::AreEqual(std::uint32_t{20}, points, L"the whole budget");
    Assert::IsTrue(nearest > 700, L"out of the other side's frigates' sensors");
  }

  // Six fields: two near each core and two in the middle, each asteroid within its field's radius on the plane and a
  // little above or below it, aligned, and apart from its field's others. No core sees the middle at the start.
  TEST_METHOD(FillsTheFieldsWhereThePlanPutsThem)
  {
    for (std::uint32_t seed = 1; seed <= SYMMETRY_SEEDS; ++seed)
    {
      const GameCore::SkirmishLayout layout = GameCore::MakeSkirmishLayout(seed);
      Assert::AreEqual(2 * HalfAsteroids(), layout.asteroids.size(), std::format(L"seed {}: every field full", seed).c_str());
      std::vector<GameCore::FieldSpec> fields(GameCore::HALF_FIELDS.begin(), GameCore::HALF_FIELDS.end());
      for (const GameCore::FieldSpec& field : GameCore::HALF_FIELDS)
      {
        fields.push_back({GameCore::HalfTurn(field.center), field.radius, field.asteroids});
      }
      std::vector<std::size_t> perField(fields.size(), 0);
      for (std::size_t index = 0; index < layout.asteroids.size(); ++index)
      {
        const GameCore::LayoutAsteroid& asteroid = layout.asteroids[index];
        const std::wstring what = std::format(L"seed {}, asteroid {}", seed, index);
        Assert::IsTrue(NeuronCore::IsCubeSymmetry(asteroid.anchor.turn), (what + L": turned by quarter turns").c_str());
        Assert::IsTrue(asteroid.model < GameCore::ASTEROID_MODELS.size(), (what + L": a model").c_str());
        Assert::IsTrue(std::abs(asteroid.anchor.position.y) <= GameCore::ASTEROID_HEIGHT, (what + L": near the plane").c_str());
        const auto field = std::ranges::find_if(fields,
                                                [&asteroid](const GameCore::FieldSpec& _field)
                                                {
                                                  const std::int64_t x = asteroid.anchor.position.x - _field.center.x;
                                                  const std::int64_t z = asteroid.anchor.position.z - _field.center.z;
                                                  return x * x + z * z <= std::int64_t{_field.radius} * _field.radius;
                                                });
        Assert::IsTrue(field != fields.end(), (what + L": within a field").c_str());
        ++perField[static_cast<std::size_t>(field - fields.begin())];
        for (std::size_t other = 0; other < index; ++other)
        {
          Assert::IsTrue(Distance(asteroid.anchor.position, layout.asteroids[other].anchor.position) >= GameCore::ASTEROID_SPACING,
                         (what + L": apart from the others").c_str());
        }
        if (field->radius > GameCore::HALF_FIELDS.front().radius)
        {
          for (const Int3 core : {GameCore::CORE_ANCHOR, GameCore::HalfTurn(GameCore::CORE_ANCHOR)})
          {
            Assert::IsTrue(Distance(asteroid.anchor.position, core) > 1200.0, (what + L": beyond the core's sensor array").c_str());
          }
        }
      }
      for (std::size_t field = 0; field < fields.size(); ++field)
      {
        Assert::AreEqual(fields[field].asteroids, static_cast<std::uint32_t>(perField[field]), std::format(L"field {}", field).c_str());
      }
    }
  }

  // The same seed lays out the same skirmish; another seed another.
  TEST_METHOD(IsItsSeedsAlone)
  {
    const GameCore::SkirmishLayout first = GameCore::MakeSkirmishLayout(12345);
    const GameCore::SkirmishLayout again = GameCore::MakeSkirmishLayout(12345);
    const GameCore::SkirmishLayout other = GameCore::MakeSkirmishLayout(12346);
    Assert::AreEqual(first.asteroids.size(), again.asteroids.size());
    bool otherDiffers = false;
    for (std::size_t index = 0; index < first.asteroids.size(); ++index)
    {
      Assert::IsTrue(first.asteroids[index].model == again.asteroids[index].model &&
                       SameAnchor(first.asteroids[index].anchor, again.asteroids[index].anchor),
                     L"the same seed, the same asteroid");
      otherDiffers = otherDiffers || !SameAnchor(first.asteroids[index].anchor, other.asteroids[index].anchor);
    }
    Assert::IsTrue(otherDiffers, L"another seed, other asteroids");
  }

  // The cube's 24 rotations, each once; and an anchored thing stands exactly where its anchor and its middle say, at
  // whole and half voxels, with its half turn exactly the image of where it stands.
  TEST_METHOD(AnchorsExactly)
  {
    const auto rotations = GameCore::CubeRotations();
    for (std::size_t i = 0; i < rotations.size(); ++i)
    {
      Assert::IsTrue(NeuronCore::IsCubeSymmetry(rotations[i]), L"one of the cube's");
      for (std::size_t j = 0; j < i; ++j)
      {
        Assert::IsFalse(SameRotation(rotations[i], rotations[j]), L"each once");
      }
    }
    const NeuronCore::Float3 middle{8.5f, 9.0f, 9.5f};
    for (const NeuronCore::Rotation& turn : rotations)
    {
      const Anchor anchor{{-1403, 7, 512}, turn};
      const NeuronCore::Float3 position = GameCore::AnchoredPosition(anchor, middle);
      const NeuronCore::Float3 offset = NeuronCore::RotateVector(turn, {0.5f, 0.0f, 0.5f});
      Assert::IsTrue(position.x == -1403.0f + offset.x && position.y == 7.0f + offset.y && position.z == 512.0f + offset.z,
                     L"the anchor, and the middle's offset from its cell, turned");
      const NeuronCore::Float3 image = GameCore::AnchoredPosition(GameCore::HalfTurn(anchor), middle);
      Assert::IsTrue(image.x == -position.x && image.y == position.y && image.z == -position.z, L"the half turn's image, exactly");
    }
  }
};

} // namespace GameCoreTests
