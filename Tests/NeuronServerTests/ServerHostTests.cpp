#include "pch.h"

#include "TestSupport.h"

#include "ServerHost.h"

#include "LoopbackTransport.h"
#include "Message.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{
namespace
{

[[nodiscard]] std::vector<std::uint32_t> EntitiesOf(const NeuronCore::Snapshot& _snapshot)
{
  std::vector<std::uint32_t> ids;
  ids.reserve(_snapshot.entities.size());
  for (const NeuronCore::EntityState& entity : _snapshot.entities)
  {
    ids.push_back(entity.id);
  }
  return ids;
}

} // namespace

// Design/Archive/SpaceScene.md §6 and §15: the handshake and a refused version, a snapshot a step with consecutive ticks, the
// same bytes to every session of a side, pause and resume, detonate and restore, and a thread that starts and stops
// cleanly. Design/ADR/ADR-032: each session plays a side, receives what its side sees, and has a command on another side's
// entity refused. No test times the thread.
TEST_CLASS(ServerHostTests)
{
public:
  TEST_METHOD(WelcomesAClientThatSaysHello)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client client = Join(host, 1);
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
    Assert::AreEqual(std::size_t{1}, welcome->composites.size(), L"the world's composite");
    Assert::AreEqual(std::size_t{2}, welcome->sides.size(), L"and its sides");
    Assert::IsTrue(welcome->sessionSide == 1, L"and the side the session plays");
    Assert::IsTrue(welcome->payload == std::vector<std::uint8_t>{1, 2, 3}, L"and its payload, unread");
    const auto* snapshot = std::get_if<NeuronCore::Snapshot>(&messages[1]);
    Assert::IsTrue(snapshot != nullptr && snapshot->tick == 1 && snapshot->entities.size() == 1, L"then the first snapshot");
  }

  // A client of another protocol is closed without a welcome, which is how it learns so; so is one that says anything
  // before Hello, says Hello twice, or sends what does not decode.
  TEST_METHOD(ClosesAClientThatDoesNotSpeakTheProtocol)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client other = Join(host, 1, NeuronCore::PROTOCOL_VERSION + 1);
    NeuronCore::LoopbackPair early = NeuronCore::MakeLoopbackPair();
    host.AddSession(std::move(early.server), 1);
    Assert::IsTrue(early.client->Send(NeuronCore::EncodeMessage(NeuronCore::Command{NeuronCore::CommandKind::Pause, 0})));
    const Client twice = Join(host, 1);
    twice.Send(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION});
    NeuronCore::LoopbackPair garbage = NeuronCore::MakeLoopbackPair();
    host.AddSession(std::move(garbage.server), 1);
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
    const Client client = Join(host, 1);
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

  TEST_METHOD(EverySessionOfASideReceivesTheSameBytes)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client first = Join(host, 1);
    const Client second = Join(host, 1);
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
    const Client client = Join(host, 1);
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
    const Client client = Join(host, 1);
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

  // Each session receives what its side sees, and the observer all of it: side 1 its own ship, side 2 its own, until the
  // world reveals each to the other (ADR-032).
  TEST_METHOD(SendsEachSideWhatItSees)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client first = Join(host, 1);
    const Client second = Join(host, 2);
    const Client observer = Join(host, NeuronCore::OBSERVER_SIDE);
    host.Step();
    world.revealed = true;
    host.Step();

    const std::vector<NeuronCore::Message> secondMessages = second.ReceiveAll();
    const std::vector<NeuronCore::Message> observerMessages = observer.ReceiveAll();
    const auto* secondWelcome = std::get_if<NeuronCore::Welcome>(secondMessages.data());
    const auto* observerWelcome = std::get_if<NeuronCore::Welcome>(observerMessages.data());
    Assert::IsTrue(secondWelcome != nullptr && secondWelcome->sessionSide == 2, L"side 2's session is told its side");
    Assert::IsTrue(observerWelcome != nullptr && observerWelcome->sessionSide == NeuronCore::OBSERVER_SIDE, L"and the observer so");

    const std::vector<NeuronCore::Snapshot> firstSnapshots = SnapshotsOf(first.ReceiveAll());
    const std::vector<NeuronCore::Snapshot> secondSnapshots = SnapshotsOf(secondMessages);
    const std::vector<NeuronCore::Snapshot> observerSnapshots = SnapshotsOf(observerMessages);
    Assert::IsTrue(firstSnapshots.size() == 2 && secondSnapshots.size() == 2 && observerSnapshots.size() == 2, L"a snapshot a step each");
    Assert::IsTrue(EntitiesOf(firstSnapshots[0]) == std::vector<std::uint32_t>{SHIP}, L"side 1 sees its own ship");
    Assert::IsTrue(EntitiesOf(secondSnapshots[0]) == std::vector<std::uint32_t>{ENEMY}, L"side 2 its own");
    Assert::IsTrue(EntitiesOf(observerSnapshots[0]) == std::vector<std::uint32_t>{SHIP, ENEMY}, L"the observer both");
    Assert::IsTrue(EntitiesOf(firstSnapshots[1]) == std::vector<std::uint32_t>{SHIP, ENEMY}, L"revealed, side 1 sees both");
    Assert::IsTrue(EntitiesOf(secondSnapshots[1]) == std::vector<std::uint32_t>{SHIP, ENEMY}, L"and side 2 too");
  }

  // A command on another side's entity is refused, counted and forgotten: the session stays open and the world does not
  // hear of it. The observer's is applied, as its own side's would be (G34, ADR-032).
  TEST_METHOD(RefusesACommandOnAnotherSidesEntity)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client first = Join(host, 1);
    const Client second = Join(host, 2);
    const Client observer = Join(host, NeuronCore::OBSERVER_SIDE);
    host.Step();
    second.Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, SHIP});
    host.Step();
    Assert::AreEqual(std::uint64_t{1}, host.RefusedCommands(), L"side 2 may not detonate side 1's ship");
    Assert::AreEqual(std::size_t{3}, host.SessionCount(), L"and its session stays open");
    Assert::IsTrue(second.transport->IsOpen());
    Assert::IsTrue(SnapshotsOf(first.ReceiveAll()).back().detonations.empty(), L"nothing detonated");

    observer.Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, SHIP});
    host.Step();
    Assert::AreEqual(std::uint64_t{1}, host.RefusedCommands(), L"the observer's command is not refused");
    Assert::AreEqual(std::size_t{1}, SnapshotsOf(first.ReceiveAll()).back().detonations.size(), L"and it detonates the ship");
  }

  // Design/ADR/ADR-033: the game's own commands reach the world with the side they came from, unread by the host, unless
  // the world refuses them, which is counted as any refusal is.
  TEST_METHOD(HandsTheWorldTheGamesCommands)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client first = Join(host, 1);
    const Client second = Join(host, 2);
    const Client observer = Join(host, NeuronCore::OBSERVER_SIDE);
    host.Step();
    first.Send(NeuronCore::Command{NeuronCore::CommandKind::Game, 0, {1, 2, 3}});
    second.Send(NeuronCore::Command{NeuronCore::CommandKind::Game, 0, {0xFF}});
    observer.Send(NeuronCore::Command{NeuronCore::CommandKind::Game, 0, {4}});
    host.Step();
    Assert::IsTrue(world.gameCommands == std::vector<Bytes>{{1, 2, 3}, {4}}, L"the world's commands, byte for byte");
    Assert::IsTrue(world.gameCommandSides == std::vector<std::uint8_t>{1, NeuronCore::OBSERVER_SIDE}, L"with their sides");
    Assert::AreEqual(std::uint64_t{1}, host.RefusedCommands(), L"the one the world refused, counted");
    Assert::AreEqual(std::size_t{3}, host.SessionCount(), L"and nobody closed");
  }

  // A session plays one of the world's sides or observes: side 3 of a world of two is a caller's mistake, and a session
  // that joins once the host logs would be missing from its log.
  TEST_METHOD(TakesASessionOnlyOfASideTheWorldHas)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    host.AddSession(NeuronCore::MakeLoopbackPair().server, 2);
    Assert::IsTrue(Throws<std::invalid_argument>([&host] { host.AddSession(NeuronCore::MakeLoopbackPair().server, 3); }),
                   L"side 3 of a world of two");
    Assert::AreEqual(std::size_t{1}, host.SessionCount(), L"only the session of side 2 joined");

    std::stringstream log;
    host.Log(log, {});
    Assert::IsTrue(Throws<std::logic_error>([&host] { host.AddSession(NeuronCore::MakeLoopbackPair().server, 1); }),
                   L"no session joins once the host logs");
    Assert::IsTrue(Throws<std::logic_error>([&host, &log] { host.Log(log, {}); }), L"and the host logs once");
    host.Step();
    Assert::IsTrue(Throws<std::logic_error>([&host, &log] { host.Log(log, {}); }), L"and the host logs only from the start");
  }

  TEST_METHOD(DropsAClientThatCloses)
  {
    TestWorld world;
    NeuronServer::ServerHost host(world);
    const Client client = Join(host, 1);
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
    const Client client = Join(host, 1);
    host.Start();
    Assert::IsTrue(host.IsRunning());
    bool refused = false;
    try
    {
      host.AddSession(NeuronCore::MakeLoopbackPair().server, 1);
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
