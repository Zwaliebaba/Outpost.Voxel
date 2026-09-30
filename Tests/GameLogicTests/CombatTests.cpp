#include "pch.h"

#include "Combat.h"
#include "Skirmish.h"
#include "TestSupport.h"

#include "Orders.h"
#include "SkirmishLayout.h"

#include "ServerHost.h"

#include "LoopbackTransport.h"
#include "Message.h"
#include "RigidTransform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
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
using NeuronCore::Int3;

constexpr std::uint32_t TICK_RATE = 30;

// _seconds of the world's time, in its ticks.
[[nodiscard]] constexpr std::uint64_t Ticks(std::uint64_t _seconds) noexcept
{
  return _seconds * TICK_RATE;
}

// What the skirmish's welcome names: the catalogue's five designs and the three asteroids; and two sides.
constexpr NeuronCore::WelcomeCounts COUNTS{8, GameCore::SIDE_COUNT};

// The seeds the plan's jinking test runs over (Design/MvpPlan.md, phase 5), and the seconds of fire each counts.
constexpr std::uint32_t JINK_SEEDS = 40;
constexpr std::uint32_t JINK_TICKS = 15 * TICK_RATE;

// A unit a test stages: a design of the catalogue, its side, where it stands, and whether it faces east, toward +x, as
// side 1 starts, or west.
struct Staged
{
  std::string_view design;
  std::uint8_t side;
  Int3 at;
  bool east;
};

[[nodiscard]] GameCore::SkirmishLayout Stage(std::initializer_list<Staged> _units)
{
  GameCore::SkirmishLayout layout;
  for (const Staged& unit : _units)
  {
    const NeuronCore::Rotation turn =
      unit.east ? GameCore::FACING_SIDE_2 : NeuronCore::ComposeRotations(GameCore::HALF_TURN, GameCore::FACING_SIDE_2);
    layout.units.push_back({unit.design, unit.side, {unit.at, turn}});
  }
  return layout;
}

void Order(GameLogic::Skirmish& _skirmish, std::uint8_t _side, const GameCore::Order& _order)
{
  const NeuronCore::Command command{NeuronCore::CommandKind::Game, 0, GameCore::EncodeOrder(_order)};
  Assert::IsFalse(_skirmish.Refuses(command, _side).has_value(), L"the side may give the order");
  _skirmish.ApplyGameCommand(command.payload, _side);
}

// The observer's snapshot of _skirmish after world tick _tick, whole, as its bytes: what a replay compares.
[[nodiscard]] Bytes ObservedBytes(const GameLogic::Skirmish& _skirmish, std::uint64_t _tick)
{
  NeuronCore::Snapshot snapshot = DescribeSkirmish(_skirmish);
  snapshot.tick = _tick;
  snapshot.worldTick = _tick;
  return NeuronCore::EncodeMessage(snapshot);
}

// The mask _snapshot holds for entity _id, or nothing when the entity is whole or absent.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> MaskOf(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id)
{
  const auto mask = std::ranges::find(_snapshot.masks, _id, &NeuronCore::EntityMask::entity);
  return mask == _snapshot.masks.end() ? std::nullopt : std::optional(mask->gone);
}

[[nodiscard]] bool Holds(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id)
{
  return std::ranges::find(_snapshot.entities, _id, &NeuronCore::EntityState::id) != _snapshot.entities.end();
}

[[nodiscard]] bool Detonated(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id)
{
  return std::ranges::find(_snapshot.detonations, _id, &NeuronCore::DetonationEvent::entity) != _snapshot.detonations.end();
}

// A duel of a gunship of side 1 and a lancer of side 2, each attacking the other, from 400 units apart.
[[nodiscard]] std::unique_ptr<GameLogic::Skirmish> Duel(std::uint32_t _seed, bool _exchanged = false)
{
  const std::uint8_t first = _exchanged ? 2 : 1;
  const std::uint8_t second = _exchanged ? 1 : 2;
  auto skirmish =
    MakeStagedSkirmish({.seed = _seed}, Stage({{"Gunship", first, {-200, 0, 0}, true}, {"Lancer", second, {200, 0, 0}, false}}));
  Order(*skirmish, first, {GameCore::OrderKind::Attack, {1}, 0.0f, 0.0f, 2});
  Order(*skirmish, second, {GameCore::OrderKind::Attack, {2}, 0.0f, 0.0f, 1});
  return skirmish;
}

// The share of the shells fired at entity _target over _ticks that hit it, in a staging of a gunship of side 1 holding
// 500 units west of _target, a design of side 2 holding too, for each of JINK_SEEDS seeds, jinking or not: as counts.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> ShellsAtHolding(std::string_view _design, bool _jinking)
{
  std::uint32_t hit = 0;
  std::uint32_t at = 0;
  for (std::uint32_t seed = 1; seed <= JINK_SEEDS; ++seed)
  {
    auto skirmish = MakeStagedSkirmish({.seed = seed, .jinking = _jinking},
                                       Stage({{"Gunship", 1, {-250, 0, 0}, true}, {_design, 2, {250, 0, 0}, false}}));
    Order(*skirmish, 1, {GameCore::OrderKind::Hold, {1}, 0.0f, 0.0f});
    Order(*skirmish, 2, {GameCore::OrderKind::Hold, {2}, 0.0f, 0.0f});
    for (std::uint64_t tick = 1; tick <= JINK_TICKS && skirmish->Losses().empty(); ++tick)
    {
      skirmish->Advance(tick);
    }
    hit += skirmish->Record(2).shellsHit;
    at += skirmish->Record(2).shellsAt;
  }
  return {hit, at};
}

// A body of _cells for the sweep's own tests, standing at _position unturned.
struct TestBody
{
  GameLogic::VoxelBody body;
  std::vector<float> toughness;
  std::vector<float> damage;
  std::vector<std::uint8_t> gone;
};

[[nodiscard]] GameLogic::SweptEntity Swept(const TestBody& _body, std::uint32_t _id, std::uint8_t _side, bool _damageable, Float3 _position)
{
  return {_id,
          _side,
          _damageable,
          &_body.body,
          GameLogic::PlaceOf(_body.body, _position, NeuronCore::IDENTITY_ROTATION),
          {0.0f, 0.0f, 0.0f},
          _body.gone,
          _body.damage,
          _body.toughness};
}

// A rod of _length voxels along +x from the origin, each of toughness _toughness.
[[nodiscard]] TestBody Rod(std::int32_t _length, float _toughness)
{
  std::vector<Int3> cells;
  cells.reserve(static_cast<std::size_t>(_length));
  for (std::int32_t x = 0; x < _length; ++x)
  {
    cells.push_back({x, 0, 0});
  }
  TestBody rod{GameLogic::VoxelBody(cells), std::vector<float>(cells.size(), _toughness), {}, {}};
  return rod;
}

[[nodiscard]] float SpentOn(std::span<const GameLogic::SpentDamage> _spent, std::size_t _entity, std::uint32_t _voxel)
{
  float damage = 0.0f;
  for (const GameLogic::SpentDamage& spent : _spent)
  {
    damage += spent.entity == _entity && spent.voxel == _voxel ? spent.damage : 0.0f;
  }
  return damage;
}

} // namespace

// Design/ADR/ADR-035: the combat of the skirmish, by the plan's tests (Design/MvpPlan.md, phase 5) and the rules they
// stand on.
TEST_CLASS(CombatTests)
{
public:
  // G39: a shot spends its damage voxel by voxel along its line and carries the rest on; where it stops, what it has left
  // spreads over its reach; it passes its shooter, stops at its own side's voxels and at an asteroid's without damage.
  TEST_METHOD(SpendsAShotAlongItsLine)
  {
    const TestBody rod = Rod(10, 10.0f);
    const std::vector<GameLogic::SweptEntity> entities{Swept(rod, 2, 2, true, {5.0f, 0.5f, 0.5f})};
    // Along +x through the rod's axis, from 5 units before it: 35 damage takes three voxels and stops in the fourth.
    const GameLogic::Shot shot{{-5.0f, 0.5f, 0.5f}, {40.0f, 0.0f, 0.0f}, 0.0f, 35.0f, 1.0f, 1, 1, 2};
    std::vector<GameLogic::SpentDamage> spent;
    const GameLogic::ShotOutcome outcome = GameLogic::SweepShot(shot, entities, spent);
    Assert::IsTrue(outcome.stopped && outcome.damagedTarget, L"it stops in its target");
    for (std::uint32_t voxel = 0; voxel < 3; ++voxel)
    {
      Assert::AreEqual(10.0f, SpentOn(spent, 0, voxel), std::format(L"voxel {} takes all it has", voxel).c_str());
    }
    // The last 5 spread over the fourth and its neighbors within reach along the rod: the third is gone, so the fourth and
    // the fifth share it.
    Assert::AreEqual(2.5f, SpentOn(spent, 0, 3), L"the voxel it stops at");
    Assert::AreEqual(2.5f, SpentOn(spent, 0, 4), L"and the one beyond, within its reach");
    Assert::AreEqual(0.0f, SpentOn(spent, 0, 5), L"and nothing further");
    Assert::AreEqual((0.0 + 5.0 / 40.0 + 3.0 / 40.0) * 1.0, outcome.stopFraction, 1.0e-6, L"it stops where it enters the fourth");

    // Of its own side, or an asteroid, it stops at the first voxel, and spends nothing.
    for (const auto& [side, damageable] :
         {std::pair<std::uint8_t, bool>{std::uint8_t{1}, true}, std::pair<std::uint8_t, bool>{std::uint8_t{0}, false}})
    {
      const std::vector<GameLogic::SweptEntity> blocking{Swept(rod, 2, side, damageable, {5.0f, 0.5f, 0.5f})};
      std::vector<GameLogic::SpentDamage> none;
      const GameLogic::ShotOutcome stopped = GameLogic::SweepShot(shot, blocking, none);
      Assert::IsTrue(stopped.stopped && none.empty() && !stopped.damagedTarget, L"it stops at a hull it may not damage");
    }
    // Its shooter it passes.
    const std::vector<GameLogic::SweptEntity> shooter{Swept(rod, 1, 1, true, {5.0f, 0.5f, 0.5f})};
    std::vector<GameLogic::SpentDamage> none;
    Assert::IsFalse(GameLogic::SweepShot(shot, shooter, none).stopped, L"it passes its shooter");
    Assert::IsTrue(none.empty(), L"and spends nothing on it");
  }

  // G72: a sweep meets each entity as it stood when the tick began, by the shot's motion relative to it.
  TEST_METHOD(SweepsAShellRelativeToWhatItMeets)
  {
    TestBody rod = Rod(1, 10.0f);
    // A voxel 20 units ahead, flying away at the shell's own speed: the shell never closes on it within the tick.
    std::vector<GameLogic::SweptEntity> entities{Swept(rod, 2, 2, true, {20.5f, 0.5f, 0.5f})};
    entities[0].velocity = {300.0f, 0.0f, 0.0f};
    const GameLogic::Shot shell{{0.0f, 0.5f, 0.5f}, {30.0f, 0.0f, 0.0f}, 0.1f, 50.0f, 1.0f, 1, 1, 2};
    std::vector<GameLogic::SpentDamage> spent;
    Assert::IsFalse(GameLogic::SweepShot(shell, entities, spent).damagedTarget, L"what flies with the shell is not met");
    entities[0].velocity = {-300.0f, 0.0f, 0.0f};
    Assert::IsTrue(GameLogic::SweepShot(shell, entities, spent).damagedTarget, L"what flies into it is");
  }

  // G58: a shell is fired at where its target would be if it held its velocity.
  TEST_METHOD(LeadsAShell)
  {
    const auto lead = GameLogic::LeadShot({0.0f, 0.0f, 0.0f}, {300.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 60.0f}, 300.0f);
    Assert::IsTrue(lead.has_value(), L"a lead exists");
    const GameLogic::Lead found = lead.value_or(GameLogic::Lead{});
    const Float3 meet = Float3{300.0f, 0.0f, 0.0f} + Float3{0.0f, 0.0f, 60.0f} * found.seconds;
    const Float3 shell = found.direction * (300.0f * found.seconds);
    Assert::AreEqual(0.0f, NeuronCore::Length(meet - shell), 1.0e-3f, L"the shell and the target meet");
    Assert::IsFalse(GameLogic::LeadShot({0.0f, 0.0f, 0.0f}, {300.0f, 0.0f, 0.0f}, {400.0f, 0.0f, 0.0f}, 300.0f).has_value(),
                    L"no shell catches a target that flies away faster than it");
  }

  // §8.3: the cells beside a removal that still connect near it cut nothing off; a rod cut in two does.
  TEST_METHOD(KnowsWhenARemovalCutsNothing)
  {
    TestBody rod = Rod(9, 10.0f);
    rod.gone.assign(2, 0);
    rod.gone[0] = 1u << 4; // the middle voxel
    const std::vector<std::uint32_t> middle{4};
    Assert::IsFalse(GameLogic::CutsNothing(rod.body, rod.gone, middle), L"a rod cut in two");
    std::vector<Int3> slab;
    for (std::int32_t z = 0; z < 5; ++z)
    {
      for (std::int32_t x = 0; x < 5; ++x)
      {
        slab.push_back({x, 0, z});
      }
    }
    const GameLogic::VoxelBody plate(slab);
    std::vector<std::uint8_t> gone(4, 0);
    gone[12 / 8] = static_cast<std::uint8_t>(1u << (12 % 8)); // the plate's middle
    const std::vector<std::uint32_t> hole{12};
    Assert::IsTrue(GameLogic::CutsNothing(plate, gone, hole), L"a hole in a plate");
  }

  // The plan's test: a seeded duel runs the same twice, to the byte, tick by tick (R21).
  TEST_METHOD(RunsASeededDuelTheSameTwice)
  {
    constexpr std::uint64_t TICKS = Ticks(40);
    const auto first = Duel(7);
    const auto second = Duel(7);
    bool lost = false;
    for (std::uint64_t tick = 1; tick <= TICKS; ++tick)
    {
      first->Advance(tick);
      second->Advance(tick);
      Assert::IsTrue(ObservedBytes(*first, tick) == ObservedBytes(*second, tick), std::format(L"tick {}", tick).c_str());
      lost = lost || !first->Losses().empty();
    }
    Assert::IsTrue(first->Record(1).voxelsLost > 0 && first->Record(2).voxelsLost > 0, L"both took damage");
    Assert::IsTrue(lost, L"and one was lost");
  }

  // The plan's test (G72): a duel with its sides exchanged gives the same result with the sides exchanged, since no side's
  // shots resolve before another's.
  TEST_METHOD(GivesAMirroredDuelTheMirroredResult)
  {
    constexpr std::uint64_t TICKS = Ticks(120);
    for (const std::uint32_t seed : {1u, 2u, 3u})
    {
      const auto duel = Duel(seed);
      const auto mirror = Duel(seed, true);
      for (std::uint64_t tick = 1; tick <= TICKS && duel->Losses().empty(); ++tick)
      {
        duel->Advance(tick);
        mirror->Advance(tick);
      }
      const std::wstring what = std::format(L"seed {}", seed);
      Assert::IsFalse(duel->Losses().empty(), (what + L": the duel ends").c_str());
      Assert::AreEqual(duel->Losses().size(), mirror->Losses().size(), what.c_str());
      for (std::size_t loss = 0; loss < duel->Losses().size(); ++loss)
      {
        Assert::AreEqual(duel->Losses()[loss].entity, mirror->Losses()[loss].entity, (what + L": the same entity is lost").c_str());
        Assert::AreEqual(duel->Losses()[loss].worldTick, mirror->Losses()[loss].worldTick, (what + L": at the same tick").c_str());
        Assert::IsTrue(duel->Losses()[loss].kind == mirror->Losses()[loss].kind, (what + L": the same way").c_str());
      }
      const NeuronCore::Snapshot left = DescribeSkirmish(*duel);
      const NeuronCore::Snapshot right = DescribeSkirmish(*mirror);
      for (const std::uint32_t id : {1u, 2u})
      {
        Assert::IsTrue(MaskOf(left, id) == MaskOf(right, id), (what + std::format(L": entity {}'s voxels", id)).c_str());
      }
    }
  }

  // The plan's test (G71): a ship whose every line to its target meets its own side's hull holds fire and damages nothing;
  // with the hull gone from the line, it fires.
  TEST_METHOD(HoldsFireBehindItsOwnSidesHull)
  {
    constexpr std::uint64_t TICKS = Ticks(10);
    for (const bool blocked : {true, false})
    {
      auto skirmish = blocked
                        ? MakeStagedSkirmish(
                            {.seed = 3, .jinking = false},
                            Stage({{"Gunship", 1, {-300, 0, 0}, true}, {"Miner", 1, {-250, 0, 0}, true}, {"Miner", 2, {100, 0, 0}, false}}))
                        : MakeStagedSkirmish({.seed = 3, .jinking = false},
                                             Stage({{"Gunship", 1, {-300, 0, 0}, true}, {"Miner", 2, {100, 0, 0}, false}}));
      const std::uint32_t target = blocked ? 3 : 2;
      for (std::uint64_t tick = 1; tick <= TICKS; ++tick)
      {
        skirmish->Advance(tick);
      }
      const NeuronCore::Snapshot snapshot = DescribeSkirmish(*skirmish);
      if (blocked)
      {
        Assert::AreEqual(std::uint32_t{0}, skirmish->Record(1).shellsFired, L"it holds fire");
        Assert::IsFalse(MaskOf(snapshot, target).has_value(), L"its target is whole");
        Assert::IsFalse(MaskOf(snapshot, 2).has_value(), L"and so is the hull in the way");
      }
      else
      {
        Assert::IsTrue(skirmish->Record(1).shellsFired > 0 && MaskOf(snapshot, target).has_value(), L"with its line clear, it fires");
      }
    }
  }

  // The plan's test (G58): a shell leads a target flying straight, and hits it. The target comes into range at its cruise,
  // flying across the shooter's line; the shooter is lost once it has fired its shells, which fly on.
  TEST_METHOD(LeadsAShellOntoATargetFlyingStraight)
  {
    constexpr std::uint32_t SHELLS = 8;
    auto skirmish =
      MakeStagedSkirmish({.seed = 5, .jinking = false}, Stage({{"Gunship", 1, {-350, 0, 0}, true}, {"Miner", 2, {0, 0, -900}, false}}));
    Order(*skirmish, 1, {GameCore::OrderKind::Hold, {1}, 0.0f, 0.0f});
    Order(*skirmish, 2, {GameCore::OrderKind::Move, {2}, 0.0f, 900.0f});
    std::uint64_t tick = 1;
    for (; tick <= Ticks(30) && skirmish->Record(1).shellsFired < SHELLS; ++tick)
    {
      skirmish->Advance(tick);
    }
    Assert::AreEqual(SHELLS, skirmish->Record(1).shellsFired, L"the shooter fired its shells");
    skirmish->Detonate(1, tick);
    const std::uint64_t landed = tick + Ticks(3); // longer than a shell flies its range
    for (; tick <= landed; ++tick)
    {
      skirmish->Advance(tick);
    }
    Assert::AreEqual(SHELLS, skirmish->Record(2).shellsAt, L"at the target");
    Assert::AreEqual(SHELLS, skirmish->Record(2).shellsHit, L"and every one hit it");
  }

  // The plan's test (G58): over 40 seeds, jinking lowers the mass driver's hits on a gunship, and not on a cruiser, which
  // its side thrust moves too little in a shell's flight.
  TEST_METHOD(JinksShellsAsideWithinItsAgility)
  {
    const auto [gunshipJinking, gunshipJinkingAt] = ShellsAtHolding("Gunship", true);
    const auto [gunshipSteady, gunshipSteadyAt] = ShellsAtHolding("Gunship", false);
    const auto [cruiserJinking, cruiserJinkingAt] = ShellsAtHolding("Cruiser", true);
    const auto [cruiserSteady, cruiserSteadyAt] = ShellsAtHolding("Cruiser", false);
    const double gunshipJinks = static_cast<double>(gunshipJinking) / gunshipJinkingAt;
    const double gunshipHolds = static_cast<double>(gunshipSteady) / gunshipSteadyAt;
    const double cruiserJinks = static_cast<double>(cruiserJinking) / cruiserJinkingAt;
    const double cruiserHolds = static_cast<double>(cruiserSteady) / cruiserSteadyAt;
    Logger::WriteMessage(
      std::format("shells hit a gunship {:.3f} of the time jinking and {:.3f} holding still; a cruiser {:.3f} and {:.3f}\n", gunshipJinks,
                  gunshipHolds, cruiserJinks, cruiserHolds)
        .c_str());
    Assert::IsTrue(gunshipJinks < 0.8 * gunshipHolds, L"a gunship's jinking turns a fifth of the shells aside, at least");
    Assert::IsTrue(cruiserJinks > 0.95 * cruiserHolds, L"a cruiser's does next to nothing");
  }

  // The plan's test: what a side receives of an entity's voxels is the server's, bit for bit, in every snapshot that holds
  // it, whether it stayed in sight or left it and came back.
  TEST_METHOD(SendsEachSideTheServersMasks)
  {
    auto skirmish = MakeStagedSkirmish({.seed = 2}, Stage({{"Gunship", 1, {-100, 0, 0}, true}, {"Gunship", 2, {300, 0, 0}, false}}));
    NeuronServer::ServerHost host(*skirmish);
    std::map<std::uint8_t, std::unique_ptr<NeuronCore::Transport>> clients;
    for (const std::uint8_t side : {std::uint8_t{1}, NeuronCore::OBSERVER_SIDE})
    {
      NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
      host.AddSession(std::move(pair.server), side);
      Assert::IsTrue(pair.client->Send(NeuronCore::EncodeMessage(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION})), L"hello");
      clients[side] = std::move(pair.client);
    }
    // They fight; then side 2's gunship leaves side 1's sight, is restored whole out of it, is fought again on its return.
    const auto order = [&](std::uint8_t _side, const GameCore::Order& _order)
    {
      const NeuronCore::Command command{NeuronCore::CommandKind::Game, 0, GameCore::EncodeOrder(_order)};
      Assert::IsFalse(skirmish->Refuses(command, _side).has_value(), L"the order is the side's");
      skirmish->ApplyGameCommand(command.payload, _side);
    };
    bool left = false;
    bool returned = false;
    std::size_t compared = 0;
    for (std::uint64_t step = 1; step <= Ticks(50); ++step)
    {
      if (step == Ticks(6))
      {
        order(2, {GameCore::OrderKind::Move, {2}, 1400.0f, 0.0f});
        order(1, {GameCore::OrderKind::Hold, {1}, 0.0f, 0.0f});
      }
      if (step == Ticks(30))
      {
        skirmish->Restore(2);
        order(2, {GameCore::OrderKind::Move, {2}, 250.0f, 0.0f});
      }
      host.Step();
      std::map<std::uint8_t, NeuronCore::Snapshot> last;
      for (auto& [side, client] : clients)
      {
        while (std::optional<Bytes> bytes = client->Receive())
        {
          auto message = NeuronCore::DecodeMessage(*bytes, COUNTS);
          Assert::IsTrue(message.has_value(), L"the message decodes");
          if (auto* snapshot = message ? std::get_if<NeuronCore::Snapshot>(&*message) : nullptr)
          {
            last[side] = std::move(*snapshot);
          }
        }
      }
      if (last.size() < 2)
      {
        continue;
      }
      const NeuronCore::Snapshot& seen = last[1];
      const NeuronCore::Snapshot& whole = last[NeuronCore::OBSERVER_SIDE];
      left = left || (step > Ticks(6) && !Holds(seen, 2));
      returned = returned || (left && Holds(seen, 2));
      for (const NeuronCore::EntityState& entity : seen.entities)
      {
        Assert::IsTrue(MaskOf(seen, entity.id) == MaskOf(whole, entity.id), std::format(L"entity {} at step {}", entity.id, step).c_str());
        ++compared;
      }
    }
    Assert::IsTrue(left && returned, L"the enemy left sight and came back");
    Assert::IsTrue(compared > 0, L"masks were compared");
  }

  // The plan's test (G54): a shell reaches a side only while its sensors cover it, and one that flies into them from
  // outside first reaches it at their edge, never where it was fired.
  TEST_METHOD(ShowsAShellOnlyFromItsSensorsEdge)
  {
    // Side 1's gunship fires at side 2's nearer gunship, which is then lost, so that the shells in flight go on toward the
    // edge of the other's sensors, 700 units about it.
    auto skirmish =
      MakeStagedSkirmish({.seed = 4, .jinking = false},
                         Stage({{"Gunship", 1, {-560, 0, 0}, true}, {"Gunship", 2, {0, 0, 0}, false}, {"Gunship", 2, {690, 0, 0}, false}}));
    constexpr float SENSOR = 700.0f;
    constexpr float TICK_FLIGHT = 300.0f / TICK_RATE;
    bool fired = false;
    bool entered = false;
    for (std::uint64_t tick = 1; tick <= Ticks(3); ++tick)
    {
      skirmish->Advance(tick);
      if (!fired && skirmish->Record(1).shellsFired > 0)
      {
        fired = true;
        skirmish->Detonate(2, tick);
      }
      const NeuronCore::Snapshot seen = DescribeSkirmish(*skirmish, 2);
      const auto payload = GameCore::DecodeSnapshotPayload(seen.payload);
      Assert::IsTrue(payload.has_value(), L"the payload decodes");
      const bool sensing = !Detonated(DescribeSkirmish(*skirmish), 2);
      for (const GameCore::ShellState& shell : payload.value_or(GameCore::SnapshotPayload{}).shells)
      {
        if (shell.side == 2)
        {
          continue; // its own, which it always sees
        }
        const float fromFar = NeuronCore::Length(shell.position - Float3{690.0f, 0.0f, 0.0f});
        Assert::IsTrue(sensing || fromFar <= SENSOR + 1.0f, std::format(L"tick {}: a shell outside every sensor", tick).c_str());
        if (!sensing && !entered)
        {
          entered = true;
          Assert::IsTrue(fromFar > SENSOR - TICK_FLIGHT - 1.0f,
                         std::format(L"tick {}: first seen {} units in", tick, SENSOR - fromFar).c_str());
        }
      }
    }
    Assert::IsTrue(fired && entered, L"a shell was fired and came into the sensors");
  }

  // §8.3: pieces cut off become debris of their own, a loss detonates its entity with its mask, and debris leaves after
  // its time.
  TEST_METHOD(BreaksOffDebrisAndLetsItGo)
  {
    const auto skirmish = Duel(11);
    const std::size_t staged = 2;
    std::uint64_t tick = 1;
    for (; tick <= Ticks(180) && skirmish->Losses().empty(); ++tick)
    {
      skirmish->Advance(tick);
    }
    Assert::IsFalse(skirmish->Losses().empty(), L"the duel ends in a loss");
    const GameLogic::Skirmish::Loss loss = skirmish->Losses().front();
    const NeuronCore::Snapshot ended = DescribeSkirmish(*skirmish);
    Assert::IsTrue(Detonated(ended, loss.entity) && MaskOf(ended, loss.entity).has_value(), L"the lost ship detonates with its mask");
    bool pieces = false;
    for (const NeuronCore::EntityState& entity : ended.entities)
    {
      if (entity.id > staged)
      {
        pieces = true;
        Assert::IsTrue(Detonated(ended, entity.id) && MaskOf(ended, entity.id).has_value(),
                       L"a piece is debris, and holds only its voxels");
      }
    }
    Assert::IsTrue(pieces, L"pieces broke off");
    // A minute on, the debris has left the world.
    const std::uint64_t gone = loss.worldTick + static_cast<std::uint64_t>(GameLogic::Skirmish::DEBRIS_SECONDS) * TICK_RATE;
    for (; tick <= gone; ++tick)
    {
      skirmish->Advance(tick);
    }
    Assert::IsFalse(Holds(DescribeSkirmish(*skirmish), loss.entity), L"the debris is gone");
  }

  // Design/ADR/ADR-035: an attack names an entity of another side that combat fights; an attack-move goes to a point.
  TEST_METHOD(RefusesAttacksOnWhatItCannotFight)
  {
    auto skirmish = MakeStagedSkirmish(
      {.seed = 1}, Stage({{"Gunship", 1, {-300, 0, 0}, true}, {"Gunship", 1, {-300, 0, 60}, true}, {"Gunship", 2, {300, 0, 0}, false}}));
    const auto refusal = [&skirmish](std::uint32_t _target)
    {
      const NeuronCore::Command command{NeuronCore::CommandKind::Game, 0,
                                        GameCore::EncodeOrder({GameCore::OrderKind::Attack, {1}, 0.0f, 0.0f, _target})};
      return skirmish->Refuses(command, 1);
    };
    Assert::IsFalse(refusal(3).has_value(), L"an enemy");
    Assert::IsTrue(refusal(2) == NeuronServer::CommandRefusal::NotOrderable, L"a ship of its own side");
    Assert::IsTrue(refusal(99) == NeuronServer::CommandRefusal::NotOrderable, L"an entity the world does not hold");
    skirmish->Detonate(3, 0);
    Assert::IsTrue(refusal(3) == NeuronServer::CommandRefusal::NotOrderable, L"debris");
  }

  // The payload tells a side its ships' attacks and attack-moves, as its orders.
  TEST_METHOD(TellsASideItsShipsAttacks)
  {
    auto skirmish = MakeStagedSkirmish(
      {.seed = 1}, Stage({{"Gunship", 1, {-600, 0, 0}, true}, {"Gunship", 1, {-600, 0, 60}, true}, {"Gunship", 2, {0, 0, 0}, false}}));
    Order(*skirmish, 1, {GameCore::OrderKind::Attack, {1}, 0.0f, 0.0f, 3});
    Order(*skirmish, 1, {GameCore::OrderKind::AttackMove, {2}, 0.0f, 100.0f});
    skirmish->Advance(1);
    const auto payload = GameCore::DecodeSnapshotPayload(DescribeSkirmish(*skirmish, 1).payload);
    Assert::IsTrue(payload.has_value() && payload->orders.size() == 2, L"both ships' orders");
    Assert::IsTrue(payload->orders[0].state == GameCore::ShipState::Attacking && payload->orders[0].target == 3, L"the attack");
    Assert::IsTrue(payload->orders[1].state == GameCore::ShipState::AttackMoving, L"the attack-move");
    const auto enemy = GameCore::DecodeSnapshotPayload(DescribeSkirmish(*skirmish, 2).payload);
    Assert::IsTrue(enemy.has_value() && enemy->orders.empty(), L"which the other side does not learn");
  }
};

} // namespace GameLogicTests
