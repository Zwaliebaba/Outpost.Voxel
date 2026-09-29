#pragma once

#include "World.h"

#include "Message.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace NeuronServer
{

// A skirmish's record, from which it replays (G41, R21, Design/ADR/ADR-032): the world it was made from, every command the
// host applied, and the hash of every side's snapshot at every tick. Little-endian: a header, then records, each opening
// with a byte of its kind.
inline constexpr std::array<std::uint8_t, 4> COMMAND_LOG_MAGIC{'O', 'V', 'C', 'L'};
inline constexpr std::uint16_t COMMAND_LOG_VERSION = 1;

// A command the host applied: the tick and the world tick it applied it at, and the session that sent it, by the order the
// host added them in.
struct LoggedCommand
{
  std::uint64_t tick;
  std::uint64_t worldTick;
  std::uint32_t session;
  NeuronCore::Command command;
};

// A tick's snapshots: the FNV-1a hash of the bytes of each, the observer's and then each side's in turn.
struct LoggedTick
{
  std::uint64_t tick;
  std::vector<std::uint64_t> snapshotHashes;
};

struct CommandLog
{
  std::vector<std::uint8_t> world; // the game's description of the world, which the engine carries without reading
  std::vector<NeuronCore::ManifestEntry> manifest;
  std::vector<std::uint8_t> sessionSides; // each session's side, in the order the host added them
  std::vector<LoggedCommand> commands;    // in the order the host applied them
  std::vector<LoggedTick> ticks;          // one for every tick, in order
};

// Why a log was refused.
enum class LogError : std::uint8_t
{
  Truncated,          // bytes that end inside the header or a record
  NotALog,            // no COMMAND_LOG_MAGIC
  UnsupportedVersion, // another version
  MalformedLog,       // a reserved field that is not zero, a record of no known kind, a command of no known kind, or ticks out of order
  BadName             // a model name that is empty, or not letters and digits
};

[[nodiscard]] const char* LogErrorName(LogError _error) noexcept;

// The header's bytes: the world's description, the manifest the world's welcome names, and the sessions' sides. Throws
// std::invalid_argument for a model name longer than a welcome can carry.
[[nodiscard]] std::vector<std::uint8_t> EncodeLogHeader(std::span<const std::uint8_t> _world,
                                                        std::span<const NeuronCore::ManifestEntry> _manifest,
                                                        std::span<const std::uint8_t> _sessionSides);

// The bytes of one record.
[[nodiscard]] std::vector<std::uint8_t> EncodeLogRecord(const LoggedCommand& _command);
[[nodiscard]] std::vector<std::uint8_t> EncodeLogRecord(const LoggedTick& _tick);

// The log _bytes hold: a header, then whole records and nothing more.
[[nodiscard]] std::expected<CommandLog, LogError> DecodeCommandLog(std::span<const std::uint8_t> _bytes);

// What a replay found.
struct ReplayOutcome
{
  std::uint64_t ticks;  // replayed, as many as the log holds
  std::size_t commands; // the log's, sent again
  std::optional<std::uint64_t>
    firstDifference; // the first tick whose commands or snapshots differ from the log's; none when every one matched
};

// Replays _log on _world, which must be made afresh from what _log's header describes. A host with one loopback client
// for each logged session steps through the log's ticks: each client says Hello, then sends its session's commands before
// the tick they were applied at, and the host's own record of the run is compared with _log's, command by command and
// tick by tick. Throws std::invalid_argument for a world whose models, by name and hash, are not the log's, or a session
// of a side _world does not have.
[[nodiscard]] ReplayOutcome Replay(World& _world, const CommandLog& _log);

} // namespace NeuronServer
