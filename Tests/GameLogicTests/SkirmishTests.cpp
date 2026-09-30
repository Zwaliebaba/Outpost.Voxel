#include "pch.h"

#include "Skirmish.h"
#include "TestSupport.h"

#include "Catalogue.h"
#include "Design.h"
#include "Profile.h"
#include "SkirmishLayout.h"
#include "WelcomeNames.h"

#include "CommandLog.h"
#include "ServerHost.h"

#include "LoopbackTransport.h"
#include "Message.h"
#include "Quaternion.h"
#include "RigidTransform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{
namespace
{

using Bytes = std::vector<std::uint8_t>;
using NeuronCore::Float3;

// The seeds the symmetry is held to (Design/MvpPlan.md §5, phase 2).
constexpr std::uint32_t SYMMETRY_SEEDS = 20;

// What the skirmish's welcome names (Design/ADR/ADR-030): the catalogue's five designs, then the three asteroids; and two
// sides. It holds each side's core and four ships, and 44 asteroids.
constexpr std::size_t DESIGN_COMPOSITES = 5;
constexpr std::size_t COMPOSITES = DESIGN_COMPOSITES + 3;
constexpr std::size_t UNITS = 10;
constexpr std::size_t ASTEROIDS = 44;

[[nodiscard]] std::wstring Widen(std::string_view _text)
{
  return {_text.begin(), _text.end()};
}

[[nodiscard]] std::vector<std::uint32_t> IdsOf(const NeuronCore::Snapshot& _snapshot)
{
  std::vector<std::uint32_t> ids;
  ids.reserve(_snapshot.entities.size());
  for (const NeuronCore::EntityState& entity : _snapshot.entities)
  {
    ids.push_back(entity.id);
  }
  return ids;
}

[[nodiscard]] std::vector<std::uint32_t> DetonatedIdsOf(const NeuronCore::Snapshot& _snapshot)
{
  std::vector<std::uint32_t> ids;
  ids.reserve(_snapshot.detonations.size());
  for (const NeuronCore::DetonationEvent& detonation : _snapshot.detonations)
  {
    ids.push_back(detonation.entity);
  }
  return ids;
}

[[nodiscard]] bool SameRotation(const NeuronCore::Rotation& _a, const NeuronCore::Rotation& _b) noexcept
{
  const auto same = [](Float3 _u, Float3 _v) { return _u.x == _v.x && _u.y == _v.y && _u.z == _v.z; };
  return same(_a.axisX, _b.axisX) && same(_a.axisY, _b.axisY) && same(_a.axisZ, _b.axisZ);
}

// The client's end of a loopback to a skirmish's host: every message it has received, decoded against the skirmish's
// composites and sides, with the bytes each came in.
struct Client
{
  std::unique_ptr<NeuronCore::Transport> transport;

  void Send(const NeuronCore::Message& _message) const
  {
    Assert::IsTrue(transport->Send(NeuronCore::EncodeMessage(_message)), L"the client sends");
  }

  [[nodiscard]] std::vector<NeuronCore::Message> ReceiveAll(std::vector<Bytes>& _bytes) const
  {
    std::vector<NeuronCore::Message> messages;
    while (std::optional<Bytes> bytes = transport->Receive())
    {
      const auto message = NeuronCore::DecodeMessage(*bytes, {COMPOSITES, GameCore::SIDE_COUNT});
      Assert::IsTrue(message.has_value(), L"the server's message decodes");
      messages.push_back(message.value_or(NeuronCore::Message{}));
      _bytes.push_back(std::move(*bytes));
    }
    return messages;
  }
};

// A client of _host that plays side _side, or observes, and has said Hello.
[[nodiscard]] Client Join(NeuronServer::ServerHost& _host, std::uint8_t _side)
{
  NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
  _host.AddSession(std::move(pair.server), _side);
  Client client{std::move(pair.client)};
  client.Send(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION});
  return client;
}

// The sessions a run of the skirmish serves, in the order they join: side 1's, side 2's and an observer's.
constexpr std::array<std::uint8_t, 3> SESSION_SIDES{1, 2, NeuronCore::OBSERVER_SIDE};
constexpr std::size_t OBSERVER_SESSION = 2;

// A command a session sends before the host's step at a tick: the host applies it at that tick, and the snapshot of the
// next tick is the first to show it.
struct Scripted
{
  std::uint64_t tick;
  std::size_t session;
  NeuronCore::CommandKind kind;
  std::uint32_t entity;
};

// The entities the script names, by id: each side's core and ships, in the layout's order, then the asteroids, of which
// the first stands in side 1's first near field.
constexpr std::uint32_t SIDE_1_CORE = 1;
constexpr std::uint32_t SIDE_2_CORE = 6;
constexpr std::uint32_t NEAR_ASTEROID = UNITS + 1;

// Side 1 loses its core and then its four ships, so that it sees only its own, then has its core back; side 2's
// detonation of side 1's core is refused; the observer detonates side 2's core, which side 2 restores; and side 1
// detonates an asteroid of its near field while it has no sensor, and sees the debris once its core is back.
constexpr std::uint64_t SCRIPT_TICKS = 12;
constexpr std::array<Scripted, 11> SCRIPT{{{2, 0, NeuronCore::CommandKind::Detonate, SIDE_1_CORE},
                                           {3, 1, NeuronCore::CommandKind::Detonate, SIDE_1_CORE},
                                           {4, 0, NeuronCore::CommandKind::Detonate, 2},
                                           {4, 0, NeuronCore::CommandKind::Detonate, 3},
                                           {4, 0, NeuronCore::CommandKind::Detonate, 4},
                                           {4, 0, NeuronCore::CommandKind::Detonate, 5},
                                           {5, 2, NeuronCore::CommandKind::Detonate, SIDE_2_CORE},
                                           {6, 0, NeuronCore::CommandKind::Detonate, NEAR_ASTEROID},
                                           {7, 0, NeuronCore::CommandKind::Restore, SIDE_1_CORE},
                                           {9, 1, NeuronCore::CommandKind::Restore, SIDE_2_CORE},
                                           {10, 0, NeuronCore::CommandKind::Restore, 2}}};
constexpr std::size_t SCRIPT_REFUSED = 1;

// What each session of a run received, by session: its welcome, then a snapshot a tick; and what the host counted.
struct Run
{
  std::vector<std::vector<Bytes>> bytes;
  std::vector<std::vector<NeuronCore::Message>> messages;
  std::uint64_t refused;
  std::size_t sessions; // open at the end
};

// _ticks of the skirmish of _seed, with a session of each of SESSION_SIDES and _script sent at its ticks, logged to _log
// when there is one.
[[nodiscard]] Run RunSkirmish(std::uint32_t _seed, std::span<const Scripted> _script, std::uint64_t _ticks, std::ostream* _log = nullptr)
{
  const auto skirmish = MakeSkirmish(_seed);
  NeuronServer::ServerHost host(*skirmish);
  std::vector<Client> clients;
  clients.reserve(SESSION_SIDES.size());
  for (const std::uint8_t side : SESSION_SIDES)
  {
    clients.push_back(Join(host, side));
  }
  if (_log != nullptr)
  {
    host.Log(*_log, GameLogic::EncodeSkirmishParameters({.seed = _seed}));
  }
  for (std::uint64_t tick = 0; tick < _ticks; ++tick)
  {
    for (const Scripted& scripted : _script)
    {
      if (scripted.tick == tick)
      {
        clients[scripted.session].Send(NeuronCore::Command{scripted.kind, scripted.entity});
      }
    }
    host.Step();
  }
  Run run{std::vector<std::vector<Bytes>>(clients.size()), std::vector<std::vector<NeuronCore::Message>>(clients.size()),
          host.RefusedCommands(), host.SessionCount()};
  for (std::size_t session = 0; session < clients.size(); ++session)
  {
    run.messages[session] = clients[session].ReceiveAll(run.bytes[session]);
  }
  return run;
}

[[nodiscard]] std::vector<NeuronCore::Snapshot> SnapshotsOf(const std::vector<NeuronCore::Message>& _messages)
{
  std::vector<NeuronCore::Snapshot> snapshots;
  for (const NeuronCore::Message& message : _messages)
  {
    if (const auto* snapshot = std::get_if<NeuronCore::Snapshot>(&message))
    {
      snapshots.push_back(*snapshot);
    }
  }
  return snapshots;
}

// Each composite's sensor range, from the welcome's names and GameCore's designs: its design's profile's, and none for an
// asteroid.
[[nodiscard]] std::vector<float> SensorRanges(const NeuronCore::Welcome& _welcome)
{
  const GameCore::WelcomeNames names = GameCore::DecodeWelcomeNames(_welcome.payload).value_or(GameCore::WelcomeNames{});
  std::vector<float> ranges;
  ranges.reserve(names.composites.size());
  for (const std::string& name : names.composites)
  {
    const GameCore::DesignSpec* spec = GameCore::FindDesign(name);
    const auto design = spec != nullptr ? std::optional(GameCore::LoadDesign(*spec, GameDataDirectory())) : std::nullopt;
    ranges.push_back(design && design->has_value() ? GameCore::ComputeProfile(design->value()).sensorRangeUnits : 0.0f);
  }
  return ranges;
}

// What side _side should see of _whole, the observer's snapshot of the same tick, by the rule of ADR-032 worked out here:
// its own entities, and every entity whose middle lies within the sensor range of one of its intact entities.
[[nodiscard]] std::vector<std::uint32_t> InSight(const NeuronCore::Snapshot& _whole, std::uint8_t _side, const std::vector<float>& _ranges)
{
  const auto intact = [&_whole](std::uint32_t _id)
  { return std::ranges::none_of(_whole.detonations, [_id](const NeuronCore::DetonationEvent& _event) { return _event.entity == _id; }); };
  std::vector<std::uint32_t> ids;
  for (const NeuronCore::EntityState& entity : _whole.entities)
  {
    bool seen = entity.side == _side;
    for (const NeuronCore::EntityState& sensor : _whole.entities)
    {
      if (seen || sensor.side != _side || !intact(sensor.id))
      {
        continue;
      }
      const double dx = static_cast<double>(entity.position.x) - static_cast<double>(sensor.position.x);
      const double dy = static_cast<double>(entity.position.y) - static_cast<double>(sensor.position.y);
      const double dz = static_cast<double>(entity.position.z) - static_cast<double>(sensor.position.z);
      const double range = _ranges[sensor.composite];
      seen = range > 0.0 && dx * dx + dy * dy + dz * dz <= range * range;
    }
    if (seen)
    {
      ids.push_back(entity.id);
    }
  }
  return ids;
}

// Side _side's snapshot _seen against the observer's _whole of the same tick: the entities in its sight, in order, each
// as the observer receives it, and the detonations of those entities alone.
void CheckSight(const NeuronCore::Snapshot& _whole, const NeuronCore::Snapshot& _seen, std::uint8_t _side,
                const std::vector<float>& _ranges, const std::wstring& _what)
{
  Assert::AreEqual(_whole.tick, _seen.tick, _what.c_str());
  const std::vector<std::uint32_t> expected = InSight(_whole, _side, _ranges);
  Assert::IsTrue(IdsOf(_seen) == expected, (_what + L": the entities in its sight").c_str());
  for (const NeuronCore::EntityState& entity : _seen.entities)
  {
    const auto whole = std::ranges::find(_whole.entities, entity.id, &NeuronCore::EntityState::id);
    Assert::IsTrue(whole != _whole.entities.end() && whole->composite == entity.composite && whole->side == entity.side &&
                     whole->position.x == entity.position.x && whole->position.y == entity.position.y &&
                     whole->position.z == entity.position.z,
                   (_what + std::format(L": entity {} as the observer receives it", entity.id)).c_str());
  }
  std::vector<std::uint32_t> detonated;
  for (const NeuronCore::DetonationEvent& detonation : _whole.detonations)
  {
    if (std::ranges::find(expected, detonation.entity) != expected.end())
    {
      detonated.push_back(detonation.entity);
    }
  }
  Assert::IsTrue(DetonatedIdsOf(_seen) == detonated, (_what + L": the detonations of those entities").c_str());
}

[[nodiscard]] const NeuronCore::Welcome& WelcomeOf(const Run& _run, std::size_t _session)
{
  const auto* welcome = std::get_if<NeuronCore::Welcome>(_run.messages[_session].data());
  Assert::IsTrue(welcome != nullptr, L"the session was welcomed");
  return *welcome;
}

} // namespace

// Design/MvpPlan.md §5, phase 2, and Design/ADR/ADR-030: the skirmish as its world serves it.
TEST_CLASS(SkirmishTests)
{
public:
  // Every entity has its image under the half turn: of the same composite, of the other side or of none, standing
  // exactly where the half turn puts it and turned by the half turn after its own turn.
  TEST_METHOD(IsItsOwnHalfTurn)
  {
    for (std::uint32_t seed = 1; seed <= SYMMETRY_SEEDS; ++seed)
    {
      const NeuronCore::Snapshot snapshot = DescribeSkirmish(*MakeSkirmish(seed));
      Assert::AreEqual(UNITS + ASTEROIDS, snapshot.entities.size(), std::format(L"seed {}: every entity", seed).c_str());
      for (const NeuronCore::EntityState& entity : snapshot.entities)
      {
        const NeuronCore::Rotation turned = NeuronCore::ComposeRotations(GameCore::HALF_TURN, NeuronCore::RotationOf(entity.rotation));
        const std::uint8_t side = entity.side == 0 ? std::uint8_t{0} : static_cast<std::uint8_t>(3 - entity.side);
        const auto images = std::ranges::count_if(snapshot.entities,
                                                  [&](const NeuronCore::EntityState& _other)
                                                  {
                                                    return _other.composite == entity.composite && _other.side == side &&
                                                           _other.position.x == -entity.position.x &&
                                                           _other.position.y == entity.position.y &&
                                                           _other.position.z == -entity.position.z &&
                                                           SameRotation(NeuronCore::RotationOf(_other.rotation), turned);
                                                  });
        Assert::AreEqual(1, static_cast<int>(images), std::format(L"seed {}: entity {}'s image", seed, entity.id).c_str());
      }
    }
  }

  // The cores where §8.4 puts them and each side's ships, the catalogue's designs and the asteroids as composites, the
  // sides' colors, and the names the client shows for each.
  TEST_METHOD(ServesTheLayoutAndItsNames)
  {
    const auto skirmish = MakeSkirmish(7);
    Assert::AreEqual(COMPOSITES, skirmish->Composites().size(), L"five designs and three asteroids");
    Assert::AreEqual(GameCore::SIDE_COUNT, skirmish->Sides().size(), L"two sides");
    for (std::size_t side = 0; side < GameCore::SIDE_COUNT; ++side)
    {
      const NeuronCore::SideColor color = GameCore::SKIRMISH_SIDES[side].color;
      Assert::IsTrue(skirmish->Sides()[side].red == color.red && skirmish->Sides()[side].green == color.green &&
                       skirmish->Sides()[side].blue == color.blue,
                     L"its color");
    }
    const auto names = GameCore::DecodeWelcomeNames(skirmish->WelcomePayload());
    Assert::IsTrue(names.has_value(), L"the payload decodes");
    const GameCore::WelcomeNames welcome = names.value_or(GameCore::WelcomeNames{});
    Assert::AreEqual(COMPOSITES, welcome.composites.size(), L"a name for each composite");
    Assert::AreEqual(std::string("Blue"), welcome.sides.front());
    Assert::AreEqual(std::string("AsteroidC"), welcome.composites.back());

    const NeuronCore::Snapshot snapshot = DescribeSkirmish(*skirmish);
    for (std::uint8_t side = 1; side <= 2; ++side)
    {
      std::vector<std::string> designs;
      for (const NeuronCore::EntityState& entity : snapshot.entities)
      {
        if (entity.side == side)
        {
          designs.push_back(welcome.composites[entity.composite]);
          if (welcome.composites[entity.composite] == "StationCore")
          {
            const float coreX = side == 1 ? -1800.0f : 1800.0f;
            Assert::IsTrue(std::abs(entity.position.x - coreX) <= 1.0f && std::abs(entity.position.y) <= 1.0f &&
                             std::abs(entity.position.z) <= 1.0f,
                           L"the core where §8.4 puts it, within a voxel");
          }
        }
      }
      std::ranges::sort(designs);
      Assert::IsTrue(designs == std::vector<std::string>{"Gunship", "Gunship", "Miner", "Miner", "StationCore"},
                     std::format(L"side {}: its core, two miners and two gunships", side).c_str());
    }
    const auto asteroids = std::ranges::count_if(snapshot.entities, [](const NeuronCore::EntityState& _entity)
                                                 { return _entity.side == 0 && _entity.composite >= DESIGN_COMPOSITES; });
    Assert::AreEqual(static_cast<int>(ASTEROIDS), static_cast<int>(asteroids), L"the asteroids, of no side");
  }

  // Everything stands aligned: turned by one of the cube's rotations, at whole or half voxels, so a core draws on the
  // aligned splat with the modules it carries.
  TEST_METHOD(StandsAligned)
  {
    for (const NeuronCore::EntityState& entity : DescribeSkirmish(*MakeSkirmish(3)).entities)
    {
      Assert::IsTrue(NeuronCore::IsCubeSymmetry(NeuronCore::RotationOf(entity.rotation)), L"turned by quarter turns");
      for (const float coordinate : {entity.position.x, entity.position.y, entity.position.z})
      {
        Assert::IsTrue(2.0f * coordinate == std::floor(2.0f * coordinate), L"at a whole or a half voxel");
      }
    }
  }

  // Phase 3's test (Design/MvpPlan.md §5): two hosts over two skirmishes of one seed, each with a session of each side and
  // an observer, over the script: every session receives the same bytes from both. Another seed differs from its welcome
  // on, and the two sides see apart.
  TEST_METHOD(RepeatsItselfFromItsSeed)
  {
    const Run first = RunSkirmish(11, SCRIPT, SCRIPT_TICKS);
    const Run second = RunSkirmish(11, SCRIPT, SCRIPT_TICKS);
    const Run other = RunSkirmish(12, SCRIPT, SCRIPT_TICKS);
    for (std::size_t session = 0; session < SESSION_SIDES.size(); ++session)
    {
      const std::wstring what = std::format(L"session {}", session);
      Assert::AreEqual(std::size_t{SCRIPT_TICKS + 1}, first.bytes[session].size(), (what + L": a welcome and a snapshot a tick").c_str());
      Assert::IsTrue(first.bytes[session] == second.bytes[session], (what + L": the same bytes").c_str());
      Assert::IsFalse(first.bytes[session].front() == other.bytes[session].front(), (what + L": another seed, another welcome").c_str());
      Assert::IsFalse(first.bytes[session][1] == other.bytes[session][1], (what + L": and other snapshots").c_str());
    }
    Assert::IsFalse(first.bytes[0][1] == first.bytes[1][1], L"the sides see apart");
  }

  // Phase 3's checkpoint (Design/MvpPlan.md §5): at the start each side sees its own core and ships and its two near
  // fields, and neither the other side's nor the middle fields, whatever the seed; the observer sees everything. The
  // asteroids stand in the layout's order: side 1's two near fields and the first middle field, then the half turn of
  // each.
  TEST_METHOD(HidesTheEnemyAndTheMiddleAtTheStart)
  {
    constexpr std::size_t NEAR_ASTEROIDS = GameCore::HALF_FIELDS[0].asteroids + GameCore::HALF_FIELDS[1].asteroids;
    constexpr std::size_t HALF_ASTEROIDS = ASTEROIDS / 2;
    for (std::uint32_t seed = 1; seed <= SYMMETRY_SEEDS; ++seed)
    {
      const auto skirmish = MakeSkirmish(seed);
      Assert::AreEqual(UNITS + ASTEROIDS, DescribeSkirmish(*skirmish).entities.size(), L"the observer sees everything");
      for (std::uint8_t side = 1; side <= 2; ++side)
      {
        std::vector<std::uint32_t> expected;
        const std::size_t firstUnit = side == 1 ? 1 : 1 + UNITS / 2;
        const std::size_t firstNear = 1 + UNITS + (side == 1 ? 0 : HALF_ASTEROIDS);
        for (std::size_t id = firstUnit; id < firstUnit + UNITS / 2; ++id)
        {
          expected.push_back(static_cast<std::uint32_t>(id));
        }
        for (std::size_t id = firstNear; id < firstNear + NEAR_ASTEROIDS; ++id)
        {
          expected.push_back(static_cast<std::uint32_t>(id));
        }
        Assert::IsTrue(IdsOf(DescribeSkirmish(*skirmish, side)) == expected,
                       std::format(L"seed {}, side {}: its own and its near fields", seed, side).c_str());
      }
    }
  }

  // Phase 3's test (Design/MvpPlan.md §5): on 20 seeds and every tick of the script, each side's snapshot holds what the
  // rule, worked out here from the observer's snapshot, puts in its sight, and nothing else. The ranges are the profiles':
  // 1,200 units for the core and 700 for a ship.
  TEST_METHOD(SendsEachSideOnlyWhatItsSensorsReach)
  {
    for (std::uint32_t seed = 1; seed <= SYMMETRY_SEEDS; ++seed)
    {
      const Run run = RunSkirmish(seed, SCRIPT, SCRIPT_TICKS);
      const std::vector<float> ranges = SensorRanges(WelcomeOf(run, OBSERVER_SESSION));
      const GameCore::WelcomeNames names =
        GameCore::DecodeWelcomeNames(WelcomeOf(run, OBSERVER_SESSION).payload).value_or(GameCore::WelcomeNames{});
      for (std::size_t composite = 0; composite < names.composites.size(); ++composite)
      {
        const std::string& name = names.composites[composite];
        const float expected = name == "StationCore" ? 1200.0f : (GameCore::FindDesign(name) != nullptr ? 700.0f : 0.0f);
        Assert::AreEqual(expected, ranges[composite], Widen(name + "'s sensor range").c_str());
      }

      const std::vector<NeuronCore::Snapshot> whole = SnapshotsOf(run.messages[OBSERVER_SESSION]);
      Assert::AreEqual(std::size_t{SCRIPT_TICKS}, whole.size(), L"a snapshot a tick");
      for (std::size_t session = 0; session < OBSERVER_SESSION; ++session)
      {
        const std::uint8_t side = SESSION_SIDES[session];
        Assert::IsTrue(WelcomeOf(run, session).sessionSide == side, L"the session is told its side");
        const std::vector<NeuronCore::Snapshot> seen = SnapshotsOf(run.messages[session]);
        Assert::AreEqual(whole.size(), seen.size(), L"a snapshot a tick");
        for (std::size_t tick = 0; tick < seen.size(); ++tick)
        {
          CheckSight(whole[tick], seen[tick], side, ranges, std::format(L"seed {}, side {}, tick {}", seed, side, tick + 1));
        }
      }

      // The script's turns: with neither core nor ships intact, side 1 sees only its own, the asteroid it detonates
      // meanwhile included out of sight; with its core back, it sees the asteroid's debris.
      const std::vector<NeuronCore::Snapshot> side1 = SnapshotsOf(run.messages[0]);
      for (std::size_t tick = 5; tick <= 7; ++tick)
      {
        Assert::IsTrue(IdsOf(side1[tick - 1]) == std::vector<std::uint32_t>{1, 2, 3, 4, 5},
                       std::format(L"seed {}, tick {}: side 1 sees only its own", seed, tick).c_str());
      }
      const std::vector<std::uint32_t> detonatedAtTick7 = DetonatedIdsOf(whole[6]);
      const std::vector<std::uint32_t> seenAtTick8 = DetonatedIdsOf(side1[7]);
      Assert::IsTrue(std::ranges::find(detonatedAtTick7, NEAR_ASTEROID) != detonatedAtTick7.end(), L"the asteroid detonated at tick 7");
      Assert::IsTrue(std::ranges::find(seenAtTick8, NEAR_ASTEROID) != seenAtTick8.end(),
                     L"and side 1 sees its debris once its core is back");
    }
  }

  // Phase 3's test (Design/MvpPlan.md §5): side 2's detonation of side 1's core is refused and counted, its session stays
  // open, and every session receives the bytes it would have had the command never been sent. A side may command its own
  // and an asteroid, which is no side's, and the observer anything.
  TEST_METHOD(RefusesACommandOnTheOtherSidesEntity)
  {
    const auto skirmish = MakeSkirmish(4);
    const auto refusal = [&skirmish](NeuronCore::CommandKind _kind, std::uint32_t _entity, std::uint8_t _side)
    { return skirmish->Refuses(NeuronCore::Command{_kind, _entity}, _side); };
    using enum NeuronCore::CommandKind;
    Assert::IsTrue(refusal(Detonate, SIDE_1_CORE, 2) == NeuronServer::CommandRefusal::OtherSidesEntity,
                   L"side 2 may not detonate side 1's core");
    Assert::IsTrue(refusal(Restore, SIDE_1_CORE, 2) == NeuronServer::CommandRefusal::OtherSidesEntity, L"nor restore it");
    Assert::IsTrue(refusal(Detonate, SIDE_2_CORE, 1) == NeuronServer::CommandRefusal::OtherSidesEntity, L"nor side 1 side 2's");
    Assert::IsFalse(refusal(Detonate, SIDE_1_CORE, 1).has_value(), L"side 1 commands its own");
    Assert::IsFalse(refusal(Detonate, SIDE_1_CORE, NeuronCore::OBSERVER_SIDE).has_value(), L"and the observer anything");
    Assert::IsFalse(refusal(Detonate, NEAR_ASTEROID, 2).has_value(), L"an asteroid is no side's");
    Assert::IsFalse(refusal(Pause, 0, 2).has_value(), L"and a pause is anyone's while the MVP has one player");

    std::vector<Scripted> without(SCRIPT.begin(), SCRIPT.end());
    std::erase_if(without, [](const Scripted& _scripted) { return _scripted.session == 1 && _scripted.entity == SIDE_1_CORE; });
    const Run refused = RunSkirmish(4, SCRIPT, SCRIPT_TICKS);
    const Run never = RunSkirmish(4, without, SCRIPT_TICKS);
    Assert::AreEqual(std::uint64_t{SCRIPT_REFUSED}, refused.refused, L"counted");
    Assert::AreEqual(std::uint64_t{0}, never.refused);
    Assert::AreEqual(SESSION_SIDES.size(), refused.sessions, L"and the session stays open");
    for (std::size_t session = 0; session < SESSION_SIDES.size(); ++session)
    {
      Assert::IsTrue(refused.bytes[session] == never.bytes[session],
                     std::format(L"session {}: the command changed nothing", session).c_str());
    }
  }

  // Phase 3's test (Design/MvpPlan.md §5): a logged run of the script replays to the same bytes from its log alone, the
  // skirmish made again from the parameters the log carries. On another seed it parts at the first tick.
  TEST_METHOD(ReplaysALoggedRun)
  {
    std::stringstream log(std::ios::in | std::ios::out | std::ios::binary);
    const Run run = RunSkirmish(9, SCRIPT, SCRIPT_TICKS, &log);
    const std::string text = log.str();
    const auto decoded = NeuronServer::DecodeCommandLog(Bytes(text.begin(), text.end()));
    Assert::IsTrue(decoded.has_value(), L"the log decodes");
    const NeuronServer::CommandLog commandLog = decoded.value_or(NeuronServer::CommandLog{});
    Assert::AreEqual(SCRIPT.size() - run.refused, commandLog.commands.size(), L"every applied command");
    Assert::IsTrue(commandLog.sessionSides == Bytes(SESSION_SIDES.begin(), SESSION_SIDES.end()), L"and every session's side");

    const auto parameters = GameLogic::DecodeSkirmishParameters(commandLog.world);
    Assert::IsTrue(parameters.has_value() && parameters->seed == 9 && parameters->tickRate == 30, L"the skirmish's parameters");
    auto again = GameLogic::Skirmish::Create(parameters.value_or(GameLogic::SkirmishParameters{}), GameDataDirectory());
    Assert::IsTrue(again.has_value(), L"made again");
    const NeuronServer::ReplayOutcome outcome = NeuronServer::Replay(**again, commandLog);
    Assert::AreEqual(SCRIPT_TICKS, outcome.ticks, L"every tick replayed");
    Assert::AreEqual(commandLog.commands.size(), outcome.commands, L"every command sent again");
    Assert::IsFalse(outcome.firstDifference.has_value(), L"and every tick matched");

    const auto otherSeed = MakeSkirmish(10);
    Assert::IsTrue(NeuronServer::Replay(*otherSeed, commandLog).firstDifference == 1u, L"another seed parts at the first tick");
  }

  TEST_METHOD(SpellsItsParametersForItsLog)
  {
    const Bytes bytes = GameLogic::EncodeSkirmishParameters({.seed = 0x01020304u, .tickRate = 30});
    Assert::IsTrue(bytes == Bytes{2, 4, 3, 2, 1, 30, 0, 0, 0, 0}, L"a version, then the seed, the tick rate and the flags");
    const auto parameters = GameLogic::DecodeSkirmishParameters(bytes);
    Assert::IsTrue(parameters.has_value() && parameters->seed == 0x01020304u && parameters->tickRate == 30 && !parameters->battle &&
                     parameters->jinking,
                   L"and back");
    const Bytes flagged = GameLogic::EncodeSkirmishParameters({.seed = 1, .tickRate = 30, .battle = true, .jinking = false});
    Assert::IsTrue(flagged.back() == 3, L"a battle, without jinking");
    const auto battle = GameLogic::DecodeSkirmishParameters(flagged);
    Assert::IsTrue(battle.has_value() && battle->battle && !battle->jinking, L"and back");
    Bytes otherVersion = bytes;
    otherVersion[0] = 1;
    Bytes unknownFlag = bytes;
    unknownFlag.back() = 4;
    Bytes longer = bytes;
    longer.push_back(0);
    Assert::IsFalse(GameLogic::DecodeSkirmishParameters(otherVersion).has_value(), L"another version");
    Assert::IsFalse(GameLogic::DecodeSkirmishParameters(unknownFlag).has_value(), L"a flag the skirmish does not have");
    Assert::IsFalse(GameLogic::DecodeSkirmishParameters(std::span(bytes).first(9)).has_value(), L"bytes cut short");
    Assert::IsFalse(GameLogic::DecodeSkirmishParameters(longer).has_value(), L"and too many");
  }

  // An entity detonates on command, once, with a seed of its own, at the world tick it was given, and is restored whole.
  TEST_METHOD(DetonatesAndRestoresOnCommand)
  {
    const auto skirmish = MakeSkirmish(5);
    skirmish->Detonate(1, 40);
    skirmish->Detonate(1, 41);
    skirmish->Detonate(6, 42);
    skirmish->Detonate(0, 43);
    skirmish->Detonate(static_cast<std::uint32_t>(UNITS + ASTEROIDS + 1), 44);
    NeuronCore::Snapshot snapshot = DescribeSkirmish(*skirmish);
    Assert::AreEqual(std::size_t{2}, snapshot.detonations.size(), L"two entities detonated, once each");
    Assert::AreEqual(1u, snapshot.detonations[0].entity);
    Assert::AreEqual(std::uint64_t{40}, snapshot.detonations[0].worldTick, L"at the tick it was given");
    Assert::AreEqual(6u, snapshot.detonations[1].entity);
    Assert::AreNotEqual(snapshot.detonations[0].seed, snapshot.detonations[1].seed, L"each with its own seed");
    skirmish->Restore(1);
    snapshot = DescribeSkirmish(*skirmish);
    Assert::AreEqual(std::size_t{1}, snapshot.detonations.size(), L"restored whole");
    Assert::AreEqual(6u, snapshot.detonations[0].entity);
  }

  TEST_METHOD(RefusesByName)
  {
    const auto noTicks = GameLogic::Skirmish::Create({.seed = 1, .tickRate = 0}, GameDataDirectory());
    Assert::AreEqual(std::string("BadParameter"), std::string(GameLogic::SkirmishRefusalName(
                                                    noTicks ? GameLogic::SkirmishRefusal::DesignRefused : noTicks.error().refusal)));
    const std::filesystem::path empty = std::filesystem::temp_directory_path() / "OutpostSkirmishTests";
    std::filesystem::create_directories(empty);
    const auto missing = GameLogic::Skirmish::Create({}, empty);
    std::filesystem::remove_all(empty);
    Assert::AreEqual(std::string("ModelNotLoaded"), std::string(GameLogic::SkirmishRefusalName(
                                                      missing ? GameLogic::SkirmishRefusal::BadParameter : missing.error().refusal)));
    Assert::AreEqual(std::string("Miner.nvf: FileNotFound"), missing ? std::string() : missing.error().detail, L"names the model and why");
  }
};

} // namespace GameLogicTests
