#include "pch.h"

#include "CommandLog.h"

#include "ServerHost.h"

#include "LoopbackTransport.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace NeuronServer
{
namespace
{

constexpr std::uint8_t COMMAND_RECORD = 1;
constexpr std::uint8_t TICK_RECORD = 2;

// The fewest bytes a manifest entry takes: a length, a letter and a hash.
constexpr std::size_t MIN_MANIFEST_ENTRY_BYTES = 1 + 1 + 8;

void PutU8(std::vector<std::uint8_t>& _bytes, std::uint8_t _value)
{
  _bytes.push_back(_value);
}

void PutU16(std::vector<std::uint8_t>& _bytes, std::uint16_t _value)
{
  _bytes.push_back(static_cast<std::uint8_t>(_value));
  _bytes.push_back(static_cast<std::uint8_t>(_value >> 8u));
}

void PutU32(std::vector<std::uint8_t>& _bytes, std::uint32_t _value)
{
  for (std::uint32_t shift = 0; shift < 32u; shift += 8u)
  {
    _bytes.push_back(static_cast<std::uint8_t>(_value >> shift));
  }
}

void PutU64(std::vector<std::uint8_t>& _bytes, std::uint64_t _value)
{
  for (std::uint32_t shift = 0; shift < 64u; shift += 8u)
  {
    _bytes.push_back(static_cast<std::uint8_t>(_value >> shift));
  }
}

// Reads little-endian values in turn. Reading past the end sets it failed, and every read after gives zeros.
class LogReader
{
public:
  explicit LogReader(std::span<const std::uint8_t> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  [[nodiscard]] std::uint64_t Unsigned(std::size_t _bytes) noexcept
  {
    if (m_failed || _bytes > m_bytes.size() - m_offset)
    {
      m_failed = true;
      return 0;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < _bytes; ++i)
    {
      value |= std::uint64_t{m_bytes[m_offset + i]} << (8u * i);
    }
    m_offset += _bytes;
    return value;
  }

  [[nodiscard]] std::uint8_t U8() noexcept
  {
    return static_cast<std::uint8_t>(Unsigned(1));
  }

  [[nodiscard]] std::uint16_t U16() noexcept
  {
    return static_cast<std::uint16_t>(Unsigned(2));
  }

  [[nodiscard]] std::uint32_t U32() noexcept
  {
    return static_cast<std::uint32_t>(Unsigned(4));
  }

  [[nodiscard]] std::uint64_t U64() noexcept
  {
    return Unsigned(8);
  }

  [[nodiscard]] std::span<const std::uint8_t> Bytes(std::size_t _count) noexcept
  {
    if (m_failed || _count > m_bytes.size() - m_offset)
    {
      m_failed = true;
      return {};
    }
    const std::span<const std::uint8_t> bytes = m_bytes.subspan(m_offset, _count);
    m_offset += _count;
    return bytes;
  }

  [[nodiscard]] std::size_t Remaining() const noexcept
  {
    return m_failed ? 0 : m_bytes.size() - m_offset;
  }

  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed;
  }

private:
  std::span<const std::uint8_t> m_bytes;
  std::size_t m_offset = 0;
  bool m_failed = false;
};

[[nodiscard]] bool IsModelName(std::span<const std::uint8_t> _name) noexcept
{
  return !_name.empty() && std::ranges::all_of(_name,
                                               [](std::uint8_t _char) {
                                                 return (_char >= '0' && _char <= '9') || (_char >= 'A' && _char <= 'Z') ||
                                                        (_char >= 'a' && _char <= 'z');
                                               });
}

// Whether _command is one the protocol has: a pause or a resume naming no entity, or a detonate or a restore naming one.
[[nodiscard]] bool IsCommand(std::uint32_t _kind, std::uint32_t _entity) noexcept
{
  switch (static_cast<NeuronCore::CommandKind>(_kind))
  {
  case NeuronCore::CommandKind::Pause:
  case NeuronCore::CommandKind::Resume:
    return _entity == 0;
  case NeuronCore::CommandKind::Detonate:
  case NeuronCore::CommandKind::Restore:
    return _entity != 0;
  }
  return false;
}

[[nodiscard]] bool SameCommand(const LoggedCommand& _a, const LoggedCommand& _b) noexcept
{
  return _a.tick == _b.tick && _a.worldTick == _b.worldTick && _a.session == _b.session && _a.command.kind == _b.command.kind &&
         _a.command.entity == _b.command.entity;
}

} // namespace

const char* LogErrorName(LogError _error) noexcept
{
  switch (_error)
  {
  case LogError::Truncated:
    return "Truncated";
  case LogError::NotALog:
    return "NotALog";
  case LogError::UnsupportedVersion:
    return "UnsupportedVersion";
  case LogError::MalformedLog:
    return "MalformedLog";
  case LogError::BadName:
    return "BadName";
  }
  return "Unknown";
}

std::vector<std::uint8_t> EncodeLogHeader(std::span<const std::uint8_t> _world, std::span<const NeuronCore::ManifestEntry> _manifest,
                                          std::span<const std::uint8_t> _sessionSides)
{
  std::vector<std::uint8_t> bytes(COMMAND_LOG_MAGIC.begin(), COMMAND_LOG_MAGIC.end());
  PutU16(bytes, COMMAND_LOG_VERSION);
  PutU16(bytes, 0); // reserved
  PutU32(bytes, static_cast<std::uint32_t>(_world.size()));
  bytes.insert(bytes.end(), _world.begin(), _world.end());
  PutU32(bytes, static_cast<std::uint32_t>(_manifest.size()));
  for (const NeuronCore::ManifestEntry& entry : _manifest)
  {
    if (entry.name.size() > NeuronCore::MAX_MODEL_NAME_CHARS)
    {
      throw std::invalid_argument("A model name longer than a welcome can carry: " + entry.name);
    }
    PutU8(bytes, static_cast<std::uint8_t>(entry.name.size()));
    bytes.insert(bytes.end(), entry.name.begin(), entry.name.end());
    PutU64(bytes, entry.hash);
  }
  PutU32(bytes, static_cast<std::uint32_t>(_sessionSides.size()));
  bytes.insert(bytes.end(), _sessionSides.begin(), _sessionSides.end());
  return bytes;
}

std::vector<std::uint8_t> EncodeLogRecord(const LoggedCommand& _command)
{
  std::vector<std::uint8_t> bytes;
  PutU8(bytes, COMMAND_RECORD);
  PutU64(bytes, _command.tick);
  PutU64(bytes, _command.worldTick);
  PutU32(bytes, _command.session);
  PutU32(bytes, static_cast<std::uint32_t>(_command.command.kind));
  PutU32(bytes, _command.command.entity);
  return bytes;
}

std::vector<std::uint8_t> EncodeLogRecord(const LoggedTick& _tick)
{
  std::vector<std::uint8_t> bytes;
  PutU8(bytes, TICK_RECORD);
  PutU64(bytes, _tick.tick);
  PutU32(bytes, static_cast<std::uint32_t>(_tick.snapshotHashes.size()));
  for (const std::uint64_t hash : _tick.snapshotHashes)
  {
    PutU64(bytes, hash);
  }
  return bytes;
}

std::expected<CommandLog, LogError> DecodeCommandLog(std::span<const std::uint8_t> _bytes)
{
  LogReader reader(_bytes);
  const std::span<const std::uint8_t> magic = reader.Bytes(COMMAND_LOG_MAGIC.size());
  if (reader.Failed())
  {
    return std::unexpected(LogError::Truncated);
  }
  if (!std::ranges::equal(magic, COMMAND_LOG_MAGIC))
  {
    return std::unexpected(LogError::NotALog);
  }
  const std::uint16_t version = reader.U16();
  const std::uint16_t reserved = reader.U16();
  if (reader.Failed())
  {
    return std::unexpected(LogError::Truncated);
  }
  if (version != COMMAND_LOG_VERSION)
  {
    return std::unexpected(LogError::UnsupportedVersion);
  }
  if (reserved != 0)
  {
    return std::unexpected(LogError::MalformedLog);
  }

  CommandLog log;
  const std::span<const std::uint8_t> world = reader.Bytes(reader.U32());
  log.world.assign(world.begin(), world.end());
  // A count is held to the bytes left before anything is reserved for it.
  const std::uint32_t modelCount = reader.U32();
  if (reader.Failed() || modelCount > reader.Remaining() / MIN_MANIFEST_ENTRY_BYTES)
  {
    return std::unexpected(LogError::Truncated);
  }
  bool badName = false;
  log.manifest.reserve(modelCount);
  for (std::uint32_t i = 0; i < modelCount; ++i)
  {
    const std::span<const std::uint8_t> name = reader.Bytes(reader.U8());
    const std::uint64_t hash = reader.U64();
    badName = badName || !IsModelName(name);
    log.manifest.push_back({std::string(name.begin(), name.end()), hash});
  }
  const std::span<const std::uint8_t> sides = reader.Bytes(reader.U32());
  log.sessionSides.assign(sides.begin(), sides.end());
  if (reader.Failed())
  {
    return std::unexpected(LogError::Truncated);
  }
  if (badName)
  {
    return std::unexpected(LogError::BadName);
  }

  // The records: a command is applied at the tick of the last tick record before it, or 0 before the first, and each tick
  // record follows the one before by one tick.
  std::uint64_t tick = 0;
  while (reader.Remaining() > 0)
  {
    const std::uint8_t kind = reader.U8();
    if (kind == COMMAND_RECORD)
    {
      const std::uint64_t commandTick = reader.U64();
      const std::uint64_t worldTick = reader.U64();
      const std::uint32_t session = reader.U32();
      const std::uint32_t commandKind = reader.U32();
      const std::uint32_t entity = reader.U32();
      if (reader.Failed())
      {
        return std::unexpected(LogError::Truncated);
      }
      if (!IsCommand(commandKind, entity) || session >= log.sessionSides.size() || commandTick != tick || worldTick > commandTick)
      {
        return std::unexpected(LogError::MalformedLog);
      }
      log.commands.push_back({commandTick, worldTick, session, {static_cast<NeuronCore::CommandKind>(commandKind), entity}});
    }
    else if (kind == TICK_RECORD)
    {
      LoggedTick logged{reader.U64(), {}};
      const std::uint32_t count = reader.U32();
      if (reader.Failed() || count > reader.Remaining() / sizeof(std::uint64_t))
      {
        return std::unexpected(LogError::Truncated);
      }
      if (logged.tick != tick + 1 || count == 0)
      {
        return std::unexpected(LogError::MalformedLog);
      }
      logged.snapshotHashes.reserve(count);
      for (std::uint32_t i = 0; i < count; ++i)
      {
        logged.snapshotHashes.push_back(reader.U64());
      }
      tick = logged.tick;
      log.ticks.push_back(std::move(logged));
    }
    else
    {
      return std::unexpected(LogError::MalformedLog);
    }
  }
  return log;
}

ReplayOutcome Replay(World& _world, const CommandLog& _log)
{
  const auto sameModel = [](const NeuronCore::ManifestEntry& _a, const NeuronCore::ManifestEntry& _b)
  { return _a.name == _b.name && _a.hash == _b.hash; };
  if (!std::ranges::equal(_world.Manifest(), _log.manifest, sameModel))
  {
    throw std::invalid_argument("The log was written with models other than the world's.");
  }

  // The replay's own record of the run, which outlives the host that writes it.
  std::stringstream record(std::ios::in | std::ios::out | std::ios::binary);
  ServerHost host(_world);
  std::vector<std::unique_ptr<NeuronCore::Transport>> clients;
  clients.reserve(_log.sessionSides.size());
  for (const std::uint8_t side : _log.sessionSides)
  {
    NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
    host.AddSession(std::move(pair.server), side);
    static_cast<void>(pair.client->Send(NeuronCore::EncodeMessage(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION})));
    clients.push_back(std::move(pair.client));
  }
  host.Log(record, _log.world);

  // Each tick, the commands the host applied at it go out before the step that serves them; what comes back is dropped.
  std::size_t next = 0;
  for (std::size_t tick = 0; tick < _log.ticks.size(); ++tick)
  {
    while (next < _log.commands.size() && _log.commands[next].tick == host.Tick())
    {
      const LoggedCommand& logged = _log.commands[next++];
      if (logged.session < clients.size())
      {
        static_cast<void>(clients[logged.session]->Send(NeuronCore::EncodeMessage(logged.command)));
      }
    }
    host.Step();
    for (const std::unique_ptr<NeuronCore::Transport>& client : clients)
    {
      while (client->Receive())
      {
      }
    }
  }

  const std::string text = record.str();
  const auto replayed = DecodeCommandLog(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
  if (!replayed)
  {
    throw std::logic_error(std::string("The replay's own log was refused: ") + LogErrorName(replayed.error()));
  }

  // The first tick at which the replay parts from the log. Commands the log holds after its last tick, of a step it did
  // not finish, are not replayed and not compared.
  std::optional<std::uint64_t> firstDifference;
  const auto differ = [&firstDifference](std::uint64_t _tick) { firstDifference = std::min(firstDifference.value_or(_tick), _tick); };
  const std::uint64_t lastTick = _log.ticks.empty() ? 0 : _log.ticks.back().tick;
  const auto finished = [lastTick](const std::vector<LoggedCommand>& _commands)
  {
    std::vector<LoggedCommand> kept;
    std::ranges::copy_if(_commands, std::back_inserter(kept),
                         [lastTick](const LoggedCommand& _command) { return _command.tick < lastTick; });
    return kept;
  };
  const std::vector<LoggedCommand> logged = finished(_log.commands);
  const std::vector<LoggedCommand> again = finished(replayed->commands);
  const auto [loggedCommand, replayedCommand] = std::ranges::mismatch(logged, again, SameCommand);
  if (loggedCommand != logged.end())
  {
    differ(loggedCommand->tick);
  }
  if (replayedCommand != again.end())
  {
    differ(replayedCommand->tick);
  }
  for (std::size_t tick = 0; tick < _log.ticks.size(); ++tick)
  {
    if (tick >= replayed->ticks.size() || replayed->ticks[tick].tick != _log.ticks[tick].tick ||
        replayed->ticks[tick].snapshotHashes != _log.ticks[tick].snapshotHashes)
    {
      differ(_log.ticks[tick].tick);
      break;
    }
  }
  return {_log.ticks.size(), next, firstDifference};
}

} // namespace NeuronServer
