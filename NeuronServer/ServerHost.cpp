#include "pch.h"

#include "ServerHost.h"

#include "CommandLog.h"

#include "Hash.h"

#include <format>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace NeuronServer
{

ServerHost::ServerHost(World& _world) noexcept
  : m_world(_world)
{
}

ServerHost::~ServerHost()
{
  Stop();
}

void ServerHost::AddSession(std::unique_ptr<NeuronCore::Transport> _transport, std::uint8_t _side)
{
  if (IsRunning())
  {
    throw std::logic_error("A session joins a server host only while the host is stopped.");
  }
  if (m_log != nullptr)
  {
    throw std::logic_error("A session joins a server host only before the host logs, since the log names every session.");
  }
  if (_side > m_world.Sides().size())
  {
    throw std::invalid_argument(std::format("A session plays side {}, and the world has {}.", _side, m_world.Sides().size()));
  }
  m_sessions.push_back({std::move(_transport), _side, static_cast<std::uint32_t>(m_sessionSides.size()), false});
  m_sessionSides.push_back(_side);
}

void ServerHost::Log(std::ostream& _log, std::span<const std::uint8_t> _world)
{
  if (m_tick != 0 || m_log != nullptr)
  {
    throw std::logic_error("A server host logs once, from its first step, so that its log replays from the start.");
  }
  m_log = &_log;
  WriteLog(EncodeLogHeader(_world, m_world.Manifest(), m_sessionSides));
}

void ServerHost::Step()
{
  for (Session& session : m_sessions)
  {
    Serve(session);
  }

  ++m_tick;
  if (!m_paused)
  {
    ++m_worldTick;
    m_world.Advance(m_worldTick);
  }

  // Each side's snapshot is described and encoded at most once a tick: for each side a welcomed session plays, and,
  // while the host logs, for the observer and every side (Design/ADR/ADR-032).
  std::vector<std::vector<std::uint8_t>> snapshots(m_world.Sides().size() + 1);
  const auto snapshot = [this, &snapshots](std::uint8_t _side) -> const std::vector<std::uint8_t>&
  {
    std::vector<std::uint8_t>& bytes = snapshots[_side];
    if (bytes.empty())
    {
      bytes = EncodeSnapshot(_side);
    }
    return bytes;
  };
  for (Session& session : m_sessions)
  {
    if (session.welcomed)
    {
      static_cast<void>(session.transport->Send(snapshot(session.side)));
    }
  }
  if (m_log != nullptr)
  {
    LoggedTick tick{m_tick, {}};
    tick.snapshotHashes.reserve(snapshots.size());
    for (std::size_t side = 0; side < snapshots.size(); ++side)
    {
      tick.snapshotHashes.push_back(NeuronCore::Fnv1aHash64(snapshot(static_cast<std::uint8_t>(side))));
    }
    WriteLog(EncodeLogRecord(tick));
    m_log->flush();
  }
  std::erase_if(m_sessions, [](const Session& _session) { return !_session.transport->IsOpen(); });
}

void ServerHost::Start()
{
  if (IsRunning())
  {
    return;
  }
  m_thread = std::jthread([this](const std::stop_token& _stop) { Run(_stop); });
}

void ServerHost::Stop() noexcept
{
  if (m_thread.joinable())
  {
    m_thread.request_stop();
    m_thread.join();
  }
}

bool ServerHost::IsRunning() const noexcept
{
  return m_thread.joinable();
}

std::uint64_t ServerHost::Tick() const noexcept
{
  return m_tick;
}

std::uint64_t ServerHost::WorldTick() const noexcept
{
  return m_worldTick;
}

bool ServerHost::IsPaused() const noexcept
{
  return m_paused;
}

std::size_t ServerHost::SessionCount() const noexcept
{
  return m_sessions.size();
}

std::uint64_t ServerHost::RefusedCommands() const noexcept
{
  return m_refusedCommands;
}

void ServerHost::Serve(Session& _session)
{
  while (const std::optional<std::vector<std::uint8_t>> bytes = _session.transport->Receive())
  {
    const auto message = NeuronCore::DecodeMessage(*bytes, {m_world.Composites().size(), m_world.Sides().size()});
    if (!message)
    {
      _session.transport->Close();
      return;
    }
    if (const auto* hello = std::get_if<NeuronCore::Hello>(&*message); hello != nullptr)
    {
      // One Hello, of this protocol: a client that speaks another is closed, which is how it learns so.
      if (_session.welcomed || hello->protocolVersion != NeuronCore::PROTOCOL_VERSION)
      {
        _session.transport->Close();
        return;
      }
      const std::span<const NeuronCore::ManifestEntry> manifest = m_world.Manifest();
      const std::span<const NeuronCore::CompositeModel> composites = m_world.Composites();
      const std::span<const NeuronCore::SideColor> sides = m_world.Sides();
      const std::span<const std::uint8_t> payload = m_world.WelcomePayload();
      const NeuronCore::Welcome welcome{NeuronCore::PROTOCOL_VERSION,
                                        m_world.TickRate(),
                                        m_tick,
                                        m_world.Settings(),
                                        std::vector<NeuronCore::ManifestEntry>(manifest.begin(), manifest.end()),
                                        std::vector<NeuronCore::CompositeModel>(composites.begin(), composites.end()),
                                        std::vector<NeuronCore::SideColor>(sides.begin(), sides.end()),
                                        _session.side,
                                        std::vector<std::uint8_t>(payload.begin(), payload.end())};
      static_cast<void>(_session.transport->Send(NeuronCore::EncodeMessage(welcome)));
      _session.welcomed = true;
      continue;
    }
    const auto* command = std::get_if<NeuronCore::Command>(&*message);
    if (command == nullptr || !_session.welcomed)
    {
      _session.transport->Close();
      return;
    }
    Apply(*command, _session);
  }
}

void ServerHost::Apply(const NeuronCore::Command& _command, const Session& _session)
{
  // A command the world refuses for the session's side is counted, and the session goes on (G34).
  if (m_world.Refuses(_command, _session.side).has_value())
  {
    ++m_refusedCommands;
    return;
  }
  switch (_command.kind)
  {
  case NeuronCore::CommandKind::Pause:
    m_paused = true;
    break;
  case NeuronCore::CommandKind::Resume:
    m_paused = false;
    break;
  case NeuronCore::CommandKind::Detonate:
    m_world.Detonate(_command.entity, m_worldTick);
    break;
  case NeuronCore::CommandKind::Restore:
    m_world.Restore(_command.entity);
    break;
  case NeuronCore::CommandKind::Game:
    m_world.ApplyGameCommand(_command.payload, _session.side);
    break;
  }
  if (m_log != nullptr)
  {
    WriteLog(EncodeLogRecord(LoggedCommand{m_tick, m_worldTick, _session.index, _command}));
  }
}

void ServerHost::Run(const std::stop_token& _stop)
{
  // The thread sleeps until the next tick is due, runs every tick that is due, at most a few, and sleeps again. A tick's
  // time is its number times the period, so a late wake-up delays a snapshot but changes nothing in it. Behind by more
  // than the few, it starts counting again from now.
  using Clock = std::chrono::steady_clock;
  Clock::time_point start = Clock::now();
  std::uint64_t ran = 0;
  while (!_stop.stop_requested())
  {
    std::this_thread::sleep_until(start + TickTime(ran + 1));
    std::uint32_t steps = 0;
    while (steps < MAX_TICKS_PER_WAKE && Clock::now() >= start + TickTime(ran + 1) && !_stop.stop_requested())
    {
      Step();
      ++ran;
      ++steps;
    }
    if (Clock::now() >= start + TickTime(ran + 1))
    {
      start = Clock::now() - TickTime(ran);
    }
  }
}

std::vector<std::uint8_t> ServerHost::EncodeSnapshot(std::uint8_t _side) const
{
  // The pause freezes the world, not the clock: the snapshots go on, each the same as the last, and nothing in them
  // moves (§6.3).
  NeuronCore::Snapshot snapshot{m_tick, m_worldTick, m_paused, {}, {}, {}};
  m_world.Describe(snapshot, _side);
  if (m_paused)
  {
    for (NeuronCore::EntityState& entity : snapshot.entities)
    {
      entity.velocity = {0.0f, 0.0f, 0.0f};
    }
  }
  return NeuronCore::EncodeMessage(snapshot);
}

void ServerHost::WriteLog(std::span<const std::uint8_t> _bytes)
{
  m_log->write(reinterpret_cast<const char*>(_bytes.data()), static_cast<std::streamsize>(_bytes.size()));
}

std::chrono::nanoseconds ServerHost::TickTime(std::uint64_t _ticks) const noexcept
{
  constexpr std::uint64_t NANOSECONDS_PER_SECOND = 1'000'000'000;
  return std::chrono::nanoseconds(static_cast<std::int64_t>(_ticks * NANOSECONDS_PER_SECOND / m_world.TickRate()));
}

} // namespace NeuronServer
