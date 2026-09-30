#include "pch.h"

#include "TestSupport.h"

#include "CommandLog.h"
#include "ServerHost.h"

#include "Hash.h"
#include "Message.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{
namespace
{

// The world's description a test log carries, which the engine never reads.
constexpr std::array<std::uint8_t, 3> WORLD_DESCRIPTION{4, 5, 6};

constexpr std::uint64_t RUN_TICKS = 10;

// A run of the test world, logged, with a session of side 1, one of side 2 and an observer, in that order: side 1 pauses
// the world at tick 2 and resumes it at tick 4, side 2's detonation of side 1's ship at tick 5 is refused, and the
// observer detonates the ship at tick 6 and restores it at tick 8. Side 1 hands the world a game command at tick 3, and
// side 2 one at tick 7 that the world refuses (Design/ADR/ADR-033). A command's tick is the host's when it applies it,
// before the step that serves it.
struct LoggedRun
{
  Bytes log;
  std::vector<std::vector<Bytes>> received; // by session: its welcome, then a snapshot a tick
  std::uint64_t refused;
};

[[nodiscard]] LoggedRun RunAndLog()
{
  TestWorld world;
  NeuronServer::ServerHost host(world);
  std::vector<Client> clients;
  clients.push_back(Join(host, 1));
  clients.push_back(Join(host, 2));
  clients.push_back(Join(host, NeuronCore::OBSERVER_SIDE));
  std::stringstream log(std::ios::in | std::ios::out | std::ios::binary);
  host.Log(log, WORLD_DESCRIPTION);
  for (std::uint64_t tick = 0; tick < RUN_TICKS; ++tick)
  {
    switch (tick)
    {
    case 2:
      clients[0].Send(NeuronCore::Command{NeuronCore::CommandKind::Pause, 0});
      break;
    case 3:
      clients[0].Send(NeuronCore::Command{NeuronCore::CommandKind::Game, 0, {7, 7}});
      break;
    case 4:
      clients[0].Send(NeuronCore::Command{NeuronCore::CommandKind::Resume, 0});
      break;
    case 5:
      clients[1].Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, SHIP});
      break;
    case 6:
      clients[2].Send(NeuronCore::Command{NeuronCore::CommandKind::Detonate, SHIP});
      break;
    case 7:
      clients[1].Send(NeuronCore::Command{NeuronCore::CommandKind::Game, 0, {0xFF}});
      break;
    case 8:
      clients[2].Send(NeuronCore::Command{NeuronCore::CommandKind::Restore, SHIP});
      break;
    default:
      break;
    }
    host.Step();
  }

  LoggedRun run{{}, std::vector<std::vector<Bytes>>(clients.size()), host.RefusedCommands()};
  const std::string text = log.str();
  run.log.assign(text.begin(), text.end());
  for (std::size_t session = 0; session < clients.size(); ++session)
  {
    static_cast<void>(clients[session].ReceiveAll(&run.received[session]));
  }
  return run;
}

[[nodiscard]] NeuronServer::CommandLog Decoded(const Bytes& _bytes)
{
  auto log = NeuronServer::DecodeCommandLog(_bytes);
  Assert::IsTrue(log.has_value(), L"the log decodes");
  return std::move(log).value_or(NeuronServer::CommandLog{});
}

// The bytes of the given parts, one after another.
[[nodiscard]] Bytes Joined(std::initializer_list<Bytes> _parts)
{
  Bytes bytes;
  for (const Bytes& part : _parts)
  {
    bytes.insert(bytes.end(), part.begin(), part.end());
  }
  return bytes;
}

// A header of two sessions, one of side 1 and an observer, and a manifest of one model.
[[nodiscard]] Bytes Header(const std::string& _modelName = "Frigate")
{
  constexpr std::array<std::uint8_t, 2> SESSION_SIDES{1, NeuronCore::OBSERVER_SIDE};
  const std::vector<NeuronCore::ManifestEntry> manifest{{_modelName, 0x0102030405060708u}};
  return NeuronServer::EncodeLogHeader(WORLD_DESCRIPTION, manifest, SESSION_SIDES);
}

[[nodiscard]] Bytes TickRecord(std::uint64_t _tick, std::vector<std::uint64_t> _snapshotHashes = {0x99u})
{
  return NeuronServer::EncodeLogRecord(NeuronServer::LoggedTick{_tick, std::move(_snapshotHashes)});
}

[[nodiscard]] Bytes CommandRecord(std::uint64_t _tick, std::uint64_t _worldTick, std::uint32_t _session, std::uint32_t _kind,
                                  std::uint32_t _entity, Bytes _payload = {})
{
  return NeuronServer::EncodeLogRecord(
    NeuronServer::LoggedCommand{_tick, _worldTick, _session, {static_cast<NeuronCore::CommandKind>(_kind), _entity, std::move(_payload)}});
}

// _bytes without their last byte.
[[nodiscard]] Bytes CutShort(Bytes _bytes)
{
  _bytes.pop_back();
  return _bytes;
}

// The name of the refusal of _bytes, or "none" for a log that decodes.
[[nodiscard]] std::string RefusalOf(const Bytes& _bytes)
{
  const auto log = NeuronServer::DecodeCommandLog(_bytes);
  return log ? std::string("none") : std::string(NeuronServer::LogErrorName(log.error()));
}

} // namespace

// Design/ADR/ADR-032: the log holds every command the host applied and every tick's snapshot hashes, is spelled as the ADR
// spells it, is refused by name when it is not a log, and replays to the same bytes, or says at which tick it parts.
TEST_CLASS(CommandLogTests)
{
public:
  TEST_METHOD(RecordsEveryAppliedCommandAndEveryTick)
  {
    const LoggedRun run = RunAndLog();
    const NeuronServer::CommandLog log = Decoded(run.log);
    Assert::IsTrue(log.world == Bytes(WORLD_DESCRIPTION.begin(), WORLD_DESCRIPTION.end()), L"the world's description, unread");
    Assert::AreEqual(std::size_t{1}, log.manifest.size(), L"the world's manifest");
    Assert::IsTrue(log.manifest.front().name == "Frigate" && log.manifest.front().hash == 0x1234u);
    Assert::IsTrue(log.sessionSides == Bytes{1, 2, NeuronCore::OBSERVER_SIDE}, L"every session's side, in order");

    // The refused commands are counted and not logged; the others are, with the world tick they were applied at and the
    // game's payload.
    Assert::AreEqual(std::uint64_t{2}, run.refused);
    const std::array<NeuronServer::LoggedCommand, 5> expected{{{2, 2, 0, {NeuronCore::CommandKind::Pause, 0}},
                                                               {3, 2, 0, {NeuronCore::CommandKind::Game, 0, {7, 7}}},
                                                               {4, 2, 0, {NeuronCore::CommandKind::Resume, 0}},
                                                               {6, 4, 2, {NeuronCore::CommandKind::Detonate, SHIP}},
                                                               {8, 6, 2, {NeuronCore::CommandKind::Restore, SHIP}}}};
    Assert::AreEqual(expected.size(), log.commands.size(), L"the applied commands");
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
      const NeuronServer::LoggedCommand& command = log.commands[i];
      const std::wstring what = std::format(L"command {}", i);
      Assert::AreEqual(expected[i].tick, command.tick, what.c_str());
      Assert::AreEqual(expected[i].worldTick, command.worldTick, what.c_str());
      Assert::AreEqual(expected[i].session, command.session, what.c_str());
      Assert::IsTrue(expected[i].command.kind == command.command.kind, what.c_str());
      Assert::AreEqual(expected[i].command.entity, command.command.entity, what.c_str());
      Assert::IsTrue(expected[i].command.payload == command.command.payload, what.c_str());
    }

    // Every tick, the hash of the observer's snapshot and then each side's, which are the bytes their sessions received.
    Assert::AreEqual(std::size_t{RUN_TICKS}, log.ticks.size(), L"every tick");
    constexpr std::array<std::size_t, 3> SESSION_OF_SIDE{2, 0, 1};
    for (std::size_t i = 0; i < log.ticks.size(); ++i)
    {
      const NeuronServer::LoggedTick& tick = log.ticks[i];
      const std::wstring what = std::format(L"tick {}", i + 1);
      Assert::AreEqual(std::uint64_t{i + 1}, tick.tick, what.c_str());
      Assert::AreEqual(std::size_t{3}, tick.snapshotHashes.size(), what.c_str());
      for (std::size_t side = 0; side < SESSION_OF_SIDE.size(); ++side)
      {
        const Bytes& sent = run.received[SESSION_OF_SIDE[side]][i + 1];
        Assert::AreEqual(NeuronCore::Fnv1aHash64(sent), tick.snapshotHashes[side], std::format(L"{}, side {}", what, side).c_str());
      }
    }
    Assert::AreNotEqual(log.ticks.front().snapshotHashes[1], log.ticks.front().snapshotHashes[2], L"the two sides see apart");
  }

  TEST_METHOD(SpellsTheLogAsTheAdrDoes)
  {
    const Bytes header{'O', 'V', 'C', 'L', 2, 0,   0,   0,                       // magic, version 2, reserved
                       3,   0,   0,   0,   4, 5,   6,                            // the world's description
                       1,   0,   0,   0,   7, 'F', 'r', 'i', 'g', 'a', 't', 'e', // a manifest of one model, by name
                       8,   7,   6,   5,   4, 3,   2,   1,                       // and hash
                       2,   0,   0,   0,   1, 0};                                // two sessions: side 1 and an observer
    Assert::IsTrue(Header() == header, L"the header");

    const Bytes command{1,                      // a command
                        3, 0, 0, 0, 0, 0, 0, 0, // its tick
                        2, 0, 0, 0, 0, 0, 0, 0, // its world tick
                        1, 0, 0, 0,             // its session
                        3, 0, 0, 0,             // a detonation
                        7, 0, 0, 0,             // of entity 7
                        0, 0, 0, 0};            // and no payload
    Assert::IsTrue(CommandRecord(3, 2, 1, 3, 7) == command, L"a command");

    const Bytes gameCommand{1,                      // a command
                            3, 0, 0, 0, 0, 0, 0, 0, // its tick
                            2, 0, 0, 0, 0, 0, 0, 0, // its world tick
                            1, 0, 0, 0,             // its session
                            5, 0, 0, 0,             // the game's
                            0, 0, 0, 0,             // naming no entity
                            2, 0, 0, 0, 9, 8};      // and its payload of two bytes (Design/ADR/ADR-033)
    Assert::IsTrue(CommandRecord(3, 2, 1, 5, 0, {9, 8}) == gameCommand, L"the game's command");

    const Bytes tick{2,                                              // a tick
                     4,    0,    0,    0,    0,    0,    0,    0,    // its number
                     2,    0,    0,    0,                            // two snapshots
                     0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // the observer's hash
                     0x99, 0,    0,    0,    0,    0,    0,    0};   // and side 1's
    Assert::IsTrue(TickRecord(4, {0x1122334455667788u, 0x99u}) == tick, L"a tick");

    Assert::IsTrue(Throws<std::invalid_argument>([] { static_cast<void>(Header(std::string(NeuronCore::MAX_MODEL_NAME_CHARS + 1, 'A'))); }),
                   L"a name longer than a welcome carries");
  }

  TEST_METHOD(DecodesAHeaderAndItsRecords)
  {
    const NeuronServer::CommandLog log = Decoded(Joined({Header(), TickRecord(1), CommandRecord(1, 1, 1, 1, 0), TickRecord(2)}));
    Assert::IsTrue(log.world == Bytes{4, 5, 6});
    Assert::IsTrue(log.manifest.size() == 1 && log.manifest.front().name == "Frigate" && log.manifest.front().hash == 0x0102030405060708u);
    Assert::IsTrue(log.sessionSides == Bytes{1, NeuronCore::OBSERVER_SIDE});
    Assert::AreEqual(std::size_t{1}, log.commands.size());
    Assert::IsTrue(log.commands.front().session == 1 && log.commands.front().command.kind == NeuronCore::CommandKind::Pause);
    Assert::AreEqual(std::size_t{2}, log.ticks.size());
    Assert::IsTrue(log.ticks.back().tick == 2 && log.ticks.back().snapshotHashes == std::vector<std::uint64_t>{0x99u});

    const NeuronServer::CommandLog empty = Decoded(Header());
    Assert::IsTrue(empty.commands.empty() && empty.ticks.empty(), L"a header alone is a log of no ticks");
  }

  // Each refusal by name, in the order the reader meets them.
  TEST_METHOD(RefusesWhatIsNotALogByName)
  {
    const Bytes valid = Joined({Header(), TickRecord(1), CommandRecord(1, 1, 0, 3, 7), TickRecord(2)});
    Assert::AreEqual(std::string("none"), RefusalOf(valid), L"the log the others corrupt");

    const auto with = [&valid](std::size_t _offset, std::uint8_t _value)
    {
      Bytes bytes = valid;
      bytes[_offset] = _value;
      return bytes;
    };
    Bytes manyModels = valid;
    for (std::size_t i = 15; i < 19; ++i)
    {
      manyModels[i] = 0xFF;
    }

    struct Case
    {
      const wchar_t* what;
      Bytes bytes;
      const char* expected;
    };
    const std::vector<Case> cases{
      {L"nothing", {}, "Truncated"},
      {L"a magic cut short", {'O', 'V', 'C'}, "Truncated"},
      {L"another magic", with(3, 'X'), "NotALog"},
      {L"another version", with(4, 1), "UnsupportedVersion"},
      {L"a reserved field set", with(6, 1), "MalformedLog"},
      {L"a header cut inside the manifest", Bytes(valid.begin(), valid.begin() + 30), "Truncated"},
      {L"more models than bytes", manyModels, "Truncated"},
      {L"a name of a space", Header("Fri gate"), "BadName"},
      {L"an empty name", Header(""), "BadName"},
      {L"a record of no kind", Joined({Header(), {3}}), "MalformedLog"},
      {L"a record cut short", Bytes(valid.begin(), valid.end() - 1), "Truncated"},
      {L"a tick of no hashes", Joined({Header(), TickRecord(1, {})}), "MalformedLog"},
      {L"a first tick of 0", Joined({Header(), TickRecord(0)}), "MalformedLog"},
      {L"a tick skipped", Joined({Header(), TickRecord(1), TickRecord(3)}), "MalformedLog"},
      {L"a command before its tick", Joined({Header(), CommandRecord(1, 0, 0, 1, 0)}), "MalformedLog"},
      {L"a command after its tick", Joined({Header(), TickRecord(1), TickRecord(2), CommandRecord(1, 1, 0, 1, 0)}), "MalformedLog"},
      {L"a world tick past the tick", Joined({Header(), TickRecord(1), CommandRecord(1, 2, 0, 1, 0)}), "MalformedLog"},
      {L"a session the log does not have", Joined({Header(), TickRecord(1), CommandRecord(1, 1, 2, 1, 0)}), "MalformedLog"},
      {L"a command of no kind", Joined({Header(), TickRecord(1), CommandRecord(1, 1, 0, 0, 0)}), "MalformedLog"},
      {L"a command of a kind past the last", Joined({Header(), TickRecord(1), CommandRecord(1, 1, 0, 6, 7)}), "MalformedLog"},
      {L"a pause that names an entity", Joined({Header(), TickRecord(1), CommandRecord(1, 1, 0, 1, 7)}), "MalformedLog"},
      {L"a detonation that names none", Joined({Header(), TickRecord(1), CommandRecord(1, 1, 0, 3, 0)}), "MalformedLog"},
      {L"a detonation with a payload", Joined({Header(), TickRecord(1), CommandRecord(1, 1, 0, 3, 7, {1})}), "MalformedLog"},
      {L"the game's command without a payload", Joined({Header(), TickRecord(1), CommandRecord(1, 1, 0, 5, 0)}), "MalformedLog"},
      {L"the game's command naming an entity", Joined({Header(), TickRecord(1), CommandRecord(1, 1, 0, 5, 7, {1})}), "MalformedLog"},
      {L"a payload cut short", CutShort(Joined({Header(), TickRecord(1), CommandRecord(1, 1, 0, 5, 0, {1, 2, 3})})), "Truncated"}};
    for (const Case& refused : cases)
    {
      Assert::AreEqual(std::string(refused.expected), RefusalOf(refused.bytes), refused.what);
    }

    // A tick that claims more hashes than the bytes left is cut short, whatever it claims.
    Bytes manyHashes = Joined({Header(), TickRecord(1)});
    manyHashes[Header().size() + 9 + 3] = 0x7F;
    Assert::AreEqual(std::string("Truncated"), RefusalOf(manyHashes), L"more hashes than bytes");
  }

  TEST_METHOD(ReplaysARunToTheSameBytes)
  {
    const LoggedRun run = RunAndLog();
    TestWorld fresh;
    const NeuronServer::ReplayOutcome outcome = NeuronServer::Replay(fresh, Decoded(run.log));
    Assert::AreEqual(RUN_TICKS, outcome.ticks, L"every tick replayed");
    Assert::AreEqual(std::size_t{5}, outcome.commands, L"every logged command sent again");
    Assert::IsFalse(outcome.firstDifference.has_value(), L"and every tick matched");
    Assert::IsTrue(outcome.meanStepMilliseconds >= 0.0 && outcome.worstStepMilliseconds >= outcome.meanStepMilliseconds,
                   L"and each step was timed");

    // A command the log holds after its last tick, of a step it did not finish, is neither sent nor compared.
    Bytes unfinished = run.log;
    const Bytes trailing = CommandRecord(RUN_TICKS, 8, 0, 1, 0);
    unfinished.insert(unfinished.end(), trailing.begin(), trailing.end());
    TestWorld again;
    const NeuronServer::ReplayOutcome cut = NeuronServer::Replay(again, Decoded(unfinished));
    Assert::AreEqual(std::size_t{5}, cut.commands, L"the unfinished step's command is not sent");
    Assert::IsFalse(cut.firstDifference.has_value(), L"and not compared");
  }

  TEST_METHOD(FindsTheTickAReplayPartsAt)
  {
    const LoggedRun run = RunAndLog();

    TestWorld moved(5.0f);
    Assert::IsTrue(NeuronServer::Replay(moved, Decoded(run.log)).firstDifference == 1u, L"a world made otherwise parts at once");

    NeuronServer::CommandLog missing = Decoded(run.log);
    missing.commands.erase(missing.commands.begin() + 3);
    TestWorld withoutDetonation;
    Assert::IsTrue(NeuronServer::Replay(withoutDetonation, missing).firstDifference == 7u,
                   L"a log without the detonation parts from the snapshot after it");

    NeuronServer::CommandLog refused = Decoded(run.log);
    refused.commands[3].session = 1;
    TestWorld fromSide2;
    Assert::IsTrue(NeuronServer::Replay(fromSide2, refused).firstDifference == 6u,
                   L"a detonation sent from side 2 is refused, and the commands part at its tick");

    NeuronServer::CommandLog tampered = Decoded(run.log);
    tampered.ticks[2].snapshotHashes[1] ^= 1u;
    TestWorld hashed;
    Assert::IsTrue(NeuronServer::Replay(hashed, tampered).firstDifference == 3u, L"a hash that differs parts at its tick");
  }

  // A replay runs on the world the log describes: another model, or a side the world does not have, is refused before it
  // runs.
  TEST_METHOD(ReplaysOnlyOnTheWorldTheLogDescribes)
  {
    const LoggedRun run = RunAndLog();
    NeuronServer::CommandLog otherModel = Decoded(run.log);
    otherModel.manifest.front().hash ^= 1u;
    TestWorld first;
    Assert::IsTrue(Throws<std::invalid_argument>([&first, &otherModel] { static_cast<void>(NeuronServer::Replay(first, otherModel)); }),
                   L"a model of another hash");

    NeuronServer::CommandLog otherSide = Decoded(run.log);
    otherSide.sessionSides.push_back(3);
    TestWorld second;
    Assert::IsTrue(Throws<std::invalid_argument>([&second, &otherSide] { static_cast<void>(NeuronServer::Replay(second, otherSide)); }),
                   L"a session of side 3 of a world of two");
  }
};

} // namespace NeuronServerTests
