#include "pch.h"

#include "ServerHost.h"
#include "World.h"

#include "LoopbackTransport.h"
#include "Message.h"
#include "Transport.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{
namespace
{

using Bytes = std::vector<std::uint8_t>;

// The one entity the test world holds, and the one it does not.
constexpr std::uint32_t SHIP = 7;
constexpr std::uint32_t NO_SUCH_ENTITY = 8;

// A world of one ship that moves one unit along x each tick, and remembers what the host asked of it.
class TestWorld final : public NeuronServer::World
{
public:
  [[nodiscard]] std::uint32_t TickRate() const noexcept override
  {
    return 30;
  }

  [[nodiscard]] const NeuronCore::WorldSettings& Settings() const noexcept override
  {
    return m_settings;
  }

  [[nodiscard]] std::span<const NeuronCore::ManifestEntry> Manifest() const noexcept override
  {
    return m_manifest;
  }

  void Advance(std::uint64_t _worldTick) override
  {
    advanced.push_back(_worldTick);
    if (!m_detonation)
    {
      m_position.x += 1.0f;
    }
  }

  void Detonate(std::uint32_t _entity, std::uint64_t _worldTick) override
  {
    if (_entity == SHIP && !m_detonation)
    {
      m_detonation = NeuronCore::DetonationEvent{SHIP, 99, _worldTick, {30.0f, 0.0f, 0.0f}};
    }
  }

  void Restore(std::uint32_t _entity) override
  {
    if (_entity == SHIP)
    {
      m_detonation.reset();
    }
  }

  void Describe(NeuronCore::Snapshot& _snapshot) const override
  {
    _snapshot.entities.push_back({SHIP, 0, m_position, {0.0f, 0.0f, 0.0f, 1.0f}, {30.0f, 0.0f, 0.0f}});
    if (m_detonation)
    {
      _snapshot.detonations.push_back(*m_detonation);
    }
  }

  // The world ticks Advance was called with, in order.
  std::vector<std::uint64_t> advanced;

private:
  NeuronCore::WorldSettings m_settings{
    {0.0f, 1.0f, 0.0f}, {0.7f, 0.7f, 0.7f}, 0.0047f, {0.05f, 0.05f, 0.05f}, {0.05f, 0.05f, 0.05f}, 1, {0.0f, 0.0f, 0.0f, 1.0f}};
  std::vector<NeuronCore::ManifestEntry> m_manifest{{"Frigate", 0x1234u}};
  NeuronCore::Float3 m_position{0.0f, 0.0f, 0.0f};
  std::optional<NeuronCore::DetonationEvent> m_detonation;
};

// The client's end of a loopback: what it has received, decoded against the test world's one model.
struct Client
{
  std::unique_ptr<NeuronCore::Transport> transport;

  void Send(const NeuronCore::Message& _message) const
  {
    Assert::IsTrue(transport->Send(NeuronCore::EncodeMessage(_message)), L"the client sends");
  }

  // Every message waiting, decoded, and the bytes they came in.
  [[nodiscard]] std::vector<NeuronCore::Message> ReceiveAll(std::vector<Bytes>* _bytes = nullptr) const
  {
    std::vector<NeuronCore::Message> messages;
    while (std::optional<Bytes> bytes = transport->Receive())
    {
      const auto message = NeuronCore::DecodeMessage(*bytes, 1);
      Assert::IsTrue(message.has_value(), L"the server's message decodes");
      messages.push_back(message.value_or(NeuronCore::Message{}));
      if (_bytes != nullptr)
      {
        _bytes->push_back(std::move(*bytes));
      }
    }
    return messages;
  }
};

// A client of _host that has said Hello, in _protocolVersion.
[[nodiscard]] Client Join(NeuronServer::ServerHost& _host, std::uint32_t _protocolVersion = NeuronCore::PROTOCOL_VERSION)
{
  NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
  _host.AddSession(std::move(pair.server));
  Client client{std::move(pair.client)};
  client.Send(NeuronCore::Hello{_protocolVersion});
  return client;
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

} // namespace

// Design/SpaceScene.md §6 and §15: the handshake and a refused version, a snapshot a step with consecutive ticks, the
// same bytes to every client, pause and resume, detonate and restore, and a thread that starts and stops cleanly. No
// test times the thread.
TEST_CLASS(ServerHostTests)
{
public:
  TEST_METHOD(WelcomesAClientThatSaysHello)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client client = Join(host);
    host.Step();
    const std::vector<NeuronCore::Message> messages = client.ReceiveAll();
    Assert::AreEqual(std::size_t{2}, messages.size(), L"a welcome and the tick's snapshot");
    const auto* welcome = std::get_if<NeuronCore::Welcome>(messages.data());
    Assert::IsTrue(welcome != nullptr, L"the welcome comes first");
    Assert::AreEqual(NeuronCore::PROTOCOL_VERSION, welcome->protocolVersion);
    Assert::AreEqual(30u, welcome->tickRate);
    Assert::AreEqual(std::uint64_t{0}, welcome->tick, L"the tick before the first snapshot");
    Assert::AreEqual(std::size_t{1}, welcome->manifest.size());
    Assert::AreEqual(std::string("Frigate"), welcome->manifest.front().name);
    const auto* snapshot = std::get_if<NeuronCore::Snapshot>(&messages[1]);
    Assert::IsTrue(snapshot != nullptr && snapshot->tick == 1 && snapshot->entities.size() == 1, L"then the first snapshot");
  }

  // A client of another protocol is closed without a welcome, which is how it learns so; so is one that says anything
  // before Hello, says Hello twice, or sends what does not decode.
  TEST_METHOD(ClosesAClientThatDoesNotSpeakTheProtocol)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client other = Join(host, NeuronCore::PROTOCOL_VERSION + 1);
    NeuronCore::LoopbackPair early = NeuronCore::MakeLoopbackPair();
    host.AddSession(std::move(early.server));
    Assert::IsTrue(early.client->Send(NeuronCore::EncodeMessage(NeuronCore::Command{NeuronCore::CommandKind::Pause, 0})));
    const Client twice = Join(host);
    twice.Send(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION});
    NeuronCore::LoopbackPair garbage = NeuronCore::MakeLoopbackPair();
    host.AddSession(std::move(garbage.server));
    Assert::IsTrue(garbage.client->Send(Bytes{1, 2, 3}));
    host.Step();

    Assert::IsFalse(other.transport->IsOpen(), L"another protocol");
    Assert::IsTrue(other.ReceiveAll().empty(), L"and no welcome");
    Assert::IsFalse(early.client->IsOpen(), L"a command before Hello");
    Assert::IsFalse(twice.transport->IsOpen(), L"a second Hello");
    Assert::IsFalse(garbage.client->IsOpen(), L"bytes that are not a message");
    Assert::AreEqual(std::size_t{0}, host.SessionCount(), L"every one of them dropped");
    Assert::IsTrue(world.advanced == std::vector<std::uint64_t>{1}, L"and the world went on regardless");
  }

  TEST_METHOD(SendsASnapshotEveryStep)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client client = Join(host);
    for (std::uint32_t i = 0; i < 50; ++i)
    {
      host.Step();
    }
    const std::vector<NeuronCore::Snapshot> snapshots = SnapshotsOf(client.ReceiveAll());
    Assert::AreEqual(std::size_t{50}, snapshots.size());
    for (std::size_t i = 0; i < snapshots.size(); ++i)
    {
      Assert::AreEqual(std::uint64_t{i + 1}, snapshots[i].tick, std::format(L"snapshot {}", i).c_str());
      Assert::AreEqual(std::uint64_t{i + 1}, snapshots[i].worldTick, std::format(L"snapshot {}", i).c_str());
      Assert::AreEqual(static_cast<float>(i + 1), snapshots[i].entities.front().position.x, std::format(L"snapshot {}", i).c_str());
    }
  }

  TEST_METHOD(EveryClientReceivesTheSameBytes)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client first = Join(host);
    const Client second = Join(host);
    for (std::uint32_t i = 0; i < 10; ++i)
    {
      host.Step();
    }
    std::vector<Bytes> firstBytes;
    std::vector<Bytes> secondBytes;
    static_cast<void>(first.ReceiveAll(&firstBytes));
    static_cast<void>(second.ReceiveAll(&secondBytes));
    Assert::AreEqual(std::size_t{11}, firstBytes.size(), L"a welcome and ten snapshots");
    Assert::IsTrue(firstBytes == secondBytes, L"byte for byte");
  }

  // The pause freezes the world, not the clock: the ticks go on, the world tick and every position stand still, and
  // nothing moves (§6.3). Resume moves it again.
  TEST_METHOD(PauseFreezesTheWorldNotTheClock)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client client = Join(host);
    host.Step();
    client.Send(NeuronCore::Command{NeuronCore::CommandKind::Pause, 0});
    for (std::uint32_t i = 0; i < 5; ++i)
    {
      host.Step();
    }
    Assert::IsTrue(host.IsPaused());
    client.Send(NeuronCore::Command{NeuronCore::CommandKind::Resume, 0});
    host.Step();

    const std::vector<NeuronCore::Snapshot> snapshots = SnapshotsOf(client.ReceiveAll());
    Assert::AreEqual(std::size_t{7}, snapshots.size());
    for (std::size_t i = 1; i <= 5; ++i)
    {
      const NeuronCore::Snapshot& paused = snapshots[i];
      const std::wstring what = std::format(L"paused snapshot {}", i);
      Assert::IsTrue(paused.paused, what.c_str());
      Assert::AreEqual(std::uint64_t{i + 1}, paused.tick, what.c_str());
      Assert::AreEqual(std::uint64_t{1}, paused.worldTick, what.c_str());
      Assert::AreEqual(1.0f, paused.entities.front().position.x, what.c_str());
      Assert::AreEqual(0.0f, paused.entities.front().velocity.x, what.c_str());
    }
    Assert::IsFalse(snapshots.back().paused, L"resumed");
    Assert::AreEqual(std::uint64_t{7}, snapshots.back().tick);
    Assert::AreEqual(std::uint64_t{2}, snapshots.back().worldTick);
    Assert::AreEqual(30.0f, snapshots.back().entities.front().velocity.x);
    Assert::IsTrue(world.advanced == std::vector<std::uint64_t>{1, 2}, L"the world advanced only while it ran");
  }

  TEST_METHOD(DetonateAndRestoreReachTheWorld)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client client = Join(host);
    host.Step();
    host.Step();
    client.Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, NO_SUCH_ENTITY});
    client.Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, SHIP});
    host.Step();
    client.Send(NeuronCore::Command{NeuronCore::CommandKind::Restore, SHIP});
    host.Step();

    const std::vector<NeuronCore::Snapshot> snapshots = SnapshotsOf(client.ReceiveAll());
    Assert::AreEqual(std::size_t{4}, snapshots.size());
    Assert::IsTrue(snapshots[1].detonations.empty(), L"nothing before the command");
    Assert::AreEqual(std::size_t{1}, snapshots[2].detonations.size(), L"one event, for the one entity there is");
    Assert::AreEqual(SHIP, snapshots[2].detonations.front().entity);
    Assert::AreEqual(std::uint64_t{2}, snapshots[2].detonations.front().worldTick, L"the world tick the command arrived at");
    Assert::IsTrue(snapshots[3].detonations.empty(), L"restored");
  }

  TEST_METHOD(DropsAClientThatCloses)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client client = Join(host);
    host.Step();
    Assert::AreEqual(std::size_t{1}, host.SessionCount());
    client.transport->Close();
    host.Step();
    Assert::AreEqual(std::size_t{0}, host.SessionCount());
  }

  // The thread runs the ticks, the host takes no session while it runs, and it stops when asked. Waiting for the
  // snapshots is bounded only so that a broken thread fails the test rather than hanging it; nothing is timed.
  TEST_METHOD(RunsOnItsOwnThreadAndStopsCleanly)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client client = Join(host);
    host.Start();
    Assert::IsTrue(host.IsRunning());
    bool refused = false;
    try
    {
      host.AddSession(NeuronCore::MakeLoopbackPair().server);
    }
    catch (const std::logic_error&)
    {
      refused = true;
    }
    std::vector<NeuronCore::Snapshot> snapshots;
    for (std::uint32_t wait = 0; wait < 60000 && snapshots.size() < 3; ++wait)
    {
      const std::vector<NeuronCore::Snapshot> received = SnapshotsOf(client.ReceiveAll());
      snapshots.insert(snapshots.end(), received.begin(), received.end());
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    host.Stop();
    Assert::IsTrue(refused, L"no session joins while the thread runs");
    Assert::IsFalse(host.IsRunning(), L"stopped");
    Assert::IsTrue(snapshots.size() >= 3, L"the thread sent snapshots");
    for (std::size_t i = 1; i < snapshots.size(); ++i)
    {
      Assert::AreEqual(snapshots[i - 1].tick + 1, snapshots[i].tick, L"with consecutive ticks");
    }

    // Stopped, the host is the caller's again, and its clock carries on from where the thread left it.
    const std::uint64_t tick = host.Tick();
    host.Step();
    Assert::AreEqual(tick + 1, host.Tick());
  }
};

} // namespace NeuronServerTests
