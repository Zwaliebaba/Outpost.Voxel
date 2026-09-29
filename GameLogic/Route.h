#pragma once

#include "Float3.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace GameLogic
{

// A circle a route flies around a station (Design/Archive/SpaceScene.md §5.2): about its center, in the plane square to its
// normal, some whole laps before it leaves, turning counterclockwise about the normal or clockwise.
struct Orbit
{
  NeuronCore::Float3 center;
  NeuronCore::Float3 normal; // unit, tilted at most 45 degrees from the world's up
  float radius;
  std::uint32_t laps;
  bool clockwise;
};

// A sphere a transit keeps out of: a station's keep-out, widened by what the flight needs around it.
struct Obstacle
{
  NeuronCore::Float3 center;
  float radius;
};

// A piece of a route: an arc of an orbit, or a straight line of a transit. An arc turns about its center by its sweep,
// counterclockwise about its normal when the sweep is positive, from the angle it starts at, measured from its first
// axis toward its normal crossed with the first. A line has a radius and a sweep of 0.
struct RoutePiece
{
  NeuronCore::Float3 start;
  NeuronCore::Float3 end;
  NeuronCore::Float3 center;
  NeuronCore::Float3 normal; // an arc's orbit normal, the world's up for a line: the up a ship banks from on it
  NeuronCore::Float3 first;  // an arc's: unit, square to its normal
  float radius;
  float startAngle; // radians
  float sweep;      // radians
  float distance;   // how far along the route it starts
  float length;
};

// A closed route (§5.2): a flight's path through its stations, a chain of arcs and lines that ends where it starts.
// Distances along it wrap at its length.
class Route
{
public:
  // The route that flies _pieces, at least one, in order: each starts where the one before it ends, and the last ends
  // where the first starts. Their distances and lengths are filled in here, and an arc's ends.
  explicit Route(std::vector<RoutePiece> _pieces);

  [[nodiscard]] float Length() const noexcept
  {
    return m_length;
  }

  [[nodiscard]] std::span<const RoutePiece> Pieces() const noexcept
  {
    return m_pieces;
  }

  // The point _distance along the route, the unit direction the route runs there, and the up a ship banks from there.
  [[nodiscard]] NeuronCore::Float3 PositionAt(float _distance) const noexcept;
  [[nodiscard]] NeuronCore::Float3 DirectionAt(float _distance) const noexcept;
  [[nodiscard]] NeuronCore::Float3 ReferenceUpAt(float _distance) const noexcept;

  // How far along the route _position lies, near _progress: the nearest point within a window of it, wide enough to
  // reach a formation's last slot and short of a lap, so that a ship is tracked along the route and never jumps across
  // to where the route comes by again.
  [[nodiscard]] float Track(float _progress, NeuronCore::Float3 _position) const noexcept;

  // How far _to lies ahead of _from along the route, the shorter way round: negative when it lies behind.
  [[nodiscard]] float Separation(float _from, float _to) const noexcept;

private:
  [[nodiscard]] const RoutePiece& PieceAt(float _distance) const noexcept;
  [[nodiscard]] float Wrap(float _distance) const noexcept;

  std::vector<RoutePiece> m_pieces;
  float m_length;
};

// The point _along into _piece, from its start.
[[nodiscard]] NeuronCore::Float3 PointOnPiece(const RoutePiece& _piece, float _along) noexcept;

// The closed route through _orbits in order, back to the first (§5.2). Each orbit is flown whole laps and then on to
// the point where it leaves toward the next; each transit is a straight line from one orbit to the next, made nearly
// tangent to both, and bent around any of _obstacles it would cross. A single orbit is a route of its own.
[[nodiscard]] Route BuildRoute(std::span<const Orbit> _orbits, std::span<const Obstacle> _obstacles);

} // namespace GameLogic
