#include "pch.h"

#include "SnapshotBuffer.h"

#include "Float3.h"
#include "Message.h"
#include "Quaternion.h"
#include "RigidTransform.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronClient::SampledEntity;
using NeuronClient::SnapshotBuffer;
using NeuronClient::WorldSample;
using NeuronCore::Float3;
using NeuronCore::Quaternion;

constexpr std::uint32_t TICK_RATE = 30;
constexpr Quaternion IDENTITY{0.0f, 0.0f, 0.0f, 1.0f};
// What the ships are drawn as, and whose they are (Design/ADR/ADR-029).
constexpr std::uint16_t COMPOSITE = 1;
constexpr std::uint8_t SIDE = 2;

// How far a sampled position may lie from a double-precision reference: rounding of coordinates of a few hundred.
constexpr float POSITION_TOLERANCE = 1.0e-4f;

// A ship on p(t) = start + velocity t + acceleration t² / 2, t in seconds from tick 0: a curve the Hermite interpolation
// reproduces exactly, being a polynomial of degree two, so that anything else it gives is its own error.
struct Course
{
  Float3 start;
  Float3 velocity;
  Float3 acceleration;

  [[nodiscard]] Float3 PositionAt(double _seconds) const noexcept
  {
    const auto axis = [_seconds](float _start, float _velocity, float _acceleration)
    { return static_cast<float>(_start + _velocity * _seconds + 0.5 * _acceleration * _seconds * _seconds); };
    return {axis(start.x, velocity.x, acceleration.x), axis(start.y, velocity.y, acceleration.y),
            axis(start.z, velocity.z, acceleration.z)};
  }

  [[nodiscard]] Float3 VelocityAt(double _seconds) const noexcept
  {
    const auto axis = [_seconds](float _velocity, float _acceleration) { return static_cast<float>(_velocity + _acceleration * _seconds); };
    return {axis(velocity.x, acceleration.x), axis(velocity.y, acceleration.y), axis(velocity.z, acceleration.z)};
  }
};

constexpr Course COURSE{{120.0f, -40.0f, 310.0f}, {55.0f, 4.0f, -21.0f}, {-6.0f, 1.5f, 9.0f}};

[[nodiscard]] double SecondsOf(double _tick) noexcept
{
  return _tick / TICK_RATE;
}

[[nodiscard]] NeuronCore::EntityState ShipAt(std::uint32_t _id, const Course& _course, std::uint64_t _tick)
{
  const double seconds = SecondsOf(static_cast<double>(_tick));
  return {_id, COMPOSITE, SIDE, _course.PositionAt(seconds), IDENTITY, _course.VelocityAt(seconds)};
}

[[nodiscard]] NeuronCore::EntityState StillAt(std::uint32_t _id, Float3 _position, Quaternion _rotation)
{
  return {_id, COMPOSITE, SIDE, _position, _rotation, {0.0f, 0.0f, 0.0f}};
}

// A snapshot of tick _tick whose world has advanced as many ticks, unpaused.
[[nodiscard]] NeuronCore::Snapshot At(std::uint64_t _tick, std::vector<NeuronCore::EntityState> _entities)
{
  return {_tick, _tick, false, std::move(_entities), {}, {}};
}

// When a snapshot of tick _tick arrives if it comes on time, a few milliseconds after its tick.
[[nodiscard]] double OnTime(std::uint64_t _tick) noexcept
{
  return SecondsOf(static_cast<double>(_tick)) + 0.004;
}

[[nodiscard]] const SampledEntity* Find(const WorldSample& _sample, std::uint32_t _id)
{
  for (const SampledEntity& entity : _sample.entities)
  {
    if (entity.id == _id)
    {
      return &entity;
    }
  }
  return nullptr;
}

// The entity _id of _sample, which must hold it; a copy, so that a failed lookup still gives something to compare.
[[nodiscard]] SampledEntity Get(const WorldSample& _sample, std::uint32_t _id)
{
  const SampledEntity* entity = Find(_sample, _id);
  Assert::IsTrue(entity != nullptr, std::format(L"entity {} is in the sample at tick {}", _id, _sample.renderTick).c_str());
  return entity != nullptr ? *entity : SampledEntity{};
}

void AreClose(Float3 _expected, Float3 _actual, float _tolerance, const std::wstring& _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _tolerance, (_what + L", x").c_str());
  Assert::AreEqual(_expected.y, _actual.y, _tolerance, (_what + L", y").c_str());
  Assert::AreEqual(_expected.z, _actual.z, _tolerance, (_what + L", z").c_str());
}

void AreIdentical(Float3 _expected, Float3 _actual, const std::wstring& _what)
{
  Assert::AreEqual(_expected.x, _actual.x, (_what + L", x").c_str());
  Assert::AreEqual(_expected.y, _actual.y, (_what + L", y").c_str());
  Assert::AreEqual(_expected.z, _actual.z, (_what + L", z").c_str());
}

void AreIdentical(Quaternion _expected, Quaternion _actual, const std::wstring& _what)
{
  Assert::AreEqual(_expected.x, _actual.x, (_what + L", x").c_str());
  Assert::AreEqual(_expected.y, _actual.y, (_what + L", y").c_str());
  Assert::AreEqual(_expected.z, _actual.z, (_what + L", z").c_str());
  Assert::AreEqual(_expected.w, _actual.w, (_what + L", w").c_str());
}

// A turn of _degrees about +Y, stored as the protocol stores one, with w >= 0.
[[nodiscard]] Quaternion TurnAboutY(double _degrees)
{
  constexpr double RADIANS_PER_DEGREE = 3.14159265358979323846 / 180.0;
  const double half = 0.5 * _degrees * RADIANS_PER_DEGREE;
  const double sign = std::cos(half) < 0.0 ? -1.0 : 1.0;
  return {0.0f, static_cast<float>(sign * std::sin(half)), 0.0f, static_cast<float>(sign * std::cos(half))};
}

} // namespace

// Design/Archive/SpaceScene.md §6.4 and §15: bracketing, appearing and disappearing, holding past the newest snapshot, and a
// stall shorter than the interpolation delay passing unseen, on the CPU.
TEST_CLASS(SnapshotBufferTests)
{
public:
  TEST_METHOD(InterpolatesAlongTheCurveThroughBothSnapshots)
  {
    SnapshotBuffer buffer(TICK_RATE);
    buffer.Add(At(10, {ShipAt(7, COURSE, 10)}), OnTime(10));
    buffer.Add(At(11, {ShipAt(7, COURSE, 11)}), OnTime(11));
    for (const double tick : {10.0, 10.2, 10.5, 10.75, 11.0})
    {
      const SampledEntity ship = Get(buffer.Sample(tick), 7);
      AreClose(COURSE.PositionAt(SecondsOf(tick)), ship.position, POSITION_TOLERANCE, std::format(L"position at tick {}", tick));
      Assert::IsFalse(ship.detonation.has_value(), L"whole");
      Assert::AreEqual(COMPOSITE, ship.composite, L"drawn as its composite");
      Assert::IsTrue(ship.side == SIDE, L"in its side's colors");
    }
    // At a snapshot's tick, its own position, to the bit.
    AreIdentical(ShipAt(7, COURSE, 10).position, Get(buffer.Sample(10.0), 7).position, L"at tick 10");
    AreIdentical(ShipAt(7, COURSE, 11).position, Get(buffer.Sample(11.0), 7).position, L"at tick 11");
  }

  TEST_METHOD(TurnsAlongTheShorterArc)
  {
    // 170 and -170 degrees about +Y are 20 degrees apart through 180, and 340 through 0.
    SnapshotBuffer buffer(TICK_RATE);
    buffer.Add(At(10, {{3, COMPOSITE, SIDE, {0.0f, 0.0f, 0.0f}, TurnAboutY(170.0), {0.0f, 0.0f, 0.0f}}}), OnTime(10));
    buffer.Add(At(11, {{3, COMPOSITE, SIDE, {0.0f, 0.0f, 0.0f}, TurnAboutY(-170.0), {0.0f, 0.0f, 0.0f}}}), OnTime(11));
    const NeuronCore::Rotation halfway = NeuronCore::RotationOf(Get(buffer.Sample(10.5), 3).rotation);
    // Halfway along the shorter arc is the half turn, which takes +X to -X.
    AreClose({-1.0f, 0.0f, 0.0f}, halfway.axisX, 1.0e-6f, L"+X halfway");
    AreClose({0.0f, 1.0f, 0.0f}, halfway.axisY, 1.0e-6f, L"+Y halfway");
    const NeuronCore::Rotation quarter = NeuronCore::RotationOf(Get(buffer.Sample(10.25), 3).rotation);
    const double degrees = std::atan2(-quarter.axisX.z, quarter.axisX.x) * 180.0 / 3.14159265358979323846;
    Assert::AreEqual(175.0, std::abs(degrees), 0.1, L"a quarter of the way: 175 degrees, nlerp's angle within a tenth");
  }

  TEST_METHOD(HoldsWhatHoldsStillExactly)
  {
    // A station turned a quarter turn about the vertical, at a whole position: it must stay exactly there, and exactly a
    // symmetry of the cube, so that it draws aligned (§7.2).
    const Quaternion quarterTurn = NeuronCore::QuaternionOf({{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    const Float3 position{-1287.0f, 212.0f, 941.0f};
    SnapshotBuffer buffer(TICK_RATE);
    buffer.Add(At(10, {StillAt(1, position, quarterTurn), ShipAt(9, COURSE, 10)}), OnTime(10));
    buffer.Add(At(11, {StillAt(1, position, quarterTurn), ShipAt(9, COURSE, 11)}), OnTime(11));
    for (const double tick : {10.0, 10.37, 10.5, 10.999})
    {
      const SampledEntity station = Get(buffer.Sample(tick), 1);
      AreIdentical(position, station.position, std::format(L"position at tick {}", tick));
      AreIdentical(quarterTurn, station.rotation, std::format(L"rotation at tick {}", tick));
      Assert::IsTrue(NeuronCore::IsCubeSymmetry(NeuronCore::RotationOf(station.rotation)), L"a symmetry of the cube");
    }
  }

  TEST_METHOD(HoldsAPausedWorldExactlyStill)
  {
    // While the world is paused, every snapshot is the last one again with every velocity zero (ADR-015): ships at any
    // rotation hold exactly where they are.
    SnapshotBuffer buffer(TICK_RATE);
    std::vector<NeuronCore::EntityState> ships;
    for (std::uint32_t id = 1; id <= 12; ++id)
    {
      ships.push_back(StillAt(id, COURSE.PositionAt(0.37 * id), TurnAboutY(29.0 * id)));
    }
    buffer.Add({20, 15, true, ships, {}}, OnTime(20));
    buffer.Add({21, 15, true, ships, {}}, OnTime(21));
    const WorldSample sample = buffer.Sample(20.61);
    for (std::size_t i = 0; i < ships.size(); ++i)
    {
      AreIdentical(ships[i].position, sample.entities[i].position, std::format(L"ship {}'s position", ships[i].id));
      AreIdentical(ships[i].rotation, sample.entities[i].rotation, std::format(L"ship {}'s rotation", ships[i].id));
    }
  }

  TEST_METHOD(AppearsAndDisappearsWithTheLaterSnapshot)
  {
    const Float3 somewhere{10.0f, 20.0f, 30.0f};
    SnapshotBuffer buffer(TICK_RATE);
    // Out of the order of their ids, which the buffer restores.
    buffer.Add(At(10, {StillAt(2, somewhere, IDENTITY), StillAt(1, somewhere, IDENTITY)}), OnTime(10));
    buffer.Add(At(11, {ShipAt(3, COURSE, 11), StillAt(2, somewhere, IDENTITY)}), OnTime(11));
    const WorldSample sample = buffer.Sample(10.5);
    Assert::AreEqual(std::size_t{2}, sample.entities.size(), L"entity 1 has gone, and entity 3 has come");
    Assert::AreEqual(2u, sample.entities[0].id);
    Assert::AreEqual(3u, sample.entities[1].id);
    // An entity that has just appeared stands where the later snapshot has it.
    AreIdentical(ShipAt(3, COURSE, 11).position, sample.entities[1].position, L"entity 3");
  }

  TEST_METHOD(HoldsTheNearestSnapshotOutsideThem)
  {
    SnapshotBuffer buffer(TICK_RATE);
    buffer.Add(At(10, {ShipAt(4, COURSE, 10)}), OnTime(10));
    buffer.Add(At(11, {ShipAt(4, COURSE, 11)}), OnTime(11));
    AreIdentical(ShipAt(4, COURSE, 10).position, Get(buffer.Sample(8.5), 4).position, L"before the oldest");
    AreIdentical(ShipAt(4, COURSE, 11).position, Get(buffer.Sample(11.5), 4).position, L"past the newest");
    AreIdentical(ShipAt(4, COURSE, 11).position, Get(buffer.Sample(40.0), 4).position, L"long past the newest");
  }

  TEST_METHOD(PosesDebrisFromItsEventAtTheRenderTime)
  {
    // Detonated at world tick 10, when the server served the command, it froze where it was then; its later snapshots
    // hold it there, still, with the event (ADR-015). Then the world pauses: the clock runs on, the world tick does not.
    const NeuronCore::EntityState before = ShipAt(5, COURSE, 10);
    const NeuronCore::EntityState frozen = StillAt(5, before.position, before.rotation);
    const NeuronCore::DetonationEvent event{5, 0xC0FFEEu, 10, before.velocity};
    SnapshotBuffer buffer(TICK_RATE);
    buffer.Add(At(10, {before}), OnTime(10));
    buffer.Add({11, 11, false, {frozen}, {event}}, OnTime(11));
    buffer.Add({12, 11, true, {frozen}, {event}}, OnTime(12));
    buffer.Add({13, 11, true, {frozen}, {event}}, OnTime(13));

    const SampledEntity flying = Get(buffer.Sample(10.5), 5);
    Assert::IsTrue(flying.detonation.has_value(), L"detonated as of the later snapshot");
    AreIdentical(before.position, flying.position, L"where it froze, not along a curve");
    Assert::AreEqual(0.5f / TICK_RATE, flying.detonation.value_or(NeuronClient::SampledDetonation{}).seconds, 1.0e-6f,
                     L"half a tick since the event");
    Assert::AreEqual(0xC0FFEEu, flying.detonation.value_or(NeuronClient::SampledDetonation{}).event.seed, L"the event's seed");

    for (const double tick : {11.5, 12.0, 12.5, 13.0, 20.0})
    {
      const SampledEntity paused = Get(buffer.Sample(tick), 5);
      Assert::AreEqual(1.0f / TICK_RATE, paused.detonation.value_or(NeuronClient::SampledDetonation{}).seconds, 1.0e-6f,
                       std::format(L"frozen with the world at tick {}", tick).c_str());
    }
    Assert::IsFalse(buffer.Sample(10.5).paused, L"running, as the later snapshot says");
    Assert::IsTrue(buffer.Sample(11.5).paused, L"paused, as the later snapshot says");
  }

  TEST_METHOD(DrawsTheDelayBehindTheFastestArrival)
  {
    // Tick 1 came 2 ms after its time and every later one 10 ms after: the offset is the 2 ms, until tick 1's arrival is
    // more than a second older than the newest.
    SnapshotBuffer buffer(TICK_RATE);
    buffer.Add(At(1, {ShipAt(1, COURSE, 1)}), SecondsOf(1.0) + 0.002);
    for (std::uint64_t tick = 2; tick <= 30; ++tick)
    {
      buffer.Add(At(tick, {ShipAt(1, COURSE, tick)}), SecondsOf(static_cast<double>(tick)) + 0.010);
    }
    const double first = buffer.RenderTick(1.01);
    Assert::AreEqual((1.01 - 0.002) * TICK_RATE - NeuronClient::INTERPOLATION_DELAY_TICKS, first, 1.0e-9, L"behind the fastest arrival");

    // Tick 31 pushes tick 1's arrival out of the last second. The offset grows to 10 ms, and the render tick holds still
    // rather than run backward, until the server's time has caught up.
    buffer.Add(At(31, {ShipAt(1, COURSE, 31)}), SecondsOf(31.0) + 0.010);
    Assert::AreEqual(first, buffer.RenderTick(1.012), L"held");
    Assert::AreEqual((1.03 - 0.010) * TICK_RATE - NeuronClient::INTERPOLATION_DELAY_TICKS, buffer.RenderTick(1.03), 1.0e-9, L"caught up");
  }

  TEST_METHOD(PassesAStallShorterThanTheDelayUnseen)
  {
    // Two clients of one server, drawing at 64 frames a second. The second's snapshots of ticks 20 and 21 are held up and
    // arrive with tick 22's, two ticks late: less than the three the client draws behind, so it draws what the first
    // does, to the bit. A tick rate of 32 keeps every time exact in binary, so that the two offsets are equal exactly.
    constexpr std::uint32_t RATE = 32;
    constexpr std::uint64_t LAST_TICK = 40;
    constexpr double LATENESS_SECONDS = 1.0 / 256.0;
    const auto ship = [](std::uint64_t _tick)
    {
      const double seconds = static_cast<double>(_tick) / RATE;
      return NeuronCore::Snapshot{
        _tick, _tick, false, {{6, COMPOSITE, SIDE, COURSE.PositionAt(seconds), IDENTITY, COURSE.VelocityAt(seconds)}}, {}, {}};
    };
    const auto arrival = [](std::uint64_t _tick, bool _stalled)
    { return static_cast<double>(_stalled && (_tick == 20 || _tick == 21) ? 22 : _tick) / RATE + LATENESS_SECONDS; };
    SnapshotBuffer steady(RATE);
    SnapshotBuffer stalled(RATE);
    std::uint64_t steadyNext = 1;
    std::uint64_t stalledNext = 1;
    bool stallSeen = false;
    for (std::uint32_t frame = 0; frame < 2 * LAST_TICK; ++frame)
    {
      const double now = static_cast<double>(frame) / 64.0 + 1.0 / 16.0;
      for (; steadyNext <= LAST_TICK && arrival(steadyNext, false) <= now; ++steadyNext)
      {
        steady.Add(ship(steadyNext), arrival(steadyNext, false));
      }
      for (; stalledNext <= LAST_TICK && arrival(stalledNext, true) <= now; ++stalledNext)
      {
        stalled.Add(ship(stalledNext), arrival(stalledNext, true));
      }
      stallSeen = stallSeen || stalled.Newest().tick < steady.Newest().tick;
      const double steadyTick = steady.RenderTick(now);
      const double stalledTick = stalled.RenderTick(now);
      Assert::AreEqual(steadyTick, stalledTick, std::format(L"the render tick of frame {}", frame).c_str());
      AreIdentical(Get(steady.Sample(steadyTick), 6).position, Get(stalled.Sample(stalledTick), 6).position,
                   std::format(L"the ship in frame {}", frame));
    }
    Assert::IsTrue(stallSeen, L"the second client's snapshots were held up");
  }

  TEST_METHOD(KeepsItsSnapshotsInOrderAndForASecond)
  {
    SnapshotBuffer buffer(TICK_RATE);
    // Out of order, and one tick twice: the first of the two is kept.
    buffer.Add(At(12, {ShipAt(2, COURSE, 12)}), OnTime(12));
    buffer.Add(At(11, {ShipAt(2, COURSE, 11)}), OnTime(12));
    buffer.Add(At(11, {StillAt(2, {0.0f, 0.0f, 0.0f}, IDENTITY)}), OnTime(12));
    AreIdentical(ShipAt(2, COURSE, 11).position, Get(buffer.Sample(11.0), 2).position, L"the first tick 11 kept");
    AreClose(COURSE.PositionAt(SecondsOf(11.5)), Get(buffer.Sample(11.5), 2).position, POSITION_TOLERANCE, L"between 11 and 12");

    // A second of snapshots at 30 a second: what came before is gone, and the oldest left is held before it.
    for (std::uint64_t tick = 13; tick <= 100; ++tick)
    {
      buffer.Add(At(tick, {ShipAt(2, COURSE, tick)}), OnTime(tick));
    }
    Assert::AreEqual(std::uint64_t{100}, buffer.Newest().tick);
    AreIdentical(ShipAt(2, COURSE, 70).position, Get(buffer.Sample(50.0), 2).position, L"the oldest kept, a second before the newest");
  }
};

} // namespace NeuronClientTests
