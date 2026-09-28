#include "pch.h"

#include "Route.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <utility>

namespace GameLogic
{
namespace
{

using NeuronCore::Float3;

// Rounds of refinement that make each transit nearly tangent to the orbits at both of its ends.
constexpr std::uint32_t TANGENT_ROUNDS = 8;

// How far outside an obstacle's sphere a transit's waypoint goes, as a multiple of its radius, and how many times a
// transit may bend.
constexpr float WAYPOINT_PUSH = 1.25f;
constexpr std::uint32_t MAX_WAYPOINT_DEPTH = 4;

// How far Track searches behind and ahead of the progress it is given: past a frigate's last slot, 2.4 widths behind its
// leader (106 units), and well short of the shortest lap, an orbit 1.3 keep-outs about a station (2,215 units).
constexpr float TRACK_WINDOW = 200.0f;

constexpr Float3 WORLD_UP{0.0f, 1.0f, 0.0f};
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;

// Two unit vectors square to _normal and to each other, the second the normal crossed with the first, so that angles
// measured from the first toward the second turn counterclockwise about the normal.
void PlaneAxes(Float3 _normal, Float3& _first, Float3& _second) noexcept
{
  const Float3 helper = std::abs(_normal.x) < 0.9f ? Float3{1.0f, 0.0f, 0.0f} : Float3{0.0f, 0.0f, 1.0f};
  _first = NeuronCore::Normalize(helper - _normal * NeuronCore::Dot(helper, _normal));
  _second = NeuronCore::Cross(_normal, _first);
}

// The point of _orbit where its direction of travel runs along _direction, as nearly as its plane allows: the radial
// direction there is the in-plane direction crossed with the normal, reversed for a clockwise orbit.
[[nodiscard]] Float3 TangentPoint(const Orbit& _orbit, Float3 _direction) noexcept
{
  Float3 inPlane = _direction - _orbit.normal * NeuronCore::Dot(_direction, _orbit.normal);
  if (NeuronCore::Length(inPlane) < 1.0e-4f)
  {
    Float3 second{};
    PlaneAxes(_orbit.normal, inPlane, second);
  }
  const Float3 radial = NeuronCore::Cross(NeuronCore::Normalize(inPlane), _orbit.normal) * (_orbit.clockwise ? -1.0f : 1.0f);
  return _orbit.center + radial * _orbit.radius;
}

// The arc of _orbit from _entry, its whole laps, and on around to _exit, where the transit starts; or, for
// _wholeCircle, one lap from _entry back to it.
void AppendArc(const Orbit& _orbit, Float3 _entry, Float3 _exit, bool _wholeCircle, std::vector<RoutePiece>& _pieces)
{
  Float3 first{};
  Float3 second{};
  PlaneAxes(_orbit.normal, first, second);
  const auto angleOf = [&](Float3 _point) noexcept
  {
    const Float3 radial = _point - _orbit.center;
    return std::atan2(NeuronCore::Dot(radial, second), NeuronCore::Dot(radial, first));
  };
  const float direction = _orbit.clockwise ? -1.0f : 1.0f;
  const float startAngle = angleOf(_entry);
  float sweep = TWO_PI;
  if (!_wholeCircle)
  {
    sweep = std::fmod(direction * (angleOf(_exit) - startAngle) + 2.0f * TWO_PI, TWO_PI) + static_cast<float>(_orbit.laps) * TWO_PI;
  }
  RoutePiece arc{};
  arc.center = _orbit.center;
  arc.normal = _orbit.normal;
  arc.first = first;
  arc.radius = _orbit.radius;
  arc.startAngle = startAngle;
  arc.sweep = direction * sweep;
  _pieces.push_back(arc);
}

// The transit from _from to _to, bent around the first of _obstacles it would cross by a waypoint pushed out of it, and
// so on for each part, a few times at most.
void AppendTransit(Float3 _from, Float3 _to, std::span<const Obstacle> _obstacles, std::uint32_t _depth, std::vector<RoutePiece>& _pieces)
{
  const Float3 along = _to - _from;
  const float lengthSquared = NeuronCore::Dot(along, along);
  if (lengthSquared <= 0.0f)
  {
    return;
  }
  for (const Obstacle& obstacle : _obstacles)
  {
    if (_depth >= MAX_WAYPOINT_DEPTH)
    {
      break;
    }
    const float t = std::clamp(NeuronCore::Dot(obstacle.center - _from, along) / lengthSquared, 0.0f, 1.0f);
    const Float3 away = _from + along * t - obstacle.center;
    const float distance = NeuronCore::Length(away);
    if (distance >= obstacle.radius || t <= 0.0f || t >= 1.0f)
    {
      continue;
    }
    Float3 out = distance > 1.0e-3f ? away * (1.0f / distance) : NeuronCore::Cross(along, WORLD_UP);
    if (NeuronCore::Length(out) < 1.0e-3f)
    {
      out = NeuronCore::Cross(along, Float3{1.0f, 0.0f, 0.0f});
    }
    const Float3 waypoint = obstacle.center + NeuronCore::Normalize(out) * (obstacle.radius * WAYPOINT_PUSH);
    AppendTransit(_from, waypoint, _obstacles, _depth + 1, _pieces);
    AppendTransit(waypoint, _to, _obstacles, _depth + 1, _pieces);
    return;
  }
  RoutePiece line{};
  line.start = _from;
  line.end = _to;
  line.normal = WORLD_UP;
  _pieces.push_back(line);
}

// How far into _piece, between _lower and _upper, lies its point nearest _position, and the square of its distance.
[[nodiscard]] float NearestOnPiece(const RoutePiece& _piece, Float3 _position, float _lower, float _upper, float& _distanceSquared) noexcept
{
  std::array<float, 3> candidates{_lower, _upper, _lower};
  if (_piece.radius <= 0.0f)
  {
    const Float3 direction = NeuronCore::Normalize(_piece.end - _piece.start);
    candidates[2] = std::clamp(NeuronCore::Dot(_position - _piece.start, direction), _lower, _upper);
  }
  else
  {
    // The circle's nearest point, at its first repeat a lap apart at or after _lower: the window is shorter than a lap,
    // so no other repeat can lie in it, and when this one does not, an end of the window is nearest.
    const Float3 second = NeuronCore::Cross(_piece.normal, _piece.first);
    const Float3 radial = _position - _piece.center;
    const float angle = std::atan2(NeuronCore::Dot(radial, second), NeuronCore::Dot(radial, _piece.first));
    const float lap = TWO_PI * _piece.radius;
    const float along = (_piece.sweep >= 0.0f ? angle - _piece.startAngle : _piece.startAngle - angle) * _piece.radius;
    candidates[2] = std::min(along + std::ceil((_lower - along) / lap) * lap, _upper);
  }
  float best = _lower;
  _distanceSquared = std::numeric_limits<float>::infinity();
  for (const float candidate : candidates)
  {
    const Float3 offset = PointOnPiece(_piece, candidate) - _position;
    const float distanceSquared = NeuronCore::Dot(offset, offset);
    if (distanceSquared < _distanceSquared)
    {
      _distanceSquared = distanceSquared;
      best = candidate;
    }
  }
  return best;
}

} // namespace

Float3 PointOnPiece(const RoutePiece& _piece, float _along) noexcept
{
  if (_piece.radius <= 0.0f)
  {
    return _piece.length > 0.0f ? _piece.start + (_piece.end - _piece.start) * (_along / _piece.length) : _piece.start;
  }
  const float angle = _piece.startAngle + (_piece.sweep >= 0.0f ? _along : -_along) / _piece.radius;
  const Float3 second = NeuronCore::Cross(_piece.normal, _piece.first);
  return _piece.center + (_piece.first * std::cos(angle) + second * std::sin(angle)) * _piece.radius;
}

Route::Route(std::vector<RoutePiece> _pieces)
  : m_pieces(std::move(_pieces)),
    m_length(0.0f)
{
  for (RoutePiece& piece : m_pieces)
  {
    piece.distance = m_length;
    if (piece.radius > 0.0f)
    {
      piece.length = std::abs(piece.sweep) * piece.radius;
      piece.start = PointOnPiece(piece, 0.0f);
      piece.end = PointOnPiece(piece, piece.length);
    }
    else
    {
      piece.length = NeuronCore::Length(piece.end - piece.start);
    }
    m_length += piece.length;
  }
}

Float3 Route::PositionAt(float _distance) const noexcept
{
  const float distance = Wrap(_distance);
  const RoutePiece& piece = PieceAt(distance);
  return PointOnPiece(piece, distance - piece.distance);
}

Float3 Route::DirectionAt(float _distance) const noexcept
{
  const float distance = Wrap(_distance);
  const RoutePiece& piece = PieceAt(distance);
  if (piece.radius <= 0.0f)
  {
    return NeuronCore::Normalize(piece.end - piece.start);
  }
  const float angle = piece.startAngle + (piece.sweep >= 0.0f ? 1.0f : -1.0f) * (distance - piece.distance) / piece.radius;
  const Float3 second = NeuronCore::Cross(piece.normal, piece.first);
  return (second * std::cos(angle) - piece.first * std::sin(angle)) * (piece.sweep >= 0.0f ? 1.0f : -1.0f);
}

Float3 Route::ReferenceUpAt(float _distance) const noexcept
{
  return PieceAt(Wrap(_distance)).normal;
}

float Route::Track(float _progress, Float3 _position) const noexcept
{
  // The window meets each piece in at most one stretch, counted as the piece lies a length back, where it lies, or a
  // length on, since the window is no longer than the route.
  const float window = std::min(TRACK_WINDOW, 0.5f * m_length);
  const float lower = _progress - window;
  const float upper = _progress + window;
  float best = std::numeric_limits<float>::infinity();
  float bestDistance = _progress;
  for (const RoutePiece& piece : m_pieces)
  {
    for (const float offset : {-m_length, 0.0f, m_length})
    {
      const float start = piece.distance + offset;
      const float from = std::max(lower, start) - start;
      const float to = std::min(upper, start + piece.length) - start;
      if (from > to)
      {
        continue;
      }
      float distanceSquared = 0.0f;
      const float along = NearestOnPiece(piece, _position, from, to, distanceSquared);
      if (distanceSquared < best)
      {
        best = distanceSquared;
        bestDistance = start + along;
      }
    }
  }
  return Wrap(bestDistance);
}

float Route::Separation(float _from, float _to) const noexcept
{
  const float ahead = Wrap(_to - _from);
  return ahead > 0.5f * m_length ? ahead - m_length : ahead;
}

const RoutePiece& Route::PieceAt(float _distance) const noexcept
{
  const auto after = std::ranges::upper_bound(m_pieces, _distance, {}, &RoutePiece::distance);
  return after == m_pieces.begin() ? m_pieces.front() : *(after - 1);
}

float Route::Wrap(float _distance) const noexcept
{
  if (m_length <= 0.0f)
  {
    return 0.0f;
  }
  const float wrapped = std::fmod(_distance, m_length);
  return wrapped < 0.0f ? wrapped + m_length : wrapped;
}

Route BuildRoute(std::span<const Orbit> _orbits, std::span<const Obstacle> _obstacles)
{
  std::vector<RoutePiece> pieces;
  const std::size_t count = _orbits.size();
  if (count == 1)
  {
    const Orbit& orbit = _orbits.front();
    const Float3 start = TangentPoint(orbit, Float3{0.0f, 0.0f, 1.0f});
    AppendArc(orbit, start, start, true, pieces);
    return Route(std::move(pieces));
  }

  // Each transit starts as the line between the two stations' centers, and each round moves its ends to where the two
  // orbits run along it, then aims it from one end to the other; for orbits in one plane that converges on their common
  // tangent, and for tilted ones on a line both are nearly tangent to.
  std::vector<Float3> directions(count);
  std::vector<Float3> exits(count);
  std::vector<Float3> entries(count);
  for (std::size_t i = 0; i < count; ++i)
  {
    directions[i] = NeuronCore::Normalize(_orbits[(i + 1) % count].center - _orbits[i].center);
  }
  for (std::uint32_t round = 0; round <= TANGENT_ROUNDS; ++round)
  {
    for (std::size_t i = 0; i < count; ++i)
    {
      exits[i] = TangentPoint(_orbits[i], directions[i]);
      entries[(i + 1) % count] = TangentPoint(_orbits[(i + 1) % count], directions[i]);
    }
    if (round == TANGENT_ROUNDS)
    {
      break;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
      directions[i] = NeuronCore::Normalize(entries[(i + 1) % count] - exits[i]);
    }
  }
  for (std::size_t i = 0; i < count; ++i)
  {
    AppendArc(_orbits[i], entries[i], exits[i], false, pieces);
    AppendTransit(exits[i], entries[(i + 1) % count], _obstacles, 0, pieces);
  }
  return Route(std::move(pieces));
}

} // namespace GameLogic
