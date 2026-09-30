#pragma once

#include "Route.h"
#include "ShipMotion.h"

#include "Float3.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace GameLogic
{

// A ship's keep-out of an obstacle on the plane (Design/ADR/ADR-033, Design/MvpPlan.md §2.2 on G61) is a circle about the
// obstacle's center, as wide as the obstacle's sphere, the ship's, and this margin together. The ship never enters it.
inline constexpr float CLEARANCE_MARGIN = 10.0f;

// Paths keep this much wider of an obstacle than its keep-out: the room a ship's corners take (ShipMotion.h).
inline constexpr float PATH_SLACK = CORNER_SWING;

// The clearances of a set of obstacles on the plane, for ships of one sphere (ADR-033): where they may go, and the
// shortest ways there. An obstacle is a core's or an asteroid's sphere, of which only the center's x and z count. Each
// has its keep-out and, PATH_SLACK wider, its path circle. A path runs straight between corners of the regular octagons
// that circumscribe the path circles. Only corners outside every path circle count, and the corners that see one another
// past every path circle are joined once, when the clearances are made.
class Clearances
{
public:
  Clearances(std::span<const Obstacle> _obstacles, float _shipRadius);

  // Where a move to _point goes: _point itself, or, when it lies within a path circle, the nearest point outside every
  // one, at _point's height.
  [[nodiscard]] NeuronCore::Float3 Reachable(NeuronCore::Float3 _point) const noexcept;

  // The shortest way on the plane from _from to _to around the path circles: the corners it turns at, then _to, each at
  // _from's height. A path circle that holds _from or _to is judged by its keep-out on the legs from them instead, so
  // that a ship in a path's slack still finds a way out. When no way reaches _to, the way leads to the corner nearest to
  // it that one reaches, and it is empty when none is.
  [[nodiscard]] std::vector<NeuronCore::Float3> Path(NeuronCore::Float3 _from, NeuronCore::Float3 _to) const;

  // Where a ship at _point is let be: _point itself, or, when it lies within a keep-out, the nearest point outside every
  // one, at _point's height.
  [[nodiscard]] NeuronCore::Float3 KeptOut(NeuronCore::Float3 _point) const noexcept;

  // How far _point lies outside the nearest keep-out on the plane, less than 0 within one.
  [[nodiscard]] float Clearance(NeuronCore::Float3 _point) const noexcept;

  // The corners of the graph: the octagons' corners that lie outside every path circle.
  [[nodiscard]] std::size_t CornerCount() const noexcept;

private:
  struct Circle
  {
    float x;
    float z;
    float keepOut; // radii
    float path;
  };

  struct Edge
  {
    std::uint32_t corner;
    float length;
  };

  // Whether the segment from _from to _to on the plane clears every path circle; one that _exempt marks, only its keep-out.
  [[nodiscard]] bool Clear(NeuronCore::Float3 _from, NeuronCore::Float3 _to, std::span<const std::uint8_t> _exempt) const noexcept;

  // Whether _point lies outside every path circle, or with _path false every keep-out.
  [[nodiscard]] bool Outside(NeuronCore::Float3 _point, bool _path) const noexcept;

  // The nearest point to _point outside every path circle, or with _path false every keep-out, by OUTSIDE_UNITS, at
  // _point's height.
  [[nodiscard]] NeuronCore::Float3 NearestOutside(NeuronCore::Float3 _point, bool _path) const noexcept;

  std::vector<Circle> m_circles;
  std::vector<NeuronCore::Float3> m_corners; // on the plane, at a height of 0
  std::vector<std::vector<Edge>> m_edges;    // each corner's, to the corners it sees, in their order
};

} // namespace GameLogic
