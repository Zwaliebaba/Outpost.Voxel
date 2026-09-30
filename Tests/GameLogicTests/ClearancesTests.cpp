#include "pch.h"

#include "Clearances.h"
#include "Route.h"

#include "Float3.h"
#include "Hash.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <span>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{
namespace
{

using NeuronCore::Float3;

// A ship's sphere in these tests, near a gunship's.
constexpr float SHIP_RADIUS = 15.0f;

// A keep-out and a path circle about an obstacle whose sphere is _radius, for SHIP_RADIUS, as ADR-033 draws them.
[[nodiscard]] constexpr float KeepOut(float _radius) noexcept
{
  return _radius + SHIP_RADIUS + GameLogic::CLEARANCE_MARGIN;
}

[[nodiscard]] constexpr float PathCircle(float _radius) noexcept
{
  return KeepOut(_radius) + GameLogic::PATH_SLACK;
}

// How far into a circle a leg or a point may seem to reach, in units: float rounding some thousands of units out.
constexpr double ROUNDING_UNITS = 1.0e-3;

[[nodiscard]] double PlaneDistance(Float3 _a, Float3 _b) noexcept
{
  return std::hypot(static_cast<double>(_b.x) - _a.x, static_cast<double>(_b.z) - _a.z);
}

// How near the leg from _from to _to comes to _center on the plane, worked out here in double precision.
[[nodiscard]] double LegDistance(Float3 _from, Float3 _to, Float3 _center) noexcept
{
  const double dx = static_cast<double>(_to.x) - _from.x;
  const double dz = static_cast<double>(_to.z) - _from.z;
  const double lengthSquared = dx * dx + dz * dz;
  double along = lengthSquared > 0.0
                   ? ((static_cast<double>(_center.x) - _from.x) * dx + (static_cast<double>(_center.z) - _from.z) * dz) / lengthSquared
                   : 0.0;
  along = along < 0.0 ? 0.0 : (along > 1.0 ? 1.0 : along);
  return std::hypot(_from.x + along * dx - _center.x, _from.z + along * dz - _center.z);
}

// Every leg of _path, from _from, keeps outside every obstacle's path circle.
void CheckLegs(Float3 _from, std::span<const Float3> _path, std::span<const GameLogic::Obstacle> _obstacles, const wchar_t* _what)
{
  Float3 from = _from;
  for (const Float3& to : _path)
  {
    for (const GameLogic::Obstacle& obstacle : _obstacles)
    {
      Assert::IsTrue(LegDistance(from, to, obstacle.center) >= PathCircle(obstacle.radius) - ROUNDING_UNITS, _what);
    }
    from = to;
  }
}

[[nodiscard]] double LengthOf(Float3 _from, std::span<const Float3> _path) noexcept
{
  double length = 0.0;
  Float3 from = _from;
  for (const Float3& to : _path)
  {
    length += PlaneDistance(from, to);
    from = to;
  }
  return length;
}

// A unit number, and a draw between _low and _high, from PcgHash of an index and a stream offset by _seed (R21).
[[nodiscard]] float Draw(std::uint32_t _seed, std::uint32_t _index, float _low, float _high) noexcept
{
  const std::uint32_t bits = NeuronCore::PcgHash(_index * 16u + 0x3Cu + _seed * 0x9E3779B9u);
  return _low + (_high - _low) * (static_cast<float>(bits >> 8u) / 16777216.0f);
}

} // namespace

// Design/ADR/ADR-033: where a ship may go on the plane and the ways there, around the cores' and asteroids' spheres.
TEST_CLASS(ClearancesTests)
{
public:
  // A point outside every path circle is where a move goes; one within goes out along its ray from the center, or, where
  // circles overlap, to where they meet, whichever is nearest outside them all; and it keeps its height.
  TEST_METHOD(TakesAMoveToTheNearestPointOutside)
  {
    const std::array<GameLogic::Obstacle, 1> one{{{{0.0f, 0.0f, 0.0f}, 20.0f}}};
    const GameLogic::Clearances alone(one, SHIP_RADIUS);
    const Float3 free{100.0f, 3.0f, 0.0f};
    const Float3 kept = alone.Reachable(free);
    Assert::IsTrue(kept.x == free.x && kept.y == free.y && kept.z == free.z, L"a point outside stays");
    const Float3 inside = alone.Reachable({30.0f, 3.0f, 0.0f});
    Assert::AreEqual(static_cast<double>(PathCircle(20.0f)), PlaneDistance(inside, {0.0f, 0.0f, 0.0f}), 0.1, L"out to the circle");
    Assert::IsTrue(inside.z == 0.0f && inside.x > 0.0f && inside.y == 3.0f, L"along its ray, at its height");
    const Float3 middle = alone.Reachable({0.0f, 0.0f, 0.0f});
    Assert::IsTrue(PlaneDistance(middle, {0.0f, 0.0f, 0.0f}) >= PathCircle(20.0f), L"the center goes somewhere outside");

    // Two circles of 57 about centers 80 apart: from within both, out to where they meet.
    const std::array<GameLogic::Obstacle, 2> two{{{{0.0f, 0.0f, 0.0f}, 20.0f}, {{80.0f, 0.0f, 0.0f}, 20.0f}}};
    const GameLogic::Clearances pair(two, SHIP_RADIUS);
    const Float3 between = pair.Reachable({40.0f, 0.0f, 5.0f});
    const double reach = PathCircle(20.0f) + 0.05;
    Assert::AreEqual(40.0, static_cast<double>(between.x), 0.01, L"where the circles meet: x");
    Assert::AreEqual(std::sqrt(reach * reach - 40.0 * 40.0), static_cast<double>(between.z), 0.01,
                     L"where the circles meet: z, the nearer side");
  }

  // A clear line is the whole way; one blocked goes around, its corners and legs outside every path circle, a little
  // longer than the shortest way around the circle itself, as its octagon is.
  TEST_METHOD(FindsTheShortestWayAround)
  {
    const std::array<GameLogic::Obstacle, 1> one{{{{0.0f, 0.0f, 0.0f}, 20.0f}}};
    const GameLogic::Clearances clearances(one, SHIP_RADIUS);
    const Float3 from{-300.0f, 4.0f, 10.0f};
    const Float3 open{-300.0f, 0.0f, 400.0f};
    const std::vector<Float3> straight = clearances.Path(from, open);
    Assert::AreEqual(std::size_t{1}, straight.size(), L"a clear line is one leg");

    const Float3 to{300.0f, 0.0f, -10.0f};
    const std::vector<Float3> around = clearances.Path(from, to);
    Assert::IsTrue(around.size() >= 2, L"it turns at a corner");
    Assert::IsTrue(around.back().x == to.x && around.back().z == to.z, L"it ends where it was sent");
    for (const Float3& point : around)
    {
      Assert::AreEqual(from.y, point.y, L"every point at the start's height");
    }
    CheckLegs(from, around, one, L"every leg clears the path circle");

    // The shortest way around a circle of radius r: a tangent from each end, and the arc between them.
    const double r = PathCircle(20.0f);
    const double a = std::hypot(300.0, 10.0);
    const double tangents = 2.0 * std::sqrt(a * a - r * r);
    const double gap = std::acos((static_cast<double>(from.x) * to.x + static_cast<double>(from.z) * to.z) / (a * a));
    const double shortest = tangents + r * (gap - 2.0 * std::acos(r / a));
    const double length = LengthOf(from, around);
    Assert::IsTrue(length >= shortest - 0.01 && length <= shortest * 1.02, std::format(L"{} against {}", length, shortest).c_str());
  }

  // On 20 seeds of fields of 30 obstacles, every way between two points a move could go keeps every leg outside every
  // path circle, and is no shorter than the line.
  TEST_METHOD(KeepsEveryLegClearOnRandomFields)
  {
    constexpr std::uint32_t SEEDS = 20;
    constexpr std::uint32_t OBSTACLES = 30;
    constexpr std::uint32_t WAYS = 20;
    std::size_t corners = 0;
    std::size_t ways = 0;
    std::size_t shortOfGoal = 0;
    for (std::uint32_t seed = 1; seed <= SEEDS; ++seed)
    {
      std::uint32_t draw = 0;
      std::vector<GameLogic::Obstacle> field;
      for (std::uint32_t obstacle = 0; obstacle < OBSTACLES; ++obstacle)
      {
        const float x = Draw(seed, draw++, -1000.0f, 1000.0f);
        const float z = Draw(seed, draw++, -1000.0f, 1000.0f);
        const float y = Draw(seed, draw++, -20.0f, 20.0f);
        field.push_back({{x, y, z}, Draw(seed, draw++, 10.0f, 45.0f)});
      }
      const GameLogic::Clearances clearances(field, SHIP_RADIUS);
      corners += clearances.CornerCount();
      for (std::uint32_t way = 0; way < WAYS; ++way)
      {
        const float fromX = Draw(seed, draw++, -1200.0f, 1200.0f);
        const float fromZ = Draw(seed, draw++, -1200.0f, 1200.0f);
        const float toX = Draw(seed, draw++, -1200.0f, 1200.0f);
        const float toZ = Draw(seed, draw++, -1200.0f, 1200.0f);
        const Float3 from = clearances.Reachable({fromX, 0.0f, fromZ});
        const Float3 to = clearances.Reachable({toX, 0.0f, toZ});
        const std::vector<Float3> path = clearances.Path(from, to);
        const std::wstring what = std::format(L"seed {}, way {}", seed, way);
        Assert::IsFalse(path.empty(), what.c_str());
        CheckLegs(from, path, field, what.c_str());
        const bool reached = path.back().x == to.x && path.back().z == to.z;
        shortOfGoal += reached ? 0 : 1;
        Assert::IsTrue(!reached || LengthOf(from, path) >= PlaneDistance(from, to) - 0.01, what.c_str());
        ++ways;
      }
    }
    Logger::WriteMessage(std::format(L"{} ways over {} seeds, {} of them short of an enclosed goal; {:.1f} corners a field\n", ways, SEEDS,
                                     shortOfGoal, static_cast<double>(corners) / SEEDS)
                           .c_str());
  }

  // A goal that a ring of obstacles encloses is out of reach: the way leads to the corner reached nearest to it, outside
  // the ring, and a ship in a path's slack still finds its way out.
  TEST_METHOD(LeadsAsNearAsItCanToAGoalItCannotReach)
  {
    std::vector<GameLogic::Obstacle> ring;
    for (std::uint32_t index = 0; index < 12; ++index)
    {
      const float angle = static_cast<float>(index) * (std::numbers::pi_v<float> / 6.0f);
      ring.push_back({{150.0f * std::cos(angle), 0.0f, 150.0f * std::sin(angle)}, 30.0f});
    }
    const GameLogic::Clearances clearances(ring, SHIP_RADIUS);
    const Float3 from{-500.0f, 0.0f, 0.0f};
    const std::vector<Float3> path = clearances.Path(from, {0.0f, 0.0f, 0.0f});
    Assert::IsFalse(path.empty(), L"a way");
    CheckLegs(from, path, ring, L"its legs clear the ring");
    Assert::IsTrue(PlaneDistance(path.back(), {0.0f, 0.0f, 0.0f}) > 150.0, L"it ends outside the ring");

    // From within a path circle's slack, outside its keep-out, a way out and around.
    const Float3 slack{150.0f + KeepOut(30.0f) + 5.0f, 0.0f, 0.0f};
    const std::vector<Float3> out = clearances.Path(slack, {600.0f, 0.0f, 300.0f});
    Assert::IsFalse(out.empty(), L"a way out of the slack");
    Assert::IsTrue(out.back().x == 600.0f && out.back().z == 300.0f, L"all the way");
  }

  // A point within a keep-out goes to the nearest point outside every keep-out, even where two overlap; one outside stays;
  // and Clearance measures how far outside the nearest keep-out a point lies.
  TEST_METHOD(KeepsPointsOutOfEveryKeepOut)
  {
    const std::array<GameLogic::Obstacle, 2> two{{{{0.0f, 0.0f, 0.0f}, 20.0f}, {{60.0f, 0.0f, 0.0f}, 20.0f}}};
    const GameLogic::Clearances clearances(two, SHIP_RADIUS);
    const Float3 outside{0.0f, 2.0f, 100.0f};
    const Float3 kept = clearances.KeptOut(outside);
    Assert::IsTrue(kept.x == outside.x && kept.y == outside.y && kept.z == outside.z, L"a point outside stays");
    Assert::AreEqual(100.0 - KeepOut(20.0f), static_cast<double>(clearances.Clearance(outside)), 1.0e-3, L"its clearance");

    const Float3 out = clearances.KeptOut({-30.0f, 2.0f, 1.0f});
    Assert::IsTrue(clearances.Clearance(out) >= 0.0f && clearances.Clearance(out) < 0.1f, L"out to the keep-out's edge");
    Assert::AreEqual(2.0f, out.y, L"at its height");

    // Keep-outs of 45 about centers 60 apart overlap: from between them, out to where they meet.
    const Float3 between = clearances.KeptOut({30.0f, 0.0f, 1.0f});
    Assert::IsTrue(clearances.Clearance(between) >= 0.0f, L"outside both");
    Assert::AreEqual(30.0, static_cast<double>(between.x), 0.01, L"where they meet");
    Assert::IsTrue(clearances.Clearance({30.0f, 0.0f, 1.0f}) < 0.0f, L"from within");
  }
};

} // namespace GameLogicTests
