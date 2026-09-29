#include "pch.h"

#include "Skirmish.h"
#include "TestSupport.h"

#include "SkirmishLayout.h"
#include "WelcomeNames.h"

#include "ServerHost.h"

#include "LoopbackTransport.h"
#include "Message.h"
#include "Quaternion.h"
#include "RigidTransform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
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

[[nodiscard]] std::unique_ptr<GameLogic::Skirmish> MakeSkirmish(std::uint32_t _seed)
{
  auto skirmish = GameLogic::Skirmish::Create({.seed = _seed}, GameDataDirectory());
  Assert::IsTrue(
    skirmish.has_value(),
    Widen(skirmish ? std::string() : std::string(GameLogic::SkirmishRefusalName(skirmish.error().refusal)) + ": " + skirmish.error().detail)
      .c_str());
  return skirmish ? std::move(*skirmish) : nullptr;
}

[[nodiscard]] NeuronCore::Snapshot DescribeSkirmish(const GameLogic::Skirmish& _skirmish)
{
  NeuronCore::Snapshot snapshot{};
  _skirmish.Describe(snapshot);
  return snapshot;
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

[[nodiscard]] Client Join(NeuronServer::ServerHost& _host)
{
  NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
  _host.AddSession(std::move(pair.server));
  Client client{std::move(pair.client)};
  client.Send(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION});
  return client;
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

  // Two hosts over two skirmishes of one seed, each with a client, over a detonation and a restore: the same welcome and
  // every snapshot byte for byte. Another seed differs from its welcome on.
  TEST_METHOD(RepeatsItselfFromItsSeed)
  {
    const auto stream = [](std::uint32_t _seed)
    {
      const auto skirmish = MakeSkirmish(_seed);
      NeuronServer::ServerHost host(*skirmish);
      const Client client = Join(host);
      std::vector<Bytes> bytes;
      for (std::uint64_t tick = 0; tick < 12; ++tick)
      {
        if (tick == 3)
        {
          client.Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, 2});
        }
        if (tick == 8)
        {
          client.Send(NeuronCore::Command{NeuronCore::CommandKind::Restore, 2});
        }
        host.Step();
        static_cast<void>(client.ReceiveAll(bytes));
      }
      return bytes;
    };
    const std::vector<Bytes> first = stream(11);
    const std::vector<Bytes> second = stream(11);
    const std::vector<Bytes> other = stream(12);
    Assert::AreEqual(std::size_t{13}, first.size(), L"a welcome and a snapshot a tick");
    Assert::AreEqual(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
      Assert::IsTrue(first[i] == second[i], std::format(L"message {} is the same", i).c_str());
    }
    Assert::IsFalse(first.front() == other.front(), L"another seed, another welcome");
    Assert::IsFalse(first[1] == other[1], L"and other snapshots");
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
