#include "pch.h"

#include "Clearances.h"
#include "ShipMotion.h"
#include "Skirmish.h"
#include "TestSupport.h"

#include "Catalogue.h"
#include "Design.h"
#include "Orders.h"
#include "Profile.h"
#include "SkirmishLayout.h"
#include "WelcomeNames.h"

#include "CommandLog.h"
#include "ServerHost.h"

#include "Composite.h"
#include "Hash.h"
#include "LoopbackTransport.h"
#include "Message.h"
#include "NvfModel.h"
#include "Quaternion.h"
#include "VoxModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
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

// The entities by id (Design/ADR/ADR-030): side 1's core and its four ships, side 2's, then the asteroids.
constexpr std::uint32_t SIDE_1_CORE = 1;
constexpr std::array<std::uint32_t, 4> SIDE_1_SHIPS{2, 3, 4, 5};
constexpr std::array<std::uint32_t, 4> SIDE_2_SHIPS{7, 8, 9, 10};
constexpr std::uint32_t FIRST_ASTEROID = 11;

constexpr std::uint32_t TICK_RATE = 30;
constexpr float SECONDS = 1.0f / static_cast<float>(TICK_RATE);

// The plan's seeds of random orders (Design/MvpPlan.md, phase 4), the rounds of orders each side gives on each, and the
// longest a round's moves may take: a crossing of the sector and back is 80 s at a frigate's cap.
constexpr std::uint32_t ORDER_SEEDS = 20;
constexpr std::uint32_t ROUNDS = 3;
constexpr std::uint32_t ROUND_TICKS = 150 * TICK_RATE;

// After a round's moves end, the ticks the ships are given to brake to a halt: longer than a frigate's two seconds.
constexpr std::uint32_t SETTLE_TICKS = 3 * TICK_RATE;

// How near its destination a ship comes to rest, in units: its move ends within ARRIVAL_RADIUS of it, and it brakes
// from there to a halt. The worst measured over 100 seeds is 1.99.
constexpr float ARRIVAL_TOLERANCE = GameLogic::ARRIVAL_RADIUS + 1.0f;

// How far a flight limit may seem exceeded, as a share of it: float rounding, as ADR-017 allows.
constexpr float LIMIT_ROUNDING = 1.0e-3f;

// What the test holds each composite to, worked out here: its sphere, half its box's diagonal, from GameData's models;
// and for a ship's design, its profile's speed cap, acceleration and turn rate, which are 0 for anything that does not
// fly.
struct Figures
{
  std::vector<float> spheres;
  std::vector<float> speedCaps;
  std::vector<float> accelerations;
  std::vector<float> turnRates;
};

[[nodiscard]] Figures FiguresOf(const GameLogic::Skirmish& _skirmish)
{
  std::vector<NeuronCore::VoxModel> models;
  for (const NeuronCore::ManifestEntry& entry : _skirmish.Manifest())
  {
    const auto bytes = NeuronCore::ReadNvfFile(GameDataDirectory() / (entry.name + ".nvf"));
    const auto parsed = NeuronCore::ParseNvfModel(bytes.value_or(Bytes{}));
    Assert::IsTrue(parsed.has_value(), L"every model reads");
    models.push_back(NeuronCore::FlattenNvfModel(parsed.value_or(NeuronCore::NvfModel{})));
  }
  const GameCore::WelcomeNames names = GameCore::DecodeWelcomeNames(_skirmish.WelcomePayload()).value_or(GameCore::WelcomeNames{});
  Figures figures;
  for (std::size_t index = 0; index < _skirmish.Composites().size(); ++index)
  {
    const auto bounds = NeuronCore::CompositeBounds(models, _skirmish.Composites()[index]);
    Assert::IsTrue(bounds.has_value(), L"every composite has voxels");
    const NeuronCore::VoxelBounds box = bounds.value_or(NeuronCore::VoxelBounds{});
    const NeuronCore::Int3 extent = box.upper - box.lower;
    figures.spheres.push_back(
      0.5f * NeuronCore::Length({static_cast<float>(extent.x), static_cast<float>(extent.y), static_cast<float>(extent.z)}));
    GameCore::Profile profile{};
    const GameCore::DesignSpec* spec = index < names.composites.size() ? GameCore::FindDesign(names.composites[index]) : nullptr;
    if (spec != nullptr && spec->kind == GameCore::DesignKind::Ship)
    {
      const auto design = GameCore::LoadDesign(*spec, GameDataDirectory());
      Assert::IsTrue(design.has_value(), L"every ship's design loads");
      if (design)
      {
        profile = GameCore::ComputeProfile(*design);
      }
    }
    figures.speedCaps.push_back(profile.speedUnitsPerSecond);
    figures.accelerations.push_back(profile.accelerationUnitsPerSecondSquared);
    figures.turnRates.push_back(profile.turnRateRadiansPerSecond);
  }
  return figures;
}

[[nodiscard]] float PlaneDistance(Float3 _a, Float3 _b) noexcept
{
  return std::hypot(_b.x - _a.x, _b.z - _a.z);
}

[[nodiscard]] const NeuronCore::EntityState& EntityOf(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id)
{
  const auto entity = std::ranges::find(_snapshot.entities, _id, &NeuronCore::EntityState::id);
  Assert::IsTrue(entity != _snapshot.entities.end(), std::format(L"entity {} is in the snapshot", _id).c_str());
  return *entity;
}

// The order states _snapshot's payload carries.
[[nodiscard]] std::vector<GameCore::ShipOrderState> StatesOf(const NeuronCore::Snapshot& _snapshot)
{
  const auto states = GameCore::DecodeOrderStates(_snapshot.payload);
  Assert::IsTrue(states.has_value(), L"the payload holds the order states");
  return states.value_or(std::vector<GameCore::ShipOrderState>{});
}

// Whether a ship moves in the snapshot _snapshot, by its order states.
[[nodiscard]] bool AnyMoving(const NeuronCore::Snapshot& _snapshot)
{
  return std::ranges::any_of(StatesOf(_snapshot),
                             [](const GameCore::ShipOrderState& _state) { return _state.state == GameCore::ShipState::Moving; });
}

// The game's command that carries _order.
[[nodiscard]] NeuronCore::Command OrderCommand(const GameCore::Order& _order)
{
  return {NeuronCore::CommandKind::Game, 0, GameCore::EncodeOrder(_order)};
}

// The cores and the asteroids, which never move, as the flight keeps clear of them: each one's middle and sphere.
[[nodiscard]] std::vector<std::pair<Float3, float>> ObstaclesOf(const NeuronCore::Snapshot& _snapshot, const Figures& _figures)
{
  std::vector<std::pair<Float3, float>> obstacles;
  for (const NeuronCore::EntityState& entity : _snapshot.entities)
  {
    if (_figures.speedCaps[entity.composite] <= 0.0f)
    {
      obstacles.emplace_back(entity.position, _figures.spheres[entity.composite]);
    }
  }
  return obstacles;
}

// One tick of the observer's snapshots, _after against _before: every ship within its profile's speed cap, acceleration
// and turn rate, and clear of every one of _obstacles' spheres. Returns the least clearance of a sphere. Each failure is
// described only when it happens: a test of 80,000 ticks cannot afford a message for every check.
[[nodiscard]] float CheckTick(const NeuronCore::Snapshot& _before, const NeuronCore::Snapshot& _after, const Figures& _figures,
                              std::span<const std::pair<Float3, float>> _obstacles, const std::wstring& _what)
{
  float least = std::numeric_limits<float>::max();
  for (std::size_t index = 0; index < _after.entities.size(); ++index)
  {
    const NeuronCore::EntityState& ship = _after.entities[index];
    const float cap = _figures.speedCaps[ship.composite];
    if (cap <= 0.0f)
    {
      continue;
    }
    const NeuronCore::EntityState& was = _before.entities[index];
    const float speed = NeuronCore::Length(ship.velocity);
    const float change = std::abs(speed - NeuronCore::Length(was.velocity));
    const Float3 heading = NeuronCore::RotationOf(ship.rotation).axisZ;
    const Float3 wasHeading = NeuronCore::RotationOf(was.rotation).axisZ;
    const float turn = std::atan2(NeuronCore::Length(NeuronCore::Cross(wasHeading, heading)), NeuronCore::Dot(wasHeading, heading));
    if (speed > cap * (1.0f + LIMIT_ROUNDING))
    {
      Assert::Fail((_what + std::format(L", ship {}: {} units/s", ship.id, speed)).c_str());
    }
    if (change > _figures.accelerations[ship.composite] * SECONDS * (1.0f + LIMIT_ROUNDING))
    {
      Assert::Fail((_what + std::format(L", ship {}: {} units/s in a tick", ship.id, change)).c_str());
    }
    if (turn > _figures.turnRates[ship.composite] * SECONDS * (1.0f + LIMIT_ROUNDING) + 1.0e-6f)
    {
      Assert::Fail((_what + std::format(L", ship {}: {} radians in a tick", ship.id, turn)).c_str());
    }
    const float sphere = _figures.spheres[ship.composite];
    for (const auto& [middle, radius] : _obstacles)
    {
      const float clearance = NeuronCore::Length(ship.position - middle) - sphere - radius;
      if (clearance < 0.0f)
      {
        Assert::Fail((_what + std::format(L", ship {} is {} units into a sphere", ship.id, -clearance)).c_str());
      }
      least = std::min(least, clearance);
    }
  }
  return least;
}

// A draw of PcgHash from an index and a stream offset by _seed (R21), and one between _low and _high.
[[nodiscard]] std::uint32_t Bits(std::uint32_t _seed, std::uint32_t _index) noexcept
{
  return NeuronCore::PcgHash(_index * 16u + 0x4Du + _seed * 0x9E3779B9u);
}

[[nodiscard]] float Between(std::uint32_t _seed, std::uint32_t _index, float _low, float _high) noexcept
{
  return _low + (_high - _low) * (static_cast<float>(Bits(_seed, _index) >> 8u) / 16777216.0f);
}

// A random order of some of a side's _ships: a move to anywhere in the sector and a little beyond, eight times in ten,
// and otherwise a stop or a hold.
[[nodiscard]] GameCore::Order RandomOrder(std::uint32_t _seed, std::uint32_t& _draw, std::span<const std::uint32_t> _ships)
{
  GameCore::Order order{GameCore::OrderKind::Move, {}, 0.0f, 0.0f};
  for (const std::uint32_t ship : _ships)
  {
    if ((Bits(_seed, _draw++) & 1u) != 0)
    {
      order.ships.push_back(ship);
    }
  }
  if (order.ships.empty())
  {
    order.ships.push_back(_ships[Bits(_seed, _draw++) % _ships.size()]);
  }
  const std::uint32_t kind = Bits(_seed, _draw++) % 10u;
  if (kind == 8)
  {
    order.kind = GameCore::OrderKind::Stop;
  }
  else if (kind == 9)
  {
    order.kind = GameCore::OrderKind::Hold;
  }
  else
  {
    order.targetX = Between(_seed, _draw++, -2400.0f, 2400.0f);
    order.targetZ = Between(_seed, _draw++, -1400.0f, 1400.0f);
  }
  return order;
}

// A client's end of a loopback to a host, and the messages it receives.
struct Client
{
  std::unique_ptr<NeuronCore::Transport> transport;

  void Send(const NeuronCore::Message& _message) const
  {
    Assert::IsTrue(transport->Send(NeuronCore::EncodeMessage(_message)), L"the client sends");
  }

  // The last snapshot of those received since the last call.
  [[nodiscard]] NeuronCore::Snapshot LastSnapshot() const
  {
    std::optional<NeuronCore::Snapshot> last;
    while (std::optional<Bytes> bytes = transport->Receive())
    {
      auto message = NeuronCore::DecodeMessage(*bytes, {8, GameCore::SIDE_COUNT});
      Assert::IsTrue(message.has_value(), L"the server's message decodes");
      if (auto* snapshot = message ? std::get_if<NeuronCore::Snapshot>(&*message) : nullptr)
      {
        last = std::move(*snapshot);
      }
    }
    Assert::IsTrue(last.has_value(), L"a snapshot came");
    return last.value_or(NeuronCore::Snapshot{});
  }
};

[[nodiscard]] Client Join(NeuronServer::ServerHost& _host, std::uint8_t _side)
{
  NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
  _host.AddSession(std::move(pair.server), _side);
  Client client{std::move(pair.client)};
  client.Send(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION});
  return client;
}

} // namespace

// Design/ADR/ADR-033: a side's orders, and the flight that carries them out on the plane.
TEST_CLASS(SkirmishOrdersTests)
{
public:
  // The plan's test (Design/MvpPlan.md, phase 4): over 20 seeds of random orders to both sides' ships, every ship keeps to
  // its profile's limits and out of every core's and asteroid's sphere, every move ends, and every ship comes to rest
  // at its destination, or beside a ship halted there first.
  TEST_METHOD(FliesRandomOrdersClearOfEverySphere)
  {
    const Figures figures = FiguresOf(*MakeSkirmish(1));
    float least = std::numeric_limits<float>::max();
    float worstArrival = 0.0f;
    std::size_t arrivals = 0;
    std::size_t taken = 0;
    std::uint32_t longest = 0;
    for (std::uint32_t seed = 1; seed <= ORDER_SEEDS; ++seed)
    {
      const auto skirmish = MakeSkirmish(seed);
      NeuronCore::Snapshot before = DescribeSkirmish(*skirmish);
      const std::vector<std::pair<Float3, float>> obstacles = ObstaclesOf(before, figures);
      std::uint32_t draw = 0;
      for (std::uint32_t round = 0; round < ROUNDS; ++round)
      {
        const std::wstring what = std::format(L"seed {}, round {}", seed, round);
        for (const std::uint8_t side : {std::uint8_t{1}, std::uint8_t{2}})
        {
          const NeuronCore::Command command = OrderCommand(RandomOrder(seed, draw, side == 1 ? SIDE_1_SHIPS : SIDE_2_SHIPS));
          Assert::IsFalse(skirmish->Refuses(command, side).has_value(), (what + L": a side orders its own ships").c_str());
          skirmish->ApplyGameCommand(command.payload, side);
        }
        before = DescribeSkirmish(*skirmish);
        std::map<std::uint32_t, Float3> destinations;
        for (const GameCore::ShipOrderState& state : StatesOf(before))
        {
          if (state.state == GameCore::ShipState::Moving)
          {
            destinations[state.ship] = {state.destinationX, 0.0f, state.destinationZ};
          }
        }

        // The moves, until none is left, and then the ships' brakes.
        std::uint32_t tick = 0;
        for (; tick < ROUND_TICKS + SETTLE_TICKS; ++tick)
        {
          if (tick < ROUND_TICKS && !AnyMoving(before))
          {
            longest = std::max(longest, tick);
            tick = ROUND_TICKS;
          }
          skirmish->Advance(0);
          NeuronCore::Snapshot after = DescribeSkirmish(*skirmish);
          least = std::min(least, CheckTick(before, after, figures, obstacles, what));
          before = std::move(after);
        }
        Assert::IsFalse(AnyMoving(before), (what + L": every move ends").c_str());

        for (const auto& [ship, destination] : destinations)
        {
          const NeuronCore::EntityState& state = EntityOf(before, ship);
          const float distance = PlaneDistance(state.position, destination);
          if (distance <= ARRIVAL_TOLERANCE)
          {
            worstArrival = std::max(worstArrival, distance);
            ++arrivals;
            continue;
          }
          // Taken: another ship halted within their spacing of the destination, and this one within the spacing and
          // CLEARANCE_MARGIN more of it.
          const auto takes = [&](const NeuronCore::EntityState& _other)
          {
            const float spacing = figures.spheres[state.composite] + figures.spheres[_other.composite] + GameLogic::CLEARANCE_MARGIN;
            return _other.id != ship && figures.speedCaps[_other.composite] > 0.0f &&
                   PlaneDistance(_other.position, destination) <= spacing && distance <= spacing + GameLogic::CLEARANCE_MARGIN;
          };
          Assert::IsTrue(std::ranges::any_of(before.entities, takes),
                         (what + std::format(L": ship {} rests {} units from its destination", ship, distance)).c_str());
          ++taken;
        }
      }
    }
    Logger::WriteMessage(std::format(L"random orders: {} arrivals, the worst {} units off; {} beside a ship halted first; the longest "
                                     L"round {} s; {} units clear of every sphere\n",
                                     arrivals, worstArrival, taken, static_cast<float>(longest) * SECONDS, least)
                           .c_str());
  }

  // The plan's test: side 1's four ships ordered as one across open water keep their spacing as they fly, as they turn,
  // speed up and slow down alike, and each comes to rest at its own place moved by the order.
  TEST_METHOD(MovesAGroupKeepingItsSpacing)
  {
    const auto skirmish = MakeSkirmish(7);
    const Figures figures = FiguresOf(*skirmish);
    NeuronCore::Snapshot before = DescribeSkirmish(*skirmish);
    const std::vector<std::pair<Float3, float>> obstacles = ObstaclesOf(before, figures);
    std::array<Float3, SIDE_1_SHIPS.size()> start{};
    Float3 middle{0.0f, 0.0f, 0.0f};
    for (std::size_t member = 0; member < SIDE_1_SHIPS.size(); ++member)
    {
      start[member] = EntityOf(before, SIDE_1_SHIPS[member]).position;
      middle = middle + start[member];
    }
    middle = middle * (1.0f / static_cast<float>(SIDE_1_SHIPS.size()));
    constexpr Float3 DISPLACEMENT{780.0f, 0.0f, -250.0f};
    skirmish->ApplyGameCommand(
      OrderCommand({GameCore::OrderKind::Move, std::vector<std::uint32_t>(SIDE_1_SHIPS.begin(), SIDE_1_SHIPS.end()),
                    middle.x + DISPLACEMENT.x, middle.z + DISPLACEMENT.z})
        .payload,
      1);
    for (const GameCore::ShipOrderState& state : StatesOf(DescribeSkirmish(*skirmish, 1)))
    {
      const auto member = static_cast<std::size_t>(std::ranges::find(SIDE_1_SHIPS, state.ship) - SIDE_1_SHIPS.begin());
      Assert::IsTrue(member < SIDE_1_SHIPS.size() && state.state == GameCore::ShipState::Moving, L"each member moves");
      Assert::AreEqual(start[member].x + DISPLACEMENT.x, state.destinationX, 1.0e-3f, L"to its own place, moved: x");
      Assert::AreEqual(start[member].z + DISPLACEMENT.z, state.destinationZ, 1.0e-3f, L"and z");
    }

    before = DescribeSkirmish(*skirmish);
    float worst = 0.0f;
    std::uint32_t tick = 0;
    for (; tick < ROUND_TICKS && AnyMoving(before); ++tick)
    {
      skirmish->Advance(0);
      NeuronCore::Snapshot after = DescribeSkirmish(*skirmish);
      static_cast<void>(CheckTick(before, after, figures, obstacles, L"the group"));
      for (std::size_t a = 0; a < SIDE_1_SHIPS.size(); ++a)
      {
        for (std::size_t b = a + 1; b < SIDE_1_SHIPS.size(); ++b)
        {
          const float spacing = NeuronCore::Length(EntityOf(after, SIDE_1_SHIPS[a]).position - EntityOf(after, SIDE_1_SHIPS[b]).position);
          worst = std::max(worst, std::abs(spacing - NeuronCore::Length(start[a] - start[b])));
        }
      }
      before = std::move(after);
    }
    Assert::IsTrue(tick < ROUND_TICKS, L"the move ends");
    Assert::IsTrue(worst <= 0.01f, std::format(L"the spacing changed by {} units", worst).c_str());
    for (std::uint32_t settle = 0; settle < SETTLE_TICKS; ++settle)
    {
      skirmish->Advance(0);
    }
    const NeuronCore::Snapshot rest = DescribeSkirmish(*skirmish);
    for (std::size_t member = 0; member < SIDE_1_SHIPS.size(); ++member)
    {
      Assert::IsTrue(PlaneDistance(EntityOf(rest, SIDE_1_SHIPS[member]).position, start[member] + DISPLACEMENT) <= ARRIVAL_TOLERANCE,
                     L"each at rest at its own place");
    }
    Logger::WriteMessage(
      std::format(L"the group's spacing changed by at most {} units over its {} s move\n", worst, static_cast<float>(tick) * SECONDS)
        .c_str());
  }

  // A side's order goes through the host while the skirmish is paused: its snapshots carry the move at once, and the
  // ship flies once it resumes. Every order another side, the observer, or no ship could take is refused and counted.
  TEST_METHOD(CarriesOrdersThroughTheHostWhilePaused)
  {
    const auto skirmish = MakeSkirmish(4);
    NeuronServer::ServerHost host(*skirmish);
    const Client side1 = Join(host, 1);
    const Client side2 = Join(host, 2);
    const Client observer = Join(host, NeuronCore::OBSERVER_SIDE);
    host.Step();
    const Float3 start = EntityOf(side1.LastSnapshot(), SIDE_1_SHIPS[0]).position;
    static_cast<void>(side2.LastSnapshot());
    static_cast<void>(observer.LastSnapshot());

    side1.Send(NeuronCore::Command{NeuronCore::CommandKind::Pause, 0, {}});
    host.Step();
    constexpr float TARGET_X = -1500.0f;
    constexpr float TARGET_Z = 200.0f;
    side1.Send(OrderCommand({GameCore::OrderKind::Move, {SIDE_1_SHIPS[0]}, TARGET_X, TARGET_Z}));
    side2.Send(OrderCommand({GameCore::OrderKind::Move, {SIDE_1_SHIPS[0]}, 0.0f, 0.0f}));
    side1.Send(OrderCommand({GameCore::OrderKind::Move, {SIDE_1_CORE}, 0.0f, 0.0f}));
    observer.Send(OrderCommand({GameCore::OrderKind::Stop, {SIDE_2_SHIPS[0]}, 0.0f, 0.0f}));
    side1.Send(NeuronCore::Command{NeuronCore::CommandKind::Game, 0, {1, 9}});
    host.Step();
    host.Step();
    Assert::AreEqual(std::uint64_t{4}, host.RefusedCommands(), L"the other side's, the core's, the observer's and the malformed");

    const NeuronCore::Snapshot paused = side1.LastSnapshot();
    Assert::IsTrue(paused.paused, L"paused");
    const std::vector<GameCore::ShipOrderState> states = StatesOf(paused);
    Assert::AreEqual(std::size_t{1}, states.size(), L"side 1's ship has its order");
    Assert::IsTrue(states[0].ship == SIDE_1_SHIPS[0] && states[0].state == GameCore::ShipState::Moving &&
                     states[0].destinationX == TARGET_X && states[0].destinationZ == TARGET_Z,
                   L"moving to where it was sent");
    const Float3 still = EntityOf(paused, SIDE_1_SHIPS[0]).position;
    Assert::IsTrue(still.x == start.x && still.z == start.z, L"and still where it was while paused");
    Assert::IsTrue(StatesOf(side2.LastSnapshot()).empty(), L"side 2 learns nothing of side 1's orders");
    Assert::AreEqual(std::size_t{1}, StatesOf(observer.LastSnapshot()).size(), L"the observer learns every side's");

    side1.Send(NeuronCore::Command{NeuronCore::CommandKind::Resume, 0, {}});
    for (std::uint32_t tick = 0; tick < TICK_RATE; ++tick)
    {
      host.Step();
    }
    const NeuronCore::EntityState flying = EntityOf(side1.LastSnapshot(), SIDE_1_SHIPS[0]);
    Assert::IsTrue(PlaneDistance(flying.position, {TARGET_X, 0.0f, TARGET_Z}) < PlaneDistance(start, {TARGET_X, 0.0f, TARGET_Z}),
                   L"once resumed, it flies toward it");
    Assert::IsTrue(NeuronCore::Length(flying.velocity) > 0.0f, L"with a velocity");
  }

  // An order that does not decode, names no ship, or names another side's ship is refused by name; an observer orders
  // nothing; a side's order of its own ships passes.
  TEST_METHOD(RefusesOrdersByName)
  {
    const auto skirmish = MakeSkirmish(2);
    const auto refusal = [&skirmish](const NeuronCore::Command& _command, std::uint8_t _side)
    {
      const auto refused = skirmish->Refuses(_command, _side);
      return refused ? static_cast<int>(*refused) : -1;
    };
    const auto move = [](std::vector<std::uint32_t> _ships)
    { return OrderCommand({GameCore::OrderKind::Move, std::move(_ships), 100.0f, 100.0f}); };
    const int malformed = static_cast<int>(NeuronServer::CommandRefusal::MalformedCommand);
    const int notOrderable = static_cast<int>(NeuronServer::CommandRefusal::NotOrderable);
    const int otherSides = static_cast<int>(NeuronServer::CommandRefusal::OtherSidesEntity);
    Assert::AreEqual(malformed, refusal({NeuronCore::CommandKind::Game, 0, {1, 1, 1}}, 1), L"a payload that does not decode");
    Assert::AreEqual(notOrderable, refusal(move({SIDE_1_CORE}), 1), L"the side's core");
    Assert::AreEqual(notOrderable, refusal(move({FIRST_ASTEROID}), 1), L"an asteroid");
    Assert::AreEqual(notOrderable, refusal(move({SIDE_1_SHIPS[0], 999}), 1), L"an entity the skirmish does not hold");
    Assert::AreEqual(otherSides, refusal(move({SIDE_1_SHIPS[0], SIDE_2_SHIPS[0]}), 1), L"the other side's ship");
    Assert::AreEqual(otherSides, refusal(move({SIDE_1_SHIPS[0]}), NeuronCore::OBSERVER_SIDE), L"the observer's");
    Assert::AreEqual(-1, refusal(move(std::vector<std::uint32_t>(SIDE_2_SHIPS.begin(), SIDE_2_SHIPS.end())), 2), L"its own ships");
  }

  // A stop halts a ship and forgets its move at once; a hold halts it and holds; a move replaces a hold, and a hold a move.
  TEST_METHOD(StopsAndHolds)
  {
    const auto skirmish = MakeSkirmish(5);
    const std::uint32_t ship = SIDE_1_SHIPS[1];
    const auto order = [&skirmish, ship](GameCore::OrderKind _kind)
    { skirmish->ApplyGameCommand(OrderCommand({_kind, {ship}, -600.0f, 0.0f}).payload, 1); };
    const auto stateOf = [&skirmish, ship]() -> std::optional<GameCore::ShipState>
    {
      const std::vector<GameCore::ShipOrderState> states = StatesOf(DescribeSkirmish(*skirmish, 1));
      const auto state = std::ranges::find(states, ship, &GameCore::ShipOrderState::ship);
      return state != states.end() ? std::optional(state->state) : std::nullopt;
    };
    const auto run = [&skirmish](std::uint32_t _ticks)
    {
      for (std::uint32_t tick = 0; tick < _ticks; ++tick)
      {
        skirmish->Advance(0);
      }
    };
    const auto speedOf = [&skirmish, ship] { return NeuronCore::Length(EntityOf(DescribeSkirmish(*skirmish), ship).velocity); };

    order(GameCore::OrderKind::Move);
    Assert::IsTrue(stateOf() == GameCore::ShipState::Moving, L"a move");
    run(2 * TICK_RATE);
    Assert::IsTrue(speedOf() > 0.0f, L"under way");
    order(GameCore::OrderKind::Stop);
    Assert::IsFalse(stateOf().has_value(), L"a stop forgets the move at once");
    run(3 * TICK_RATE);
    Assert::AreEqual(0.0f, speedOf(), L"and halts the ship");
    const Float3 halted = EntityOf(DescribeSkirmish(*skirmish), ship).position;
    run(TICK_RATE);
    const Float3 later = EntityOf(DescribeSkirmish(*skirmish), ship).position;
    Assert::IsTrue(halted.x == later.x && halted.z == later.z, L"where it stays");

    order(GameCore::OrderKind::Hold);
    Assert::IsTrue(stateOf() == GameCore::ShipState::Holding, L"a hold");
    order(GameCore::OrderKind::Move);
    Assert::IsTrue(stateOf() == GameCore::ShipState::Moving, L"a move replaces it");
    run(2 * TICK_RATE);
    order(GameCore::OrderKind::Hold);
    Assert::IsTrue(stateOf() == GameCore::ShipState::Holding, L"and a hold the move");
    run(3 * TICK_RATE);
    Assert::AreEqual(0.0f, speedOf(), L"halted, holding");
  }

  // A logged run of orders replays to the same bytes from its log alone (Design/ADR/ADR-032, R21).
  TEST_METHOD(ReplaysALoggedRunOfOrders)
  {
    constexpr std::uint32_t SEED = 11;
    constexpr std::uint64_t TICKS = std::uint64_t{20} * TICK_RATE;
    std::stringstream log(std::ios::in | std::ios::out | std::ios::binary);
    {
      const auto skirmish = MakeSkirmish(SEED);
      NeuronServer::ServerHost host(*skirmish);
      const Client side1 = Join(host, 1);
      const Client side2 = Join(host, 2);
      host.Log(log, GameLogic::EncodeSkirmishParameters({.seed = SEED}));
      std::uint32_t draw = 0;
      for (std::uint64_t tick = 0; tick < TICKS; ++tick)
      {
        if (tick % (std::uint64_t{4} * TICK_RATE) == 1)
        {
          side1.Send(OrderCommand(RandomOrder(SEED, draw, SIDE_1_SHIPS)));
          side2.Send(OrderCommand(RandomOrder(SEED, draw, SIDE_2_SHIPS)));
        }
        host.Step();
      }
    }
    const std::string text = log.str();
    const auto decoded = NeuronServer::DecodeCommandLog(Bytes(text.begin(), text.end()));
    Assert::IsTrue(decoded.has_value(), L"the log decodes");
    const NeuronServer::CommandLog commandLog = decoded.value_or(NeuronServer::CommandLog{});
    Assert::AreEqual(std::size_t{10}, commandLog.commands.size(), L"every order");
    const auto again = MakeSkirmish(SEED);
    const NeuronServer::ReplayOutcome outcome = NeuronServer::Replay(*again, commandLog);
    Assert::AreEqual(TICKS, outcome.ticks, L"every tick replayed");
    Assert::IsFalse(outcome.firstDifference.has_value(), L"and every tick matched");
  }
};

} // namespace GameLogicTests
