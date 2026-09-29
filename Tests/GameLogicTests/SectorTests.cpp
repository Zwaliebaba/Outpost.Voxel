#include "pch.h"

#include "Route.h"
#include "Sector.h"
#include "ShipMotion.h"
#include "TestSupport.h"

#include "ServerHost.h"

#include "Explosion.h"
#include "Float3.h"
#include "Hash.h"
#include "LoopbackTransport.h"
#include "Message.h"
#include "Quaternion.h"
#include "RigidTransform.h"
#include "Transport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{
namespace
{

using Bytes = std::vector<std::uint8_t>;
using NeuronCore::Float3;

// Ten simulated minutes at the default tick rate (Design/Archive/SpaceScene.md §15).
constexpr std::uint64_t TEN_MINUTES = 18'000;

// How far over its class's limit a tick's motion may measure: the snapshot's floats, not play in the flight. The widest
// measured is 3 parts in 100,000.
constexpr float LIMIT_SLACK = 1.001f;

// How far a wingman strays from its slot at most, and on average, in its ship's widths (Design/ADR/ADR-017, which has
// what the default sector's ten minutes measure, and the worst transient over thirty layouts: 1.81).
constexpr float SLOT_WIDTHS = 2.0f;
constexpr float MEAN_SLOT_WIDTHS = 0.25f;

// The default sector's first flight of frigates: the capital ships' eight flights of one come first.
constexpr std::size_t FIRST_FRIGATE_FLIGHT = 8;

// The first station's id: stations take the first ids.
constexpr std::uint32_t FIRST_STATION = 1;

// A run's measure: the least clearance of any ship from any keep-out, the largest share of each of its class's limits
// any ship used in a tick, and how far wingmen strayed from their slots.
struct FlightMeasure
{
  float clearance = std::numeric_limits<float>::infinity();
  float speed = 0.0f;       // of the class's top speed
  float speedChange = 0.0f; // of its acceleration over a tick
  float turn = 0.0f;        // of its turn rate over a tick
  float rotation = 0.0f;    // of its turn rate and bank rate together over a tick: how far its whole attitude turned
  float slotWidths = 0.0f;  // the furthest a wingman strayed from its slot
  double slotWidthsSum = 0.0;
  std::size_t slotSamples = 0;
};

using Commands = std::function<void(GameLogic::Sector&, std::uint64_t)>;

// The angle a rotation turns through between two unit quaternions, from the sine and cosine of its half together.
[[nodiscard]] float AngleBetween(NeuronCore::Quaternion _from, NeuronCore::Quaternion _to) noexcept
{
  const Float3 from{_from.x, _from.y, _from.z};
  const Float3 to{_to.x, _to.y, _to.z};
  const Float3 half = to * _from.w - from * _to.w - NeuronCore::Cross(from, to);
  const float cosine = _from.w * _to.w + NeuronCore::Dot(from, to);
  return 2.0f * std::atan2(NeuronCore::Length(half), std::abs(cosine));
}

// The angle between two headings, from the sine and cosine together.
[[nodiscard]] float AngleBetween(Float3 _from, Float3 _to) noexcept
{
  return std::atan2(NeuronCore::Length(NeuronCore::Cross(_from, _to)), NeuronCore::Dot(_from, _to));
}

[[nodiscard]] const NeuronCore::EntityState* Find(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id) noexcept
{
  const auto entity = std::ranges::find(_snapshot.entities, _id, &NeuronCore::EntityState::id);
  return entity == _snapshot.entities.end() ? nullptr : &*entity;
}

[[nodiscard]] const NeuronCore::DetonationEvent* FindDetonation(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id) noexcept
{
  const auto event = std::ranges::find(_snapshot.detonations, _id, &NeuronCore::DetonationEvent::entity);
  return event == _snapshot.detonations.end() ? nullptr : &*event;
}

// Whether entity _id is in _snapshot and whole.
[[nodiscard]] bool IsWhole(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id) noexcept
{
  return Find(_snapshot, _id) != nullptr && FindDetonation(_snapshot, _id) == nullptr;
}

// A snapshot's whole entities by id, which a sector numbers from 1: null for an id that is absent or detonated.
[[nodiscard]] std::vector<const NeuronCore::EntityState*> WholeById(const NeuronCore::Snapshot& _snapshot)
{
  std::vector<const NeuronCore::EntityState*> whole;
  for (const NeuronCore::EntityState& entity : _snapshot.entities)
  {
    whole.resize(std::max<std::size_t>(whole.size(), entity.id + std::size_t{1}), nullptr);
    whole[entity.id] = &entity;
  }
  for (const NeuronCore::DetonationEvent& event : _snapshot.detonations)
  {
    whole[event.entity] = nullptr;
  }
  return whole;
}

[[nodiscard]] const NeuronCore::EntityState* WholeShip(const std::vector<const NeuronCore::EntityState*>& _whole,
                                                       std::uint32_t _id) noexcept
{
  return _id < _whole.size() ? _whole[_id] : nullptr;
}

// How far each wingman of _sector's flights is from its slot, in widths of its ship, added into _measure: the slots
// taken in rank order by the whole ships other than the leader, as the sector assigns them.
void MeasureSlots(const GameLogic::Sector& _sector, const std::vector<const NeuronCore::EntityState*>& _whole, FlightMeasure& _measure)
{
  for (std::size_t flight = 0; flight < _sector.FlightCount(); ++flight)
  {
    const std::uint32_t leaderId = _sector.FlightLeader(flight);
    const NeuronCore::EntityState* leader = WholeShip(_whole, leaderId);
    if (leader == nullptr)
    {
      continue;
    }
    const GameLogic::ShipClass& shipClass = _sector.ClassOf(leader->modelIndex);
    const NeuronCore::Rotation frame = NeuronCore::RotationOf(leader->rotation);
    std::size_t slot = 0;
    for (const std::uint32_t id : _sector.FlightMembers(flight))
    {
      const NeuronCore::EntityState* wingman = WholeShip(_whole, id);
      if (id == leaderId || wingman == nullptr)
      {
        continue;
      }
      const Float3 place = leader->position + NeuronCore::RotateVector(frame, GameLogic::FormationSlot(slot, shipClass));
      const float widths = NeuronCore::Length(place - wingman->position) / (2.0f * shipClass.radius);
      _measure.slotWidths = std::max(_measure.slotWidths, widths);
      _measure.slotWidthsSum += widths;
      ++_measure.slotSamples;
      ++slot;
    }
  }
}

// Runs _sector _ticks ticks, applying _commands at each world tick before it advances, as a host does, and measures each
// tick's snapshot: every ship's clearance from every keep-out, and each whole ship's motion since the tick before.
[[nodiscard]] FlightMeasure Fly(GameLogic::Sector& _sector, std::uint64_t _ticks, const Commands& _commands, bool _slots)
{
  const float seconds = 1.0f / static_cast<float>(_sector.TickRate());
  NeuronCore::Snapshot previous = Describe(_sector);
  std::vector<const NeuronCore::EntityState*> wholeBefore = WholeById(previous);
  const std::vector<Float3> stations = StationCenters(previous);
  FlightMeasure measure;
  for (std::uint64_t worldTick = 0; worldTick < _ticks; ++worldTick)
  {
    _commands(_sector, worldTick);
    _sector.Advance(worldTick + 1);
    NeuronCore::Snapshot snapshot = Describe(_sector);
    const std::vector<const NeuronCore::EntityState*> whole = WholeById(snapshot);
    for (const NeuronCore::EntityState& ship : snapshot.entities)
    {
      if (ship.modelIndex == GameLogic::STATION_MODEL)
      {
        continue;
      }
      const float keepOut = _sector.KeepOutRadius(ship.modelIndex);
      for (const Float3& station : stations)
      {
        measure.clearance = std::min(measure.clearance, NeuronCore::Length(ship.position - station) - keepOut);
      }
      const NeuronCore::EntityState* before = WholeShip(wholeBefore, ship.id);
      if (before == nullptr || WholeShip(whole, ship.id) == nullptr)
      {
        continue;
      }
      const GameLogic::ShipClass& shipClass = _sector.ClassOf(ship.modelIndex);
      const float speed = NeuronCore::Length(ship.velocity);
      const float speedBefore = NeuronCore::Length(before->velocity);
      measure.speed = std::max(measure.speed, speed / shipClass.maxSpeed);
      measure.speedChange = std::max(measure.speedChange, std::abs(speed - speedBefore) / (shipClass.acceleration * seconds));
      const float turn = AngleBetween(before->velocity * (1.0f / speedBefore), ship.velocity * (1.0f / speed));
      measure.turn = std::max(measure.turn, turn / (shipClass.turnRate * seconds));
      const float rotation = AngleBetween(before->rotation, ship.rotation);
      measure.rotation = std::max(measure.rotation, rotation / ((shipClass.turnRate + shipClass.bankRate) * seconds));
    }
    if (_slots)
    {
      MeasureSlots(_sector, whole, measure);
    }
    previous = std::move(snapshot);
    wholeBefore = WholeById(previous);
  }
  return measure;
}

void WriteMeasure(const wchar_t* _run, const FlightMeasure& _measure)
{
  std::wstring message =
    std::format(L"{}: {} units clear of every keep-out; of the limits, speed {}, acceleration {}, turn {}, attitude {}", _run,
                _measure.clearance, _measure.speed, _measure.speedChange, _measure.turn, _measure.rotation);
  if (_measure.slotSamples > 0)
  {
    message += std::format(L"; slots at most {} widths away, {} on average", _measure.slotWidths,
                           _measure.slotWidthsSum / static_cast<double>(_measure.slotSamples));
  }
  Logger::WriteMessage((message + L"\n").c_str());
}

// A schedule of detonations and restores that exercises every rule of Design/ADR/ADR-017 over ten minutes, at world
// tick _worldTick. Every 20 s the leaders of every third flight detonate, and 10 s later every ship of those flights is
// restored, the former leaders to rejoin as wingmen; every 30 s the last ship of every third flight from the second
// detonates, and 15 s later it is restored; so capital ships, flights of one, detonate and come back too. A station
// detonates at 30 s, for the ships to fly through its debris, and is restored at 2 min 30 s.
void DetonateAndRestore(GameLogic::Sector& _sector, std::uint64_t _worldTick)
{
  for (std::size_t flight = 0; flight < _sector.FlightCount(); ++flight)
  {
    const std::span<const std::uint32_t> members = _sector.FlightMembers(flight);
    if (flight % 3 == 0 && _worldTick % 600 == 300)
    {
      _sector.Detonate(_sector.FlightLeader(flight), _worldTick);
    }
    if (flight % 3 == 1 && _worldTick % 900 == 450)
    {
      _sector.Detonate(members.back(), _worldTick);
    }
    if ((flight % 3 == 0 && _worldTick % 600 == 0) || (flight % 3 == 1 && _worldTick % 900 == 0))
    {
      for (const std::uint32_t id : members)
      {
        _sector.Restore(id);
      }
    }
  }
  if (_worldTick == 900)
  {
    _sector.Detonate(FIRST_STATION, _worldTick);
  }
  if (_worldTick == 4500)
  {
    _sector.Restore(FIRST_STATION);
  }
}

// The client's end of a loopback to a sector's host: what it has received, decoded against the sector's three models.
struct Client
{
  std::unique_ptr<NeuronCore::Transport> transport;

  void Send(const NeuronCore::Message& _message) const
  {
    Assert::IsTrue(transport->Send(NeuronCore::EncodeMessage(_message)), L"the client sends");
  }

  // Every message waiting, decoded, with the bytes it came in added to _bytes.
  [[nodiscard]] std::vector<NeuronCore::Message> ReceiveAll(std::vector<Bytes>& _bytes) const
  {
    std::vector<NeuronCore::Message> messages;
    while (std::optional<Bytes> bytes = transport->Receive())
    {
      const auto message = NeuronCore::DecodeMessage(*bytes, GameLogic::MODEL_COUNT);
      Assert::IsTrue(message.has_value(), L"the server's message decodes");
      messages.push_back(message.value_or(NeuronCore::Message{}));
      _bytes.push_back(std::move(*bytes));
    }
    return messages;
  }
};

// A client of _host that has said Hello.
[[nodiscard]] Client Join(NeuronServer::ServerHost& _host)
{
  NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
  _host.AddSession(std::move(pair.server));
  Client client{std::move(pair.client)};
  client.Send(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION});
  return client;
}

// The last snapshot among _messages.
[[nodiscard]] NeuronCore::Snapshot LastSnapshot(const std::vector<NeuronCore::Message>& _messages)
{
  const auto last = std::find_if(_messages.rbegin(), _messages.rend(), [](const NeuronCore::Message& _message)
                                 { return std::holds_alternative<NeuronCore::Snapshot>(_message); });
  Assert::IsTrue(last != _messages.rend(), L"a snapshot arrived");
  return last == _messages.rend() ? NeuronCore::Snapshot{} : std::get<NeuronCore::Snapshot>(*last);
}

// The bytes of the file _name in GameData.
[[nodiscard]] Bytes GameDataBytes(const char* _name)
{
  std::ifstream stream(GameDataDirectory() / _name, std::ios::binary);
  Assert::IsTrue(stream.good(), L"the model's file opens");
  return Bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace

// Design/Archive/SpaceScene.md §5 and §15: the default sector, its layout, its repeatability, its flight over ten minutes with and
// without detonations, the rules of Design/ADR/ADR-017 for a flight whose ships detonate and are restored, detonations
// through the host to every client, debris's lifetime, and the refusals.
TEST_CLASS(SectorTests)
{
public:
  // Four stations, eight capital ships and forty frigates, in that order of ids; the capital ships alone and the frigates
  // in flights of two to four; three models, named, and hashed as their files are.
  TEST_METHOD(BuildsTheDefaultSector)
  {
    const auto sector = MakeSector({});
    const NeuronCore::Snapshot snapshot = Describe(*sector);
    Assert::AreEqual(std::size_t{52}, snapshot.entities.size(), L"4 stations, 8 capital ships and 40 frigates");
    for (std::size_t i = 0; i < snapshot.entities.size(); ++i)
    {
      const std::uint16_t model = i < 4 ? GameLogic::STATION_MODEL : i < 12 ? GameLogic::CAPITAL_SHIP_MODEL : GameLogic::FRIGATE_MODEL;
      Assert::AreEqual(static_cast<std::uint32_t>(i + 1), snapshot.entities[i].id, L"ids from 1, in order");
      Assert::IsTrue(model == snapshot.entities[i].modelIndex, L"stations, then capital ships, then frigates");
    }
    Assert::IsTrue(snapshot.detonations.empty(), L"nothing has detonated");

    std::uint32_t nextShip = 5;
    std::uint32_t frigates = 0;
    for (std::size_t flight = 0; flight < sector->FlightCount(); ++flight)
    {
      const std::span<const std::uint32_t> members = sector->FlightMembers(flight);
      const bool capital = flight < FIRST_FRIGATE_FLIGHT;
      Assert::IsTrue(capital ? members.size() == 1 : members.size() >= 2 && members.size() <= 4,
                     L"capital ships alone, frigates two to four");
      Assert::AreEqual(members.front(), sector->FlightLeader(flight), L"a flight's first ship leads it");
      for (const std::uint32_t id : members)
      {
        Assert::AreEqual(nextShip++, id, L"a flight's ships take consecutive ids, in rank order");
      }
      frigates += capital ? 0u : static_cast<std::uint32_t>(members.size());
    }
    Assert::AreEqual(40u, frigates, L"every frigate flies in a flight");

    constexpr std::array<const char*, GameLogic::MODEL_COUNT> NAMES{"MilitaryStation", "CapitalShip", "Frigate"};
    const std::span<const NeuronCore::ManifestEntry> manifest = sector->Manifest();
    Assert::AreEqual(GameLogic::MODEL_COUNT, manifest.size());
    for (std::size_t model = 0; model < GameLogic::MODEL_COUNT; ++model)
    {
      Assert::AreEqual(std::string(NAMES[model]), manifest[model].name);
      const Bytes file = GameDataBytes((std::string(NAMES[model]) + ".vox").c_str());
      Assert::AreEqual(NeuronCore::Fnv1aHash64(file), manifest[model].hash, L"the manifest hashes the model's file");
    }

    // The models' spheres as SpaceScene §4 measures them, and the keep-outs they make.
    Assert::AreEqual(199.11f, sector->ModelRadius(GameLogic::STATION_MODEL), 0.01f);
    Assert::AreEqual(45.81f, sector->ModelRadius(GameLogic::CAPITAL_SHIP_MODEL), 0.01f);
    Assert::AreEqual(22.15f, sector->ModelRadius(GameLogic::FRIGATE_MODEL), 0.01f);
    Assert::AreEqual(294.92f, sector->KeepOutRadius(GameLogic::CAPITAL_SHIP_MODEL), 0.01f);
    Assert::AreEqual(271.26f, sector->KeepOutRadius(GameLogic::FRIGATE_MODEL), 0.01f);

    Assert::AreEqual(30u, sector->TickRate());
    Assert::AreEqual(1u, sector->Settings().skySeed, L"the sky's seed is the sector's");
    Assert::AreEqual(1.0f, NeuronCore::Length(sector->Settings().toSun), 1.0e-6f, L"the sun's direction is a unit vector");
  }

  // Every station stands whole, upright and turned by quarter turns about the vertical, at least the spacing from every
  // other, inside its region; and between them the layouts use all four headings.
  TEST_METHOD(LaysStationsOutSpacedWholeAndUpright)
  {
    std::vector<Float3> headings;
    for (std::uint32_t seed = 1; seed <= 8; ++seed)
    {
      for (const std::uint32_t count : {4u, 8u})
      {
        const GameLogic::SectorParameters parameters{.seed = seed, .stations = count};
        const auto sector = MakeSector(parameters);
        const NeuronCore::Snapshot snapshot = Describe(*sector);
        const std::vector<Float3> stations = StationCenters(snapshot);
        Assert::AreEqual(std::size_t{count}, stations.size());
        const float regionRadius = parameters.stationSpacing * std::sqrt(static_cast<float>(count));
        for (std::size_t i = 0; i < stations.size(); ++i)
        {
          const Float3 center = stations[i];
          const NeuronCore::EntityState& station = snapshot.entities[i];
          Assert::IsTrue(center.x == std::round(center.x) && center.y == std::round(center.y) && center.z == std::round(center.z),
                         L"a station's center is whole");
          Assert::IsTrue(center.x * center.x + center.z * center.z <= regionRadius * regionRadius, L"inside the region's disk");
          Assert::IsTrue(std::abs(center.y) <= 0.25f * parameters.stationSpacing, L"and its height");
          const NeuronCore::Rotation rotation = NeuronCore::RotationOf(station.rotation);
          Assert::IsTrue(rotation.axisY.x == 0.0f && rotation.axisY.y == 1.0f && rotation.axisY.z == 0.0f, L"upright, exactly");
          Assert::IsTrue(NeuronCore::IsCubeSymmetry(rotation), L"turned by quarter turns");
          Assert::IsTrue(station.velocity.x == 0.0f && station.velocity.y == 0.0f && station.velocity.z == 0.0f, L"standing still");
          if (std::ranges::none_of(headings, [&](Float3 _heading) { return NeuronCore::Length(_heading - rotation.axisZ) == 0.0f; }))
          {
            headings.push_back(rotation.axisZ);
          }
          for (std::size_t j = 0; j < i; ++j)
          {
            Assert::IsTrue(NeuronCore::Length(center - stations[j]) >= parameters.stationSpacing, L"at least the spacing apart");
          }
        }
      }
    }
    Assert::AreEqual(std::size_t{4}, headings.size(), L"all four headings");
  }

  // Two hosts over two sectors of one seed, each with a client, over two minutes that detonate and restore: the same
  // welcome and every snapshot byte for byte. Another seed differs from its first message on.
  TEST_METHOD(RepeatsItselfFromItsSeed)
  {
    constexpr std::uint64_t TWO_MINUTES = 3600;
    const auto stream = [](std::uint32_t _seed, std::uint64_t _ticks)
    {
      const auto sector = MakeSector({.seed = _seed});
      NeuronServer::ServerHost host(*sector);
      const Client client = Join(host);
      std::vector<Bytes> bytes;
      for (std::uint64_t tick = 0; tick < _ticks; ++tick)
      {
        if (tick == 300 || tick == 1500)
        {
          client.Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, sector->FlightLeader(FIRST_FRIGATE_FLIGHT)});
        }
        if (tick == 900)
        {
          client.Send(NeuronCore::Command{NeuronCore::CommandKind::Restore, sector->FlightMembers(FIRST_FRIGATE_FLIGHT).front()});
        }
        host.Step();
        static_cast<void>(client.ReceiveAll(bytes));
      }
      return bytes;
    };
    const std::vector<Bytes> first = stream(1, TWO_MINUTES);
    const std::vector<Bytes> second = stream(1, TWO_MINUTES);
    const std::vector<Bytes> other = stream(2, 1);
    Assert::AreEqual(std::size_t{TWO_MINUTES + 1}, first.size(), L"a welcome and a snapshot a tick");
    Assert::AreEqual(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
      Assert::IsTrue(first[i] == second[i], std::format(L"message {} is the same", i).c_str());
    }
    Assert::AreEqual(std::size_t{2}, other.size());
    Assert::IsTrue(first[0] != other[0], L"another seed welcomes to another sky");
    Assert::IsTrue(first[1] != other[1], L"and lays out another sector");
  }

  // Design/Archive/SpaceScene.md §5.3 and §15: over ten minutes no ship enters a keep-out sphere, every tick keeps within its
  // class's speed, acceleration and turn rate, its attitude turns no faster than its turn and roll together, and
  // wingmen hold their slots within a bound.
  TEST_METHOD(FliesTenMinutesWithinItsLimits)
  {
    const auto sector = MakeSector({});
    const FlightMeasure measure = Fly(*sector, TEN_MINUTES, [](GameLogic::Sector&, std::uint64_t) {}, true);
    WriteMeasure(L"ten minutes", measure);
    Assert::IsTrue(measure.clearance >= 0.0f, L"no ship enters a keep-out");
    Assert::IsTrue(measure.speed <= LIMIT_SLACK, L"no ship flies faster than its class's top speed");
    Assert::IsTrue(measure.speedChange <= LIMIT_SLACK, L"nor changes speed faster than its acceleration");
    Assert::IsTrue(measure.turn <= LIMIT_SLACK, L"nor turns faster than its turn rate");
    Assert::IsTrue(measure.rotation <= LIMIT_SLACK, L"nor turns its attitude faster than its turn and roll together");
    Assert::IsTrue(measure.slotWidths <= SLOT_WIDTHS, L"wingmen hold their slots");
    Assert::IsTrue(measure.slotWidthsSum <= MEAN_SLOT_WIDTHS * static_cast<double>(measure.slotSamples), L"closely, on average");
  }

  // The same, through ten minutes of detonations and restores: successions, restored leaders rejoining as wingmen,
  // restored wingmen, and flights left with no ship whole. Rejoining ships come back along their route, never across a
  // station.
  TEST_METHOD(KeepsClearThroughDetonationsAndRestores)
  {
    const auto sector = MakeSector({});
    const FlightMeasure measure = Fly(*sector, TEN_MINUTES, DetonateAndRestore, false);
    WriteMeasure(L"ten minutes of detonations", measure);
    Assert::IsTrue(measure.clearance >= 0.0f, L"no ship enters a keep-out");
    Assert::IsTrue(measure.speed <= LIMIT_SLACK, L"no ship flies faster than its class's top speed");
    Assert::IsTrue(measure.speedChange <= LIMIT_SLACK, L"nor changes speed faster than its acceleration");
    Assert::IsTrue(measure.turn <= LIMIT_SLACK, L"nor turns faster than its turn rate");
    Assert::IsTrue(measure.rotation <= LIMIT_SLACK, L"nor turns its attitude faster than its turn and roll together");
  }

  // The owner's rule (Design/ADR/ADR-017): when a leader detonates, the first whole wingman by rank leads. A ship restored
  // rejoins as a wingman, and leads only a flight with no other ship whole; a flight with none has no leader.
  TEST_METHOD(TheFirstWingmanLeadsWhenItsLeaderDetonates)
  {
    const auto sector = MakeSector({});
    std::size_t flight = FIRST_FRIGATE_FLIGHT;
    while (flight < sector->FlightCount() && sector->FlightMembers(flight).size() < 4)
    {
      ++flight;
    }
    Assert::IsTrue(flight < sector->FlightCount(), L"the default sector has a flight of four");
    const std::vector<std::uint32_t> ships(sector->FlightMembers(flight).begin(), sector->FlightMembers(flight).end());
    std::uint64_t worldTick = 0;
    const auto advance = [&] { sector->Advance(++worldTick); };

    advance();
    Assert::AreEqual(ships[0], sector->FlightLeader(flight), L"its first ship leads");
    sector->Detonate(ships[0], worldTick);
    advance();
    Assert::AreEqual(ships[1], sector->FlightLeader(flight), L"the first wingman takes over");
    sector->Detonate(ships[1], worldTick);
    advance();
    Assert::AreEqual(ships[2], sector->FlightLeader(flight), L"then the next");
    sector->Restore(ships[0]);
    advance();
    Assert::AreEqual(ships[2], sector->FlightLeader(flight), L"a restored leader rejoins as a wingman");
    sector->Detonate(ships[2], worldTick);
    sector->Detonate(ships[3], worldTick);
    advance();
    Assert::AreEqual(ships[0], sector->FlightLeader(flight), L"and leads again when it is the only ship whole");
    sector->Detonate(ships[0], worldTick);
    advance();
    Assert::AreEqual(0u, sector->FlightLeader(flight), L"a flight with no ship whole has no leader");
    sector->Restore(ships[3]);
    advance();
    Assert::AreEqual(ships[3], sector->FlightLeader(flight), L"the first ship restored leads it");
  }

  // The owner's rule (Design/ADR/ADR-017): a detonated ship freezes where it blew up, and restored, it is whole again at
  // that transform with its flight as it was then: its first tick starts from the heading and speed it had.
  TEST_METHOD(ARestoredShipResumesWhereItBlewUp)
  {
    const auto sector = MakeSector({});
    constexpr std::uint64_t DETONATION = 300;
    constexpr std::uint64_t RESTORE = 900;
    for (std::uint64_t worldTick = 1; worldTick <= DETONATION; ++worldTick)
    {
      sector->Advance(worldTick);
    }
    const std::uint32_t capitalShip = sector->FlightLeader(0);
    const std::uint32_t wingman = sector->FlightMembers(FIRST_FRIGATE_FLIGHT)[1];
    const NeuronCore::Snapshot before = Describe(*sector);
    sector->Detonate(capitalShip, DETONATION);
    sector->Detonate(wingman, DETONATION);
    for (std::uint64_t worldTick = DETONATION + 1; worldTick <= RESTORE; ++worldTick)
    {
      sector->Advance(worldTick);
    }
    const NeuronCore::Snapshot frozen = Describe(*sector);
    sector->Restore(capitalShip);
    sector->Restore(wingman);
    sector->Advance(RESTORE + 1);
    const NeuronCore::Snapshot after = Describe(*sector);

    const float seconds = 1.0f / static_cast<float>(sector->TickRate());
    for (const std::uint32_t id : {capitalShip, wingman})
    {
      const NeuronCore::EntityState* blewUp = Find(before, id);
      const NeuronCore::EntityState* still = Find(frozen, id);
      const NeuronCore::DetonationEvent* event = FindDetonation(frozen, id);
      const NeuronCore::EntityState* resumed = Find(after, id);
      Assert::IsTrue(blewUp != nullptr && still != nullptr && event != nullptr && resumed != nullptr, L"the ship is there throughout");
      Assert::IsTrue(still->position.x == blewUp->position.x && still->position.y == blewUp->position.y &&
                       still->position.z == blewUp->position.z,
                     L"frozen where it blew up");
      Assert::IsTrue(still->rotation.x == blewUp->rotation.x && still->rotation.y == blewUp->rotation.y &&
                       still->rotation.z == blewUp->rotation.z && still->rotation.w == blewUp->rotation.w,
                     L"and as it faced");
      Assert::IsTrue(still->velocity.x == 0.0f && still->velocity.y == 0.0f && still->velocity.z == 0.0f, L"and still");
      Assert::AreEqual(DETONATION, event->worldTick, L"the event is the tick it blew up");
      Assert::IsTrue(event->velocity.x == blewUp->velocity.x && event->velocity.y == blewUp->velocity.y &&
                       event->velocity.z == blewUp->velocity.z,
                     L"and carries the velocity it had");

      const GameLogic::ShipClass& shipClass = sector->ClassOf(resumed->modelIndex);
      const float speedBefore = NeuronCore::Length(blewUp->velocity);
      const float speed = NeuronCore::Length(resumed->velocity);
      Assert::IsTrue(IsWhole(after, id), L"restored whole");
      Assert::IsTrue(std::abs(speed - speedBefore) <= shipClass.acceleration * seconds * LIMIT_SLACK, L"from the speed it had");
      Assert::IsTrue(AngleBetween(blewUp->velocity * (1.0f / speedBefore), resumed->velocity * (1.0f / speed)) <=
                       shipClass.turnRate * seconds * LIMIT_SLACK,
                     L"along the heading it had");
      Assert::AreEqual(speed * seconds, NeuronCore::Length(resumed->position - blewUp->position), 1.0e-2f,
                       L"one tick on from where it blew up");
    }
    Assert::AreEqual(capitalShip, sector->FlightLeader(0), L"a capital ship leads its flight of one again");
  }

  // A command from one client detonates an entity for every client, as one event that lasts with the debris, and a
  // client that joins after it receives the same event and so computes the same debris.
  TEST_METHOD(SendsADetonationToEveryClientAsOneEvent)
  {
    const auto sector = MakeSector({});
    NeuronServer::ServerHost host(*sector);
    const Client first = Join(host);
    const Client second = Join(host);
    std::vector<Bytes> firstBytes;
    std::vector<Bytes> secondBytes;
    for (int tick = 0; tick < 10; ++tick)
    {
      host.Step();
    }
    const std::uint32_t ship = sector->FlightLeader(FIRST_FRIGATE_FLIGHT);
    const NeuronCore::Snapshot before = LastSnapshot(first.ReceiveAll(firstBytes));
    static_cast<void>(second.ReceiveAll(secondBytes));
    const NeuronCore::EntityState* blewUp = Find(before, ship);
    Assert::IsTrue(blewUp != nullptr, L"the ship is in the snapshot");

    first.Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, ship});
    host.Step();
    NeuronCore::Snapshot snapshot = LastSnapshot(first.ReceiveAll(firstBytes));
    static_cast<void>(second.ReceiveAll(secondBytes));
    Assert::IsTrue(firstBytes.back() == secondBytes.back(), L"both clients receive the same snapshot");
    Assert::AreEqual(std::size_t{1}, snapshot.detonations.size(), L"one event");
    const NeuronCore::DetonationEvent event = snapshot.detonations.front();
    Assert::AreEqual(ship, event.entity);
    Assert::AreEqual(before.worldTick, event.worldTick, L"at the world tick the command arrived in");
    Assert::IsTrue(event.velocity.x == blewUp->velocity.x && event.velocity.y == blewUp->velocity.y &&
                     event.velocity.z == blewUp->velocity.z,
                   L"with the ship's velocity");

    second.Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, FIRST_STATION});
    for (int tick = 0; tick < 30; ++tick)
    {
      host.Step();
    }
    const Client late = Join(host);
    std::vector<Bytes> lateBytes;
    host.Step();
    snapshot = LastSnapshot(first.ReceiveAll(firstBytes));
    static_cast<void>(second.ReceiveAll(secondBytes));
    const NeuronCore::Snapshot lateSnapshot = LastSnapshot(late.ReceiveAll(lateBytes));
    Assert::IsTrue(lateBytes.back() == firstBytes.back() && secondBytes.back() == firstBytes.back(),
                   L"the late client receives the same snapshot");
    Assert::AreEqual(std::size_t{2}, lateSnapshot.detonations.size(), L"with both events");
    const NeuronCore::DetonationEvent* lateEvent = FindDetonation(lateSnapshot, ship);
    const NeuronCore::DetonationEvent* stationEvent = FindDetonation(lateSnapshot, FIRST_STATION);
    Assert::IsTrue(lateEvent != nullptr && stationEvent != nullptr, L"the ship's and the station's");
    Assert::IsTrue(lateEvent->seed == event.seed && lateEvent->worldTick == event.worldTick, L"the ship's as it was sent");
    Assert::IsTrue(stationEvent->seed != event.seed, L"each detonation draws a seed of its own");
    Assert::IsTrue(stationEvent->velocity.x == 0.0f && stationEvent->velocity.y == 0.0f && stationEvent->velocity.z == 0.0f,
                   L"a station detonates standing still");

    // The debris each client poses from its event, at the snapshot's world time, is the same.
    const float age = static_cast<float>(lateSnapshot.worldTick - event.worldTick) / static_cast<float>(sector->TickRate());
    NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters({0.0f, 0.0f, 0.0f});
    parameters.seed = event.seed;
    parameters.inheritedVelocity = event.velocity;
    NeuronCore::ExplosionParameters lateParameters = parameters;
    lateParameters.seed = lateEvent->seed;
    lateParameters.inheritedVelocity = lateEvent->velocity;
    for (const std::uint32_t voxel : {0u, 17u, 1180u})
    {
      const Float3 rest{static_cast<float>(voxel % 13), static_cast<float>(voxel % 7), static_cast<float>(voxel % 5)};
      const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(voxel, rest, parameters, age);
      const NeuronCore::VoxelPose latePose = NeuronCore::ExplosionPose(voxel, rest, lateParameters, age);
      Assert::IsTrue(pose.center.x == latePose.center.x && pose.center.y == latePose.center.y && pose.center.z == latePose.center.z,
                     L"the late client computes the same debris");
    }
  }

  // Design/Archive/SpaceScene.md §5.5: with a lifetime, a detonated entity and its event leave the snapshot on the tick the
  // lifetime runs out, for good; its id is not reused, and its flight goes on without it.
  TEST_METHOD(DebrisLeavesOnTheTickItsLifetimeRunsOut)
  {
    const auto sector = MakeSector({.debrisLifetimeSeconds = 2.0f});
    constexpr std::uint64_t DETONATION = 100;
    constexpr std::uint64_t LIFETIME_TICKS = 60;
    for (std::uint64_t worldTick = 1; worldTick <= DETONATION; ++worldTick)
    {
      sector->Advance(worldTick);
    }
    const std::uint32_t ship = sector->FlightLeader(FIRST_FRIGATE_FLIGHT);
    sector->Detonate(FIRST_STATION, DETONATION);
    sector->Detonate(ship, DETONATION);
    for (std::uint64_t worldTick = DETONATION + 1; worldTick < DETONATION + LIFETIME_TICKS; ++worldTick)
    {
      sector->Advance(worldTick);
      const NeuronCore::Snapshot snapshot = Describe(*sector);
      Assert::IsTrue(Find(snapshot, FIRST_STATION) != nullptr && FindDetonation(snapshot, FIRST_STATION) != nullptr,
                     L"the station's debris lasts");
      Assert::IsTrue(Find(snapshot, ship) != nullptr && FindDetonation(snapshot, ship) != nullptr, L"and the ship's");
    }
    sector->Advance(DETONATION + LIFETIME_TICKS);
    NeuronCore::Snapshot snapshot = Describe(*sector);
    Assert::IsTrue(Find(snapshot, FIRST_STATION) == nullptr && Find(snapshot, ship) == nullptr, L"then both leave");
    Assert::IsTrue(snapshot.detonations.empty(), L"with their events");
    Assert::AreEqual(std::size_t{50}, snapshot.entities.size(), L"and nothing else does");
    Assert::AreNotEqual(ship, sector->FlightLeader(FIRST_FRIGATE_FLIGHT), L"its flight goes on without it");

    sector->Restore(FIRST_STATION);
    sector->Advance(DETONATION + LIFETIME_TICKS + 1);
    snapshot = Describe(*sector);
    Assert::IsTrue(Find(snapshot, FIRST_STATION) == nullptr, L"a removed entity is gone for good");
  }

  TEST_METHOD(DebrisStaysWithoutALifetime)
  {
    const auto sector = MakeSector({});
    sector->Advance(1);
    sector->Detonate(FIRST_STATION, 1);
    for (std::uint64_t worldTick = 2; worldTick <= 3000; ++worldTick)
    {
      sector->Advance(worldTick);
    }
    const NeuronCore::Snapshot snapshot = Describe(*sector);
    const NeuronCore::DetonationEvent* event = FindDetonation(snapshot, FIRST_STATION);
    Assert::IsTrue(Find(snapshot, FIRST_STATION) != nullptr && event != nullptr, L"the debris is there 100 s on");
    Assert::AreEqual(std::uint64_t{1}, event->worldTick, L"from the tick it detonated");
  }

  // A parameter block is refused by name before anything runs, and so is a model folder without the models. A sector
  // of nothing is no refusal.
  TEST_METHOD(RefusesByName)
  {
    struct Case
    {
      GameLogic::SectorParameters parameters;
      GameLogic::SectorRefusal refusal;
      const char* name;
    };
    const std::array cases{
      Case{{.tickRate = 0}, GameLogic::SectorRefusal::BadParameter, "BadParameter"},
      Case{{.stationSpacing = 0.0f}, GameLogic::SectorRefusal::BadParameter, "BadParameter"},
      Case{{.stationSpacing = std::numeric_limits<float>::quiet_NaN()}, GameLogic::SectorRefusal::BadParameter, "BadParameter"},
      Case{{.debrisLifetimeSeconds = -1.0f}, GameLogic::SectorRefusal::BadParameter, "BadParameter"},
      Case{{.stations = 0}, GameLogic::SectorRefusal::ShipsWithoutStations, "ShipsWithoutStations"},
      Case{{.stationSpacing = 500.0f}, GameLogic::SectorRefusal::SpacingTooSmall, "SpacingTooSmall"},
      Case{{.stations = 300}, GameLogic::SectorRefusal::DoesNotFit, "DoesNotFit"},
    };
    for (const Case& refused : cases)
    {
      const auto sector = GameLogic::Sector::Create(refused.parameters, GameDataDirectory());
      Assert::IsFalse(sector.has_value(), std::wstring(refused.name, refused.name + std::char_traits<char>::length(refused.name)).c_str());
      const GameLogic::SectorRefusal refusal = sector ? GameLogic::SectorRefusal::BadParameter : sector.error().refusal;
      Assert::AreEqual(std::string(refused.name), std::string(GameLogic::SectorRefusalName(refusal)));
    }

    const auto missing = GameLogic::Sector::Create({}, GameDataDirectory() / "NoSuchFolder");
    Assert::IsFalse(missing.has_value(), L"a folder without the models");
    Assert::AreEqual(std::string("ModelNotLoaded"),
                     std::string(GameLogic::SectorRefusalName(missing ? GameLogic::SectorRefusal::BadParameter : missing.error().refusal)));
    Assert::AreEqual(std::string("MilitaryStation.vox: FileNotFound"), missing ? std::string() : missing.error().detail,
                     L"names the model and why");

    const auto empty = MakeSector({.stations = 0, .frigates = 0, .capitalShips = 0});
    Assert::IsTrue(Describe(*empty).entities.empty(), L"a sector of nothing holds nothing");
    Assert::AreEqual(std::size_t{0}, empty->FlightCount());
  }
};

} // namespace GameLogicTests
