#pragma once

#include "InputState.h"
#include "Pick.h"
#include "SnapshotBuffer.h"
#include "Surface.h"

#include "Orders.h"

#include "Float3.h"
#include "PerspectiveView.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace GameLib
{

// What the order bar, or its keys, asked for this frame.
enum class OrderAction : std::uint8_t
{
  None,
  Move, // arm a move: the next click in the world gives it
  Stop,
  Hold,
  Attack // arm an attack: the next click in the world gives it (Design/ADR/ADR-035)
};

// What the next click in the world gives, once the order bar has armed it.
enum class ArmedOrder : std::uint8_t
{
  None,
  Move,
  Attack
};

// How the commander draws what it knows in the world's overlay (Design/ADR/ADR-034), linear, and the widths, in pixels.
struct CommanderLook
{
  NeuronCore::Float3 ownColor;     // the ring of a selected entity of the side's own
  NeuronCore::Float3 enemyColor;   // of another side's
  NeuronCore::Float3 neutralColor; // of one of no side
  NeuronCore::Float3 orderColor;   // a move's line and marker
  NeuronCore::Float3 attackColor;  // an attack's and an attack-move's (Design/ADR/ADR-035)
  float ringWidthPixels;
  float lineWidthPixels;
  float markerPixels;
};

// The player's selection, control groups and orders (the concept's §9, Design/ADR/ADR-034), from the pointer and the keys
// the interface leaves the world. An observer selects, and orders nothing.
class Commander
{
public:
  // _side is the side the session plays, or NeuronCore::OBSERVER_SIDE.
  explicit Commander(std::uint8_t _side) noexcept;

  // A frame's commanding, which returns the orders to send:
  // - A click selects the entity under the pointer, or nothing. A drag selects the side's ships whose middles its box
  //   holds, or when it holds none of them, every entity whose middle it holds (the concept's §9). With Shift, a click
  //   adds its entity to the selection, or takes it out, and a drag adds.
  // - A right-click orders the selected ships of the side's: to attack the enemy under the pointer, an entity of another
  //   side, or else to move to the point of the plane under it (Design/ADR/ADR-035).
  // - A click while a move is armed moves them to the point, over an enemy or not; while an attack is armed, it attacks
  //   the enemy under the pointer, or else attack-moves them to the point. Every order is marked at once.
  // - _action stops or holds them, or arms a move or an attack.
  // - Ctrl with a digit makes the selection a control group, and the digit alone selects what of the group can still be
  //   picked.
  // - Esc disarms an order, or else clears the selection.
  // _pointerIsWorlds says whether the interface left the pointer to the world. _boxes are what the pointer can pick, the
  // whole entities, _orderable the side's whole ships, _enemies the whole entities of other sides, and _nowSeconds the
  // client's clock.
  [[nodiscard]] std::vector<GameCore::Order> Update(const NeuronClient::InputState& _input, bool _pointerIsWorlds,
                                                    const NeuronCore::PerspectiveView& _view, std::span<const NeuronClient::PickBox> _boxes,
                                                    std::span<const std::uint32_t> _orderable, std::span<const std::uint32_t> _enemies,
                                                    OrderAction _action, double _nowSeconds);

  // Draws in the world's overlay: a ring about each selected entity, as wide as _radii says its composite is; a marker at
  // each of the side's moves and attack-moves, as _states give them, and a line to it from each selected ship; a line
  // from each selected ship attacking to its target, when _sample holds it; the marks of the orders just given, fading;
  // and the box of a drag.
  void Draw(NeuronClient::Surface& _surface, const NeuronCore::PerspectiveView& _view, const NeuronClient::WorldSample& _sample,
            std::span<const GameCore::ShipOrderState> _states, std::span<const float> _radii, const CommanderLook& _look,
            double _nowSeconds) const;

  // The selection, in the order of its ids.
  [[nodiscard]] std::span<const std::uint32_t> Selection() const noexcept
  {
    return m_selection;
  }

  // Selects _id alone.
  void SelectOnly(std::uint32_t _id);

  // Leaves out of the selection what _sample no longer holds.
  void Keep(const NeuronClient::WorldSample& _sample);

  [[nodiscard]] ArmedOrder Armed() const noexcept
  {
    return m_armed;
  }

private:
  // An order's mark: where it was given, when, and whether it was an attack or an attack-move.
  struct Mark
  {
    float x;
    float z;
    double givenSeconds;
    bool attack;
  };

  // The selected ships of the side's.
  [[nodiscard]] std::vector<std::uint32_t> Ordered(std::span<const std::uint32_t> _orderable) const;

  std::uint8_t m_side;
  std::vector<std::uint32_t> m_selection;
  std::array<std::vector<std::uint32_t>, 10> m_groups;
  std::optional<NeuronCore::Float2> m_dragStart; // where the world's left press landed, while it is held
  NeuronCore::Float2 m_pointer{};
  ArmedOrder m_armed = ArmedOrder::None;
  std::vector<Mark> m_marks;
};

// The point of the sector's plane, y = 0, under the centre of pixel _pixel of _view, or nothing when its ray does not come
// down to the plane.
[[nodiscard]] std::optional<NeuronCore::Float3> PlanePoint(const NeuronCore::PerspectiveView& _view, NeuronCore::Float2 _pixel) noexcept;

} // namespace GameLib
