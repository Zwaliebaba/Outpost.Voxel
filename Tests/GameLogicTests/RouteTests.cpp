#include "pch.h"

#include "Route.h"
#include "Sector.h"
#include "ShipMotion.h"
#include "TestSupport.h"

#include "Float3.h"
#include "Message.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{
namespace
{

using NeuronCore::Float3;

// The seeds the route tests build the default sector from.
constexpr std::array<std::uint32_t, 3> SEEDS{1, 2, 3};

// How far apart two pieces may join, in units: float rounding some thousands of units from the origin, where the widest
// measured gap is 0.0013.
constexpr float JOINT_TOLERANCE = 0.01f;

// A route of two orbits 2,000 units apart, each flown two laps, so that every point of its first orbit lies on the
// route twice, a lap apart.
[[nodiscard]] GameLogic::Route TwoOrbits()
{
  constexpr Float3 UP{0.0f, 1.0f, 0.0f};
  const std::array<GameLogic::Orbit, 2> orbits{{{{0.0f, 0.0f, 0.0f}, UP, 400.0f, 2, false}, {{2000.0f, 0.0f, 0.0f}, UP, 400.0f, 2, false}}};
  return GameLogic::BuildRoute(orbits, {});
}

} // namespace

// Design/Archive/SpaceScene.md §5.2 and §15: every route closes, and keeps its flights clear of the stations; and the route's
// geometry that the flight reads (Design/ADR/ADR-017): its direction, how a ship is tracked along it, and how far apart
// two points of it lie.
TEST_CLASS(RouteTests)
{
public:
  // Each piece starts where the one before it ends, the last ends where the first starts, and each starts as far along
  // the route as the pieces before it are long.
  TEST_METHOD(EveryRouteCloses)
  {
    float widest = 0.0f;
    for (const std::uint32_t seed : SEEDS)
    {
      const auto sector = MakeSector({.seed = seed});
      for (std::size_t flight = 0; flight < sector->FlightCount(); ++flight)
      {
        const GameLogic::Route& route = sector->FlightRoute(flight);
        const std::span<const GameLogic::RoutePiece> pieces = route.Pieces();
        Assert::IsFalse(pieces.empty(), L"a route has a piece");
        float distance = 0.0f;
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
          const float gap = NeuronCore::Length(pieces[(i + 1) % pieces.size()].start - pieces[i].end);
          widest = std::max(widest, gap);
          Assert::IsTrue(gap <= JOINT_TOLERANCE,
                         std::format(L"seed {}, flight {}: piece {} ends {} from the next's start", seed, flight, i, gap).c_str());
          Assert::AreEqual(distance, pieces[i].distance, L"a piece starts where the ones before it end");
          distance += pieces[i].length;
        }
        Assert::AreEqual(distance, route.Length(), L"the route is as long as its pieces");
      }
    }
    Logger::WriteMessage(std::format(L"the widest joint is {} units\n", widest).c_str());
  }

  // Every point of every route, eight units apart, lies further from every station's center than its ships' keep-out
  // radius and its wingmen's reach to either side: the orbits are sized for it and the transits bent around the stations.
  // Between two samples the distance dips by less than a twentieth of a unit, on the tightest orbit.
  TEST_METHOD(KeepsItsFlightsReachClearOfEveryKeepOut)
  {
    constexpr float STEP = 8.0f;
    float nearest = std::numeric_limits<float>::infinity();
    for (const std::uint32_t seed : SEEDS)
    {
      const auto sector = MakeSector({.seed = seed});
      const NeuronCore::Snapshot snapshot = Describe(*sector);
      const std::vector<Float3> stations = StationCenters(snapshot);
      for (std::size_t flight = 0; flight < sector->FlightCount(); ++flight)
      {
        const std::span<const std::uint32_t> members = sector->FlightMembers(flight);
        const std::uint16_t model = ModelOf(snapshot, members.front());
        const float reach = members.size() > 1 ? std::abs(GameLogic::FormationSlot(0, sector->ClassOf(model)).x) : 0.0f;
        const float keepOut = sector->KeepOutRadius(model) + reach;
        const GameLogic::Route& route = sector->FlightRoute(flight);
        const auto samples = static_cast<std::uint32_t>(route.Length() / STEP);
        for (std::uint32_t sample = 0; sample <= samples; ++sample)
        {
          const float distance = static_cast<float>(sample) * STEP;
          const Float3 point = route.PositionAt(distance);
          for (const Float3& station : stations)
          {
            const float clearance = NeuronCore::Length(point - station) - keepOut;
            nearest = std::min(nearest, clearance);
            Assert::IsTrue(
              clearance >= 0.0f,
              std::format(L"seed {}, flight {}: {} units into a keep-out, {} along", seed, flight, -clearance, distance).c_str());
          }
        }
      }
    }
    Logger::WriteMessage(std::format(L"the nearest a route and its reach come to a keep-out is {} units clear\n", nearest).c_str());
  }

  // The direction is the route's own, away from the joints where it turns: a chord two units long, centered on the point,
  // runs along it to rounding.
  TEST_METHOD(RunsAlongItsDirection)
  {
    constexpr float HALF_CHORD = 1.0f;
    constexpr float STEP = 7.0f;
    const auto sector = MakeSector({});
    for (std::size_t flight = 0; flight < sector->FlightCount(); ++flight)
    {
      const GameLogic::Route& route = sector->FlightRoute(flight);
      const auto samples = static_cast<std::uint32_t>(route.Length() / STEP);
      for (std::uint32_t sample = 0; sample < samples; ++sample)
      {
        const float distance = static_cast<float>(sample) * STEP;
        const bool nearJoint = std::ranges::any_of(route.Pieces(), [&](const GameLogic::RoutePiece& _piece)
                                                   { return std::abs(route.Separation(_piece.distance, distance)) <= 2.0f * HALF_CHORD; });
        if (nearJoint)
        {
          continue;
        }
        const Float3 direction = route.DirectionAt(distance);
        const Float3 chord = NeuronCore::Normalize(route.PositionAt(distance + HALF_CHORD) - route.PositionAt(distance - HALF_CHORD));
        Assert::AreEqual(1.0f, NeuronCore::Length(direction), 1.0e-5f, L"the direction is a unit vector");
        Assert::IsTrue(NeuronCore::Length(chord - direction) <= 1.0e-3f, std::format(L"flight {}, {} along", flight, distance).c_str());
      }
    }
  }

  // A point that follows the route is tracked along it, each step, and never onto the lap before or after it, where the
  // same point of an orbit comes by again.
  TEST_METHOD(TracksAShipAlongItWithoutJumpingALap)
  {
    const GameLogic::Route route = TwoOrbits();
    const float lap = 2.0f * std::numbers::pi_v<float> * 400.0f;
    for (const float distance : {100.0f, 100.0f + lap})
    {
      const float tracked = route.Track(distance, route.PositionAt(distance));
      Assert::IsTrue(std::abs(route.Separation(distance, tracked)) <= 0.05f,
                     std::format(L"{} along is tracked at {}", distance, tracked).c_str());
    }
    constexpr float STEP = 3.0f;
    const auto steps = static_cast<std::uint32_t>(2.5f * route.Length() / STEP);
    float progress = 0.0f;
    for (std::uint32_t step = 1; step <= steps; ++step)
    {
      const float distance = static_cast<float>(step) * STEP;
      progress = route.Track(progress, route.PositionAt(distance));
      Assert::IsTrue(std::abs(route.Separation(distance, progress)) <= 0.05f,
                     std::format(L"{} along is tracked at {}", distance, progress).c_str());
    }
  }

  TEST_METHOD(MeasuresSeparationTheShorterWayRound)
  {
    const GameLogic::Route route = TwoOrbits();
    const float length = route.Length();
    Assert::AreEqual(0.1f * length, route.Separation(0.1f * length, 0.2f * length), 1.0e-3f * length, L"ahead");
    Assert::AreEqual(-0.1f * length, route.Separation(0.2f * length, 0.1f * length), 1.0e-3f * length, L"behind");
    Assert::AreEqual(0.2f * length, route.Separation(0.9f * length, 0.1f * length), 1.0e-3f * length, L"ahead across the end");
    Assert::AreEqual(-0.2f * length, route.Separation(0.1f * length, 0.9f * length), 1.0e-3f * length, L"behind across the end");
    Assert::AreEqual(0.0f, route.Separation(0.3f * length, 1.3f * length), 1.0e-3f * length, L"a whole route apart");
  }
};

} // namespace GameLogicTests
