#include "pch.h"

#include "SnapshotBuffer.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace NeuronClient
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Quaternion;

// However old, the buffer keeps two snapshots: enough to bracket a time.
constexpr std::size_t MIN_SNAPSHOTS = 2;

[[nodiscard]] bool AreEqual(Quaternion _a, Quaternion _b) noexcept
{
  return _a.x == _b.x && _a.y == _b.y && _a.z == _b.z && _a.w == _b.w;
}

// The cubic Hermite curve through _p0, leaving at _v0, and _p1, arriving at _v1, _seconds later, at _s from 0 to 1. It
// is summed in double and rounded once, and gives the two positions exactly at 0 and 1.
[[nodiscard]] Float3 Hermite(Float3 _p0, Float3 _v0, Float3 _p1, Float3 _v1, double _seconds, double _s) noexcept
{
  const double s2 = _s * _s;
  const double s3 = s2 * _s;
  const double h00 = 2.0 * s3 - 3.0 * s2 + 1.0;
  const double h10 = (s3 - 2.0 * s2 + _s) * _seconds;
  const double h01 = -2.0 * s3 + 3.0 * s2;
  const double h11 = (s3 - s2) * _seconds;
  const auto axis = [&](float _position0, float _velocity0, float _position1, float _velocity1)
  { return static_cast<float>(h00 * _position0 + h10 * _velocity0 + h01 * _position1 + h11 * _velocity1); };
  return {axis(_p0.x, _v0.x, _p1.x, _v1.x), axis(_p0.y, _v0.y, _p1.y, _v1.y), axis(_p0.z, _v0.z, _p1.z, _v1.z)};
}

// Normalized linear interpolation from _q0 to _q1 at _s, along the shorter of the two arcs between them: _q1 and -_q1
// are one rotation, and the one nearer _q0 is taken.
[[nodiscard]] Quaternion Nlerp(Quaternion _q0, Quaternion _q1, double _s) noexcept
{
  const double dot = static_cast<double>(_q0.x) * _q1.x + static_cast<double>(_q0.y) * _q1.y + static_cast<double>(_q0.z) * _q1.z +
                     static_cast<double>(_q0.w) * _q1.w;
  const double to = dot < 0.0 ? -_s : _s;
  const double from = 1.0 - _s;
  const double x = from * _q0.x + to * _q1.x;
  const double y = from * _q0.y + to * _q1.y;
  const double z = from * _q0.z + to * _q1.z;
  const double w = from * _q0.w + to * _q1.w;
  const double length = std::sqrt(x * x + y * y + z * z + w * w);
  return {static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length), static_cast<float>(w / length)};
}

[[nodiscard]] Float3 Lerp(Float3 _a, Float3 _b, double _s) noexcept
{
  const auto axis = [_s](float _from, float _to) { return static_cast<float>((1.0 - _s) * _from + _s * _to); };
  return {axis(_a.x, _b.x), axis(_a.y, _b.y), axis(_a.z, _b.z)};
}

} // namespace

SnapshotBuffer::SnapshotBuffer(std::uint32_t _tickRate) noexcept
  : m_tickRate(_tickRate)
{
}

void SnapshotBuffer::Add(NeuronCore::Snapshot _snapshot, double _arrivalSeconds)
{
  const auto place = std::ranges::lower_bound(m_snapshots, _snapshot.tick, {}, &NeuronCore::Snapshot::tick);
  if (place != m_snapshots.end() && place->tick == _snapshot.tick)
  {
    return;
  }
  const double tickSeconds = static_cast<double>(_snapshot.tick) / m_tickRate;
  std::ranges::sort(_snapshot.entities, {}, &NeuronCore::EntityState::id);
  std::ranges::sort(_snapshot.detonations, {}, &NeuronCore::DetonationEvent::entity);
  m_snapshots.insert(place, std::move(_snapshot));
  while (m_snapshots.size() > MIN_SNAPSHOTS &&
         static_cast<double>(m_snapshots.back().tick - m_snapshots.front().tick) > HISTORY_SECONDS * m_tickRate)
  {
    m_snapshots.pop_front();
  }

  m_arrivals.push_back({_arrivalSeconds, _arrivalSeconds - tickSeconds});
  while (m_arrivals.front().seconds < _arrivalSeconds - HISTORY_SECONDS)
  {
    m_arrivals.pop_front();
  }
}

double SnapshotBuffer::RenderTick(double _clientSeconds) noexcept
{
  if (m_arrivals.empty())
  {
    return m_lastRenderTick.value_or(0.0);
  }
  const double offsetSeconds = std::ranges::min(m_arrivals, {}, &Arrival::offsetSeconds).offsetSeconds;
  double renderTick = (_clientSeconds - offsetSeconds) * m_tickRate - INTERPOLATION_DELAY_TICKS;
  if (m_lastRenderTick)
  {
    renderTick = std::max(renderTick, *m_lastRenderTick);
  }
  m_lastRenderTick = renderTick;
  return renderTick;
}

WorldSample SnapshotBuffer::Sample(double _renderTick) const
{
  WorldSample sample{_renderTick, false, {}};
  if (m_snapshots.empty())
  {
    return sample;
  }

  // The first snapshot at or after the render tick, and the one before it; the nearest one on both sides outside them.
  const auto later = std::ranges::find_if(m_snapshots, [_renderTick](const NeuronCore::Snapshot& _snapshot)
                                          { return static_cast<double>(_snapshot.tick) >= _renderTick; });
  const NeuronCore::Snapshot& after = later == m_snapshots.end() ? m_snapshots.back() : *later;
  const NeuronCore::Snapshot& before = later == m_snapshots.end() || later == m_snapshots.begin() ? after : *std::prev(later);
  const auto ticks = static_cast<double>(after.tick - before.tick);
  const double s = ticks > 0.0 ? std::clamp((_renderTick - static_cast<double>(before.tick)) / ticks, 0.0, 1.0) : 1.0;
  const double seconds = ticks / m_tickRate;
  // The world's clock at the render tick, which stands still while the world is paused.
  const double worldTick =
    static_cast<double>(before.worldTick) + s * (static_cast<double>(after.worldTick) - static_cast<double>(before.worldTick));

  sample.paused = after.paused;
  sample.entities.reserve(after.entities.size());
  auto earlier = before.entities.begin();
  auto detonation = after.detonations.begin();
  for (const NeuronCore::EntityState& entity : after.entities)
  {
    // Each list is in the order of its ids.
    while (earlier != before.entities.end() && earlier->id < entity.id)
    {
      ++earlier;
    }
    while (detonation != after.detonations.end() && detonation->entity < entity.id)
    {
      ++detonation;
    }
    SampledEntity sampled{entity.id, entity.modelIndex, entity.position, entity.rotation, entity.velocity, std::nullopt};
    if (detonation != after.detonations.end() && detonation->entity == entity.id)
    {
      // Debris stands where its entity froze, posed from the event; before the event it is the whole model at rest.
      const double sinceTicks = std::max(worldTick - static_cast<double>(detonation->worldTick), 0.0);
      sampled.detonation = SampledDetonation{*detonation, static_cast<float>(sinceTicks / m_tickRate)};
    }
    else if (&before != &after && earlier != before.entities.end() && earlier->id == entity.id)
    {
      // Through one position twice, still, the curve rounds back to it exactly; a rotation normalized again need not, so
      // one that has not changed is kept as it is.
      sampled.position = Hermite(earlier->position, earlier->velocity, entity.position, entity.velocity, seconds, s);
      if (!AreEqual(earlier->rotation, entity.rotation))
      {
        sampled.rotation = Nlerp(earlier->rotation, entity.rotation, s);
      }
      sampled.velocity = Lerp(earlier->velocity, entity.velocity, s);
    }
    sample.entities.push_back(sampled);
  }
  return sample;
}

} // namespace NeuronClient
