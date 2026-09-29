#pragma once

#include "World.h"

#include "Message.h"
#include "Transport.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

namespace NeuronServer
{

// How many due ticks the host's thread runs when it wakes, at most, before it gives up on the rest: a stall delays
// snapshots, and never turns into a spiral of catching up (Design/Archive/SpaceScene.md §6.3).
inline constexpr std::uint32_t MAX_TICKS_PER_WAKE = 4;

// The server: sessions over transports, each playing a side or observing, the handshake, one snapshot a tick to every
// session of what its side sees, and commands, which the world judges for the side that sent them (§6.1,
// Design/ADR/ADR-015, Design/ADR/ADR-032). It runs the tick on a thread of its own, or one Step at a time for a caller that
// owns time, as the tests and the bench do. It simulates nothing itself: its World does.
//
// While the thread runs, the host belongs to it: call nothing but Stop until Stop returns. The host and its clients
// share nothing but their transports' queues.
class ServerHost
{
public:
  explicit ServerHost(World& _world) noexcept;
  ~ServerHost();
  ServerHost(const ServerHost&) = delete;
  ServerHost& operator=(const ServerHost&) = delete;
  ServerHost(ServerHost&&) = delete;
  ServerHost& operator=(ServerHost&&) = delete;

  // Takes the server's end of a transport, for a session that plays side _side of the world, or observes with
  // NeuronCore::OBSERVER_SIDE. Its client says Hello and is welcomed, told its side, before it receives a snapshot. Throws
  // std::invalid_argument for a side the world does not have, and std::logic_error while the host runs on its thread or
  // once it logs.
  void AddSession(std::unique_ptr<NeuronCore::Transport> _transport, std::uint8_t _side);

  // Records the run in _log (G41, ADR-032): first the header, with _world, the game's description of the world, then
  // every command the host applies and the hash of every side's snapshot at every tick, flushed at the end of each.
  // _log must outlive the host's use of it. Throws std::logic_error once the host logs or has stepped.
  void Log(std::ostream& _log, std::span<const std::uint8_t> _world);

  // One tick. Reads every session's messages: a Hello of this protocol is welcomed, and a welcomed session's commands
  // are applied, unless the world refuses them for the session's side, which is counted; anything else, or a message
  // that does not decode, closes its session. Then the clock advances, and the world with it unless paused, and every
  // welcomed session receives the snapshot of its side. Closed sessions are dropped.
  void Step();

  // Runs Step on a thread of its own at the world's tick rate, until Stop.
  void Start();

  void Stop() noexcept;

  [[nodiscard]] bool IsRunning() const noexcept;
  [[nodiscard]] std::uint64_t Tick() const noexcept;
  [[nodiscard]] std::uint64_t WorldTick() const noexcept;
  [[nodiscard]] bool IsPaused() const noexcept;
  [[nodiscard]] std::size_t SessionCount() const noexcept;

  // The commands the world has refused, from every session (G34).
  [[nodiscard]] std::uint64_t RefusedCommands() const noexcept;

private:
  struct Session
  {
    std::unique_ptr<NeuronCore::Transport> transport;
    std::uint8_t side;
    std::uint32_t index; // in the order the host added them, which the log names it by
    bool welcomed;
  };

  void Serve(Session& _session);
  void Apply(const NeuronCore::Command& _command, const Session& _session);
  void Run(const std::stop_token& _stop);

  // The bytes of this tick's snapshot as side _side sees it.
  [[nodiscard]] std::vector<std::uint8_t> EncodeSnapshot(std::uint8_t _side) const;

  void WriteLog(std::span<const std::uint8_t> _bytes);

  // When tick _ticks falls, counted from the thread's start: its number times the period, exactly (§6.3).
  [[nodiscard]] std::chrono::nanoseconds TickTime(std::uint64_t _ticks) const noexcept;

  World& m_world;
  std::vector<Session> m_sessions;
  std::vector<std::uint8_t> m_sessionSides; // every session's side, by its index, the dropped ones too
  std::uint64_t m_tick = 0;
  std::uint64_t m_worldTick = 0;
  bool m_paused = false;
  std::uint64_t m_refusedCommands = 0;
  std::ostream* m_log = nullptr;
  std::jthread m_thread;
};

} // namespace NeuronServer
