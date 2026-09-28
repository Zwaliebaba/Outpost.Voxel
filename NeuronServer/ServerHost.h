#pragma once

#include "World.h"

#include "Message.h"
#include "Transport.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>

namespace NeuronServer
{

// How many due ticks the host's thread runs when it wakes, at most, before it gives up on the rest: a stall delays
// snapshots, and never turns into a spiral of catching up (Design/SpaceScene.md §6.3).
inline constexpr std::uint32_t MAX_TICKS_PER_WAKE = 4;

// The server: sessions over transports, the handshake, one snapshot a tick to every session, and commands (§6.1,
// Design/ADR/ADR-015). It runs the tick on a thread of its own, or one Step at a time for a caller that owns time, as
// the tests and the bench do. It simulates nothing itself: its World does.
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

  // Takes the server's end of a transport. Its client says Hello and is welcomed before it receives a snapshot. Throws
  // std::logic_error while the host runs on its thread.
  void AddSession(std::unique_ptr<NeuronCore::Transport> _transport);

  // One tick. Reads every session's messages: a Hello of this protocol is welcomed, and a welcomed session's commands
  // are applied; anything else, or a message that does not decode, closes its session. Then the clock advances, and the
  // world with it unless paused, and every welcomed session receives the same snapshot. Closed sessions are dropped.
  void Step();

  // Runs Step on a thread of its own at the world's tick rate, until Stop.
  void Start();

  void Stop() noexcept;

  [[nodiscard]] bool IsRunning() const noexcept;
  [[nodiscard]] std::uint64_t Tick() const noexcept;
  [[nodiscard]] std::uint64_t WorldTick() const noexcept;
  [[nodiscard]] bool IsPaused() const noexcept;
  [[nodiscard]] std::size_t SessionCount() const noexcept;

private:
  struct Session
  {
    std::unique_ptr<NeuronCore::Transport> transport;
    bool welcomed;
  };

  void Serve(Session& _session);
  void Apply(const NeuronCore::Command& _command);
  void Run(const std::stop_token& _stop);

  // When tick _ticks falls, counted from the thread's start: its number times the period, exactly (§6.3).
  [[nodiscard]] std::chrono::nanoseconds TickTime(std::uint64_t _ticks) const noexcept;

  World& m_world;
  std::vector<Session> m_sessions;
  std::uint64_t m_tick = 0;
  std::uint64_t m_worldTick = 0;
  bool m_paused = false;
  std::jthread m_thread;
};

} // namespace NeuronServer
