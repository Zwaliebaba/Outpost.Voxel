#include "pch.h"

#include "Clearances.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace GameLogic
{
namespace
{

using NeuronCore::Float3;

// The directions of a regular octagon's corners from its middle: at 22.5 degrees and every 45 after, so that its sides face
// along the axes and their diagonals. They are spelled out, so that no build's cosine moves a corner.
constexpr float COS_EIGHTH_TURN = 0.92387953f; // of 22.5 degrees
constexpr float SIN_EIGHTH_TURN = 0.38268343f;
constexpr std::array<std::array<float, 2>, 8> OCTAGON{{{COS_EIGHTH_TURN, SIN_EIGHTH_TURN},
                                                       {SIN_EIGHTH_TURN, COS_EIGHTH_TURN},
                                                       {-SIN_EIGHTH_TURN, COS_EIGHTH_TURN},
                                                       {-COS_EIGHTH_TURN, SIN_EIGHTH_TURN},
                                                       {-COS_EIGHTH_TURN, -SIN_EIGHTH_TURN},
                                                       {-SIN_EIGHTH_TURN, -COS_EIGHTH_TURN},
                                                       {SIN_EIGHTH_TURN, -COS_EIGHTH_TURN},
                                                       {COS_EIGHTH_TURN, -SIN_EIGHTH_TURN}}};

// A corner, or a point a move is taken to, stands this far outside the path circles it borders, so that an octagon's own
// sides clear its circle and no rounding puts a point back within one.
constexpr float OUTSIDE_UNITS = 0.05f;

// A path's first point, before any corner.
constexpr std::uint32_t FROM_START = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] float PlaneDistance(Float3 _from, Float3 _to) noexcept
{
  const float dx = _to.x - _from.x;
  const float dz = _to.z - _from.z;
  return std::sqrt(dx * dx + dz * dz);
}

// Whether the segment from _from to _to on the plane keeps at least _radius from (_x, _z).
[[nodiscard]] bool SegmentClears(Float3 _from, Float3 _to, float _x, float _z, float _radius) noexcept
{
  if (std::max(_from.x, _to.x) <= _x - _radius || std::min(_from.x, _to.x) >= _x + _radius || std::max(_from.z, _to.z) <= _z - _radius ||
      std::min(_from.z, _to.z) >= _z + _radius)
  {
    return true;
  }
  const float dx = _to.x - _from.x;
  const float dz = _to.z - _from.z;
  const float lengthSquared = dx * dx + dz * dz;
  const float along = lengthSquared > 0.0f ? std::clamp(((_x - _from.x) * dx + (_z - _from.z) * dz) / lengthSquared, 0.0f, 1.0f) : 0.0f;
  const float nearestX = _from.x + along * dx - _x;
  const float nearestZ = _from.z + along * dz - _z;
  return nearestX * nearestX + nearestZ * nearestZ >= _radius * _radius;
}

} // namespace

Clearances::Clearances(std::span<const Obstacle> _obstacles, float _shipRadius)
{
  m_circles.reserve(_obstacles.size());
  for (const Obstacle& obstacle : _obstacles)
  {
    const float keepOut = obstacle.radius + _shipRadius + CLEARANCE_MARGIN;
    m_circles.push_back({obstacle.center.x, obstacle.center.z, keepOut, keepOut + PATH_SLACK});
  }
  for (const Circle& circle : m_circles)
  {
    const float reach = (circle.path + OUTSIDE_UNITS) / COS_EIGHTH_TURN;
    for (const std::array<float, 2>& direction : OCTAGON)
    {
      const Float3 corner{circle.x + direction[0] * reach, 0.0f, circle.z + direction[1] * reach};
      if (Outside(corner, true))
      {
        m_corners.push_back(corner);
      }
    }
  }
  m_edges.resize(m_corners.size());
  for (std::size_t from = 0; from < m_corners.size(); ++from)
  {
    for (std::size_t to = from + 1; to < m_corners.size(); ++to)
    {
      if (Clear(m_corners[from], m_corners[to], {}))
      {
        const float length = PlaneDistance(m_corners[from], m_corners[to]);
        m_edges[from].push_back({static_cast<std::uint32_t>(to), length});
        m_edges[to].push_back({static_cast<std::uint32_t>(from), length});
      }
    }
  }
}

Float3 Clearances::Reachable(Float3 _point) const noexcept
{
  return Outside(_point, true) ? _point : NearestOutside(_point, true);
}

Float3 Clearances::NearestOutside(Float3 _point, bool _path) const noexcept
{
  // The nearest point outside a union of circles lies where a ray from the point outward meets one circle, or where two
  // circles meet.
  Float3 best = _point;
  float bestDistance = std::numeric_limits<float>::infinity();
  const auto consider = [this, &_point, _path, &best, &bestDistance](float _x, float _z)
  {
    const Float3 candidate{_x, _point.y, _z};
    const float distance = PlaneDistance(_point, candidate);
    if (distance < bestDistance && Outside(candidate, _path))
    {
      best = candidate;
      bestDistance = distance;
    }
  };
  for (const Circle& circle : m_circles)
  {
    const float dx = _point.x - circle.x;
    const float dz = _point.z - circle.z;
    const float length = std::sqrt(dx * dx + dz * dz);
    const float reach = (_path ? circle.path : circle.keepOut) + OUTSIDE_UNITS;
    if (length > 1.0e-3f)
    {
      consider(circle.x + dx * (reach / length), circle.z + dz * (reach / length));
    }
    else
    {
      consider(circle.x + reach, circle.z);
    }
  }
  for (std::size_t first = 0; first < m_circles.size(); ++first)
  {
    for (std::size_t second = first + 1; second < m_circles.size(); ++second)
    {
      const Circle& a = m_circles[first];
      const Circle& b = m_circles[second];
      const float ra = (_path ? a.path : a.keepOut) + OUTSIDE_UNITS;
      const float rb = (_path ? b.path : b.keepOut) + OUTSIDE_UNITS;
      const float dx = b.x - a.x;
      const float dz = b.z - a.z;
      const float apart = std::sqrt(dx * dx + dz * dz);
      if (apart <= 0.0f || apart >= ra + rb || apart <= std::abs(ra - rb))
      {
        continue;
      }
      const float along = (ra * ra - rb * rb + apart * apart) / (2.0f * apart);
      const float across = std::sqrt(std::max(ra * ra - along * along, 0.0f));
      const float ux = dx / apart;
      const float uz = dz / apart;
      consider(a.x + ux * along - uz * across, a.z + uz * along + ux * across);
      consider(a.x + ux * along + uz * across, a.z + uz * along - ux * across);
    }
  }
  return best;
}

std::vector<Float3> Clearances::Path(Float3 _from, Float3 _to) const
{
  // The path circles that hold each end, which the legs from that end judge by their keep-outs.
  std::vector<std::uint8_t> holdsFrom(m_circles.size(), 0);
  std::vector<std::uint8_t> holdsTo(m_circles.size(), 0);
  std::vector<std::uint8_t> holdsEither(m_circles.size(), 0);
  for (std::size_t index = 0; index < m_circles.size(); ++index)
  {
    const Circle& circle = m_circles[index];
    const auto holds = [&circle](Float3 _point)
    {
      const float dx = _point.x - circle.x;
      const float dz = _point.z - circle.z;
      return dx * dx + dz * dz < circle.path * circle.path;
    };
    holdsFrom[index] = holds(_from) ? 1 : 0;
    holdsTo[index] = holds(_to) ? 1 : 0;
    holdsEither[index] = static_cast<std::uint8_t>(holdsFrom[index] | holdsTo[index]);
  }
  const auto atHeight = [&_from](Float3 _point) { return Float3{_point.x, _from.y, _point.z}; };
  if (Clear(_from, _to, holdsEither))
  {
    return {atHeight(_to)};
  }

  // Dijkstra's search over the corners, from _from, with _to as one node more. The nearest node not yet settled is found
  // by going over them all in order, so that ties settle alike in every build.
  const std::size_t goal = m_corners.size();
  std::vector<float> distance(goal + 1, std::numeric_limits<float>::infinity());
  std::vector<std::uint32_t> previous(goal + 1, FROM_START);
  std::vector<std::uint8_t> settled(goal + 1, 0);
  std::vector<std::uint8_t> seesGoal(goal, 0);
  for (std::size_t corner = 0; corner < goal; ++corner)
  {
    if (Clear(_from, m_corners[corner], holdsFrom))
    {
      distance[corner] = PlaneDistance(_from, m_corners[corner]);
    }
    seesGoal[corner] = Clear(m_corners[corner], _to, holdsTo) ? 1 : 0;
  }
  const auto relax = [&distance, &previous](std::size_t _node, float _length, std::size_t _via)
  {
    if (_length < distance[_node])
    {
      distance[_node] = _length;
      previous[_node] = static_cast<std::uint32_t>(_via);
    }
  };
  for (;;)
  {
    std::size_t nearest = goal + 1;
    for (std::size_t node = 0; node <= goal; ++node)
    {
      if (settled[node] == 0 && distance[node] < std::numeric_limits<float>::infinity() &&
          (nearest > goal || distance[node] < distance[nearest]))
      {
        nearest = node;
      }
    }
    if (nearest >= goal)
    {
      break;
    }
    settled[nearest] = 1;
    for (const Edge& edge : m_edges[nearest])
    {
      relax(edge.corner, distance[nearest] + edge.length, nearest);
    }
    if (seesGoal[nearest] != 0)
    {
      relax(goal, distance[nearest] + PlaneDistance(m_corners[nearest], _to), nearest);
    }
  }

  // Short of _to, the corner reached nearest to it.
  std::size_t end = goal;
  if (!(distance[goal] < std::numeric_limits<float>::infinity()))
  {
    end = goal + 1;
    float nearestToGoal = std::numeric_limits<float>::infinity();
    for (std::size_t corner = 0; corner < goal; ++corner)
    {
      if (distance[corner] < std::numeric_limits<float>::infinity() && PlaneDistance(m_corners[corner], _to) < nearestToGoal)
      {
        end = corner;
        nearestToGoal = PlaneDistance(m_corners[corner], _to);
      }
    }
    if (end > goal)
    {
      return {};
    }
  }
  std::vector<Float3> path;
  for (std::size_t node = end; node != FROM_START; node = previous[node])
  {
    path.push_back(atHeight(node == goal ? _to : m_corners[node]));
  }
  std::ranges::reverse(path);
  return path;
}

Float3 Clearances::KeptOut(Float3 _point) const noexcept
{
  return Outside(_point, false) ? _point : NearestOutside(_point, false);
}

float Clearances::Clearance(Float3 _point) const noexcept
{
  float clearance = std::numeric_limits<float>::max();
  for (const Circle& circle : m_circles)
  {
    const float dx = _point.x - circle.x;
    const float dz = _point.z - circle.z;
    clearance = std::min(clearance, std::sqrt(dx * dx + dz * dz) - circle.keepOut);
  }
  return clearance;
}

std::size_t Clearances::CornerCount() const noexcept
{
  return m_corners.size();
}

bool Clearances::Clear(Float3 _from, Float3 _to, std::span<const std::uint8_t> _exempt) const noexcept
{
  for (std::size_t index = 0; index < m_circles.size(); ++index)
  {
    const Circle& circle = m_circles[index];
    const bool exempt = index < _exempt.size() && _exempt[index] != 0;
    if (!SegmentClears(_from, _to, circle.x, circle.z, exempt ? circle.keepOut : circle.path))
    {
      return false;
    }
  }
  return true;
}

bool Clearances::Outside(Float3 _point, bool _path) const noexcept
{
  return std::ranges::all_of(m_circles,
                             [&_point, _path](const Circle& _circle)
                             {
                               const float dx = _point.x - _circle.x;
                               const float dz = _point.z - _circle.z;
                               const float radius = _path ? _circle.path : _circle.keepOut;
                               return dx * dx + dz * dz >= radius * radius;
                             });
}

} // namespace GameLogic
