#pragma once

#include "Float3.h"
#include "Message.h"
#include "Quaternion.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace NeuronClient
{

// How far behind the server the client draws, in ticks of the server's clock: three, 100 ms at the default 30 a second
// (Design/Archive/SpaceScene.md §6.4, §17 question 13). A snapshot late by less than this is drawn as if it had come on time.
inline constexpr double INTERPOLATION_DELAY_TICKS = 3.0;

// How far back the buffer looks, in seconds: the clock's offset is the smallest over the arrivals of this long, which
// filters out delivery delay, and a snapshot this much older than the newest is dropped (§6.4).
inline constexpr double HISTORY_SECONDS = 1.0;

// A detonated entity's debris at the render time: its event, and the seconds since it on the world's clock, which stands
// still while the world is paused (Design/ADR/ADR-015).
struct SampledDetonation
{
  NeuronCore::DetonationEvent event;
  float seconds;
};

// An entity at the render time (§6.4).
struct SampledEntity
{
  std::uint32_t id;
  std::uint16_t modelIndex;
  NeuronCore::Float3 position;     // where the middle of its model's box is (§5.1)
  NeuronCore::Quaternion rotation; // of unit length, but not always with w >= 0
  NeuronCore::Float3 velocity;
  std::optional<SampledDetonation> detonation; // empty while it is whole
};

// The world at one render time: every entity present then, in the order of their ids.
struct WorldSample
{
  double renderTick;
  bool paused; // as the later of the two snapshots says
  std::vector<SampledEntity> entities;
};

// The client's recent snapshots, and where every entity is between them (§6.4). Times are ticks of the server's clock,
// tick n falling n / rate seconds after tick 0. The client relates its own clock to the server's by each arrival's offset,
// the arrival less its tick's time, and keeps the smallest over the last second; it draws that far and the delay behind.
//
// Two snapshots bracket a render time. An entity present in both moves along the cubic Hermite curve through their
// positions and velocities and turns by normalized linear interpolation along the shorter arc; one that holds still
// between them holds exactly still, so that a station stays a symmetry of the cube and draws aligned (§7.2). The later
// snapshot says what is present and what has detonated: an entity only in it has appeared, one only in the earlier has
// gone, and debris stands where its entity froze, posed from its event at the render time's world tick. Before the
// oldest snapshot and past the newest, the buffer holds the nearest.
class SnapshotBuffer
{
public:
  explicit SnapshotBuffer(std::uint32_t _tickRate);

  // Takes _snapshot, which arrived at _arrivalSeconds on the client's clock. A tick the buffer already holds is ignored.
  void Add(NeuronCore::Snapshot _snapshot, double _arrivalSeconds);

  [[nodiscard]] bool IsEmpty() const noexcept
  {
    return m_snapshots.empty();
  }

  // The snapshot of the latest tick. The buffer must not be empty.
  [[nodiscard]] const NeuronCore::Snapshot& Newest() const noexcept
  {
    return m_snapshots.back();
  }

  [[nodiscard]] std::uint32_t TickRate() const noexcept
  {
    return m_tickRate;
  }

  // The tick to draw at _clientSeconds on the client's clock: the server's time the offset gives, less the delay. It
  // never runs backward: when the offset grows, it holds still until the server's time has caught up. Before any
  // snapshot has arrived, it is 0.
  [[nodiscard]] double RenderTick(double _clientSeconds) noexcept;

  // Every entity at _renderTick, a tick of the server's clock and a fraction of one.
  [[nodiscard]] WorldSample Sample(double _renderTick) const;

private:
  struct Arrival
  {
    double seconds;       // on the client's clock
    double offsetSeconds; // the arrival less its tick's time
  };

  std::uint32_t m_tickRate;
  std::deque<NeuronCore::Snapshot> m_snapshots; // by tick, and each one's entities and detonations by id
  std::deque<Arrival> m_arrivals;               // in the order they came
  std::optional<double> m_lastRenderTick;
};

} // namespace NeuronClient
