#include "pch.h"

#include "Commander.h"

#include "Overlay.h"

#include "Message.h"
#include "Ray.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace GameLib
{
namespace
{

using NeuronClient::MouseButton;
using NeuronCore::Float2;
using NeuronCore::Float3;

// A press that moves less than this far before its release is a click, and one that moves further a drag.
constexpr float CLICK_PIXELS = 4.0f;

// An order's mark fades over this long.
constexpr double MARK_SECONDS = 1.0;

// The Windows SDK's VK_ESCAPE; the digits' keys are their characters.
constexpr std::uint32_t ESCAPE_KEY = 0x1B;

// A selected entity's ring stands this far out from its sphere, as a share of it.
constexpr float RING_SCALE = 1.15f;

// The alphas of the overlay's marks: a selected entity's ring, a move's line, and its marker for a ship selected and not.
constexpr float RING_ALPHA = 0.9f;
constexpr float LINE_ALPHA = 0.45f;
constexpr float SELECTED_MARKER_ALPHA = 0.9f;
constexpr float MARKER_ALPHA = 0.4f;
constexpr float BOX_FILL_ALPHA = 0.08f;

[[nodiscard]] float Apart(Float2 _a, Float2 _b) noexcept
{
  return std::hypot(_b.x - _a.x, _b.y - _a.y);
}

[[nodiscard]] bool Holds(std::span<const std::uint32_t> _ids, std::uint32_t _id) noexcept
{
  return std::ranges::find(_ids, _id) != _ids.end();
}

[[nodiscard]] bool Pickable(std::span<const NeuronClient::PickBox> _boxes, std::uint32_t _id) noexcept
{
  return std::ranges::find(_boxes, _id, &NeuronClient::PickBox::id) != _boxes.end();
}

[[nodiscard]] const NeuronClient::SampledEntity* Find(const NeuronClient::WorldSample& _sample, std::uint32_t _id) noexcept
{
  const auto found = std::ranges::lower_bound(_sample.entities, _id, {}, &NeuronClient::SampledEntity::id);
  return found != _sample.entities.end() && found->id == _id ? &*found : nullptr;
}

void Normalize(std::vector<std::uint32_t>& _ids)
{
  std::ranges::sort(_ids);
  const auto repeated = std::ranges::unique(_ids);
  _ids.erase(repeated.begin(), repeated.end());
}

} // namespace

std::optional<Float3> PlanePoint(const NeuronCore::PerspectiveView& _view, Float2 _pixel) noexcept
{
  if (!(_pixel.x >= 0.0f && _pixel.y >= 0.0f && _pixel.x < static_cast<float>(_view.widthPixels) &&
        _pixel.y < static_cast<float>(_view.heightPixels)))
  {
    return std::nullopt;
  }
  const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(_view, static_cast<std::uint32_t>(_pixel.x), static_cast<std::uint32_t>(_pixel.y));
  if (!(ray.direction.y < 0.0f) || !(ray.origin.y > 0.0f))
  {
    return std::nullopt;
  }
  return ray.origin + ray.direction * (-ray.origin.y / ray.direction.y);
}

Commander::Commander(std::uint8_t _side) noexcept
  : m_side(_side)
{
}

std::vector<GameCore::Order> Commander::Update(const NeuronClient::InputState& _input, bool _pointerIsWorlds,
                                               const NeuronCore::PerspectiveView& _view, std::span<const NeuronClient::PickBox> _boxes,
                                               std::span<const std::uint32_t> _orderable, std::span<const std::uint32_t> _enemies,
                                               OrderAction _action, double _nowSeconds)
{
  std::vector<GameCore::Order> orders;
  std::erase_if(m_marks, [_nowSeconds](const Mark& _mark) { return _nowSeconds - _mark.givenSeconds >= MARK_SECONDS; });
  m_pointer = {static_cast<float>(_input.PointerXPixels()), static_cast<float>(_input.PointerYPixels())};
  std::vector<std::uint32_t> ordered = Ordered(_orderable);
  if (ordered.size() > GameCore::MAX_ORDER_SHIPS)
  {
    ordered.resize(GameCore::MAX_ORDER_SHIPS);
  }
  // The order a click at _pixel gives, _armed as it was: an attack on the enemy under it, unless a move is armed, and
  // otherwise a move to the point of the plane under it, or an attack-move while an attack is armed.
  const auto orderAt = [this, &orders, &ordered, &_view, _boxes, _enemies, _nowSeconds](Float2 _pixel, ArmedOrder _armed)
  {
    m_armed = ArmedOrder::None;
    if (ordered.empty())
    {
      return;
    }
    const std::optional<std::uint32_t> picked = _armed == ArmedOrder::Move ? std::nullopt : NeuronClient::Pick(_view, _boxes, _pixel);
    if (picked.has_value() && Holds(_enemies, *picked))
    {
      const auto box = std::ranges::find(_boxes, *picked, &NeuronClient::PickBox::id);
      orders.push_back({GameCore::OrderKind::Attack, ordered, 0.0f, 0.0f, *picked});
      m_marks.push_back({box->middle.x, box->middle.z, _nowSeconds, true});
      return;
    }
    if (const std::optional<Float3> point = PlanePoint(_view, _pixel); point.has_value())
    {
      const bool attack = _armed == ArmedOrder::Attack;
      orders.push_back({attack ? GameCore::OrderKind::AttackMove : GameCore::OrderKind::Move, ordered, point->x, point->z});
      m_marks.push_back({point->x, point->z, _nowSeconds, attack});
    }
  };

  // The order bar.
  if (!ordered.empty())
  {
    switch (_action)
    {
    case OrderAction::Move:
      m_armed = ArmedOrder::Move;
      break;
    case OrderAction::Attack:
      m_armed = ArmedOrder::Attack;
      break;
    case OrderAction::Stop:
      orders.push_back({GameCore::OrderKind::Stop, ordered, 0.0f, 0.0f});
      m_armed = ArmedOrder::None;
      break;
    case OrderAction::Hold:
      orders.push_back({GameCore::OrderKind::Hold, ordered, 0.0f, 0.0f});
      m_armed = ArmedOrder::None;
      break;
    case OrderAction::None:
      break;
    }
  }

  // Esc, and the control groups. Alt with a digit is a debug key's.
  if (_input.WasKeyPressed(ESCAPE_KEY))
  {
    if (m_armed != ArmedOrder::None)
    {
      m_armed = ArmedOrder::None;
    }
    else
    {
      m_selection.clear();
    }
  }
  if (!_input.IsKeyDown(NeuronClient::ALT_KEY))
  {
    for (std::uint32_t digit = 0; digit < m_groups.size(); ++digit)
    {
      if (!_input.WasKeyPressed('0' + digit))
      {
        continue;
      }
      std::vector<std::uint32_t>& group = m_groups[digit];
      if (_input.IsKeyDown(NeuronClient::CONTROL_KEY))
      {
        group = m_selection;
      }
      else if (!group.empty())
      {
        m_selection.clear();
        std::ranges::copy_if(group, std::back_inserter(m_selection), [&_boxes](std::uint32_t _id) { return Pickable(_boxes, _id); });
      }
    }
  }

  // The pointer, when the interface left it to the world.
  if (_pointerIsWorlds && _input.HasPointer())
  {
    if (_input.WasButtonPressed(MouseButton::Left))
    {
      m_dragStart = m_pointer;
    }
    if (_input.WasButtonReleased(MouseButton::Left) && m_dragStart.has_value())
    {
      const Float2 start = *m_dragStart;
      m_dragStart.reset();
      const bool shift = _input.IsKeyDown(NeuronClient::SHIFT_KEY);
      if (Apart(start, m_pointer) < CLICK_PIXELS)
      {
        if (m_armed != ArmedOrder::None)
        {
          orderAt(m_pointer, m_armed);
        }
        else if (const std::optional<std::uint32_t> picked = NeuronClient::Pick(_view, _boxes, m_pointer); shift && picked.has_value())
        {
          if (Holds(m_selection, *picked))
          {
            std::erase(m_selection, *picked);
          }
          else
          {
            m_selection.push_back(*picked);
          }
        }
        else if (!shift)
        {
          m_selection.clear();
          if (picked.has_value())
          {
            m_selection.push_back(*picked);
          }
        }
      }
      else
      {
        std::vector<std::uint32_t> boxed = NeuronClient::PickWithin(_view, _boxes, start, m_pointer);
        // A box that holds any of the side's ships selects only those (the concept's §9).
        if (std::ranges::any_of(boxed, [_orderable](std::uint32_t _id) { return Holds(_orderable, _id); }))
        {
          std::erase_if(boxed, [_orderable](std::uint32_t _id) { return !Holds(_orderable, _id); });
        }
        if (!shift)
        {
          m_selection.clear();
        }
        m_selection.insert(m_selection.end(), boxed.begin(), boxed.end());
      }
    }
    if (_input.WasButtonReleased(MouseButton::Right))
    {
      if (m_armed != ArmedOrder::None)
      {
        m_armed = ArmedOrder::None;
      }
      else
      {
        orderAt(m_pointer, ArmedOrder::None);
      }
    }
  }
  if (!_input.IsButtonDown(MouseButton::Left) && !_input.WasButtonReleased(MouseButton::Left))
  {
    m_dragStart.reset();
  }
  Normalize(m_selection);
  return orders;
}

void Commander::Draw(NeuronClient::Surface& _surface, const NeuronCore::PerspectiveView& _view, const NeuronClient::WorldSample& _sample,
                     std::span<const GameCore::ShipOrderState> _states, std::span<const float> _radii, const CommanderLook& _look,
                     double _nowSeconds) const
{
  for (const std::uint32_t id : m_selection)
  {
    const NeuronClient::SampledEntity* entity = Find(_sample, id);
    if (entity == nullptr || entity->composite >= _radii.size())
    {
      continue;
    }
    const Float3 color = entity->side == 0 ? _look.neutralColor : (entity->side == m_side ? _look.ownColor : _look.enemyColor);
    NeuronClient::DrawRing(_surface, _view, entity->position, _radii[entity->composite] * RING_SCALE, _look.ringWidthPixels, color,
                           RING_ALPHA);
  }
  for (const GameCore::ShipOrderState& state : _states)
  {
    const NeuronClient::SampledEntity* ship = Find(_sample, state.ship);
    if (ship == nullptr)
    {
      continue;
    }
    const bool selected = Holds(m_selection, state.ship);
    if (state.state == GameCore::ShipState::Moving || state.state == GameCore::ShipState::AttackMoving)
    {
      const Float3 color = state.state == GameCore::ShipState::Moving ? _look.orderColor : _look.attackColor;
      const Float3 destination{state.destinationX, ship->position.y, state.destinationZ};
      if (selected)
      {
        NeuronClient::DrawLine(_surface, _view, ship->position, destination, _look.lineWidthPixels, color, LINE_ALPHA);
      }
      NeuronClient::DrawMarker(_surface, _view, destination, _look.markerPixels, _look.lineWidthPixels, color,
                               selected ? SELECTED_MARKER_ALPHA : MARKER_ALPHA);
    }
    else if (state.state == GameCore::ShipState::Attacking && selected)
    {
      if (const NeuronClient::SampledEntity* target = Find(_sample, state.target); target != nullptr)
      {
        NeuronClient::DrawLine(_surface, _view, ship->position, target->position, _look.lineWidthPixels, _look.attackColor, LINE_ALPHA);
      }
    }
  }
  for (const Mark& mark : m_marks)
  {
    const auto fade = static_cast<float>(std::clamp((_nowSeconds - mark.givenSeconds) / MARK_SECONDS, 0.0, 1.0));
    NeuronClient::DrawMarker(_surface, _view, {mark.x, 0.0f, mark.z}, _look.markerPixels * (1.0f + fade), _look.lineWidthPixels,
                             mark.attack ? _look.attackColor : _look.orderColor, 1.0f - fade);
  }
  if (m_dragStart.has_value() && Apart(*m_dragStart, m_pointer) >= CLICK_PIXELS)
  {
    const Float2 lower = NeuronCore::Min(*m_dragStart, m_pointer);
    const Float2 upper = NeuronCore::Max(*m_dragStart, m_pointer);
    _surface.FillRectangle(static_cast<std::int32_t>(std::lround(lower.x)), static_cast<std::int32_t>(std::lround(lower.y)),
                           static_cast<std::uint32_t>(std::lround(upper.x - lower.x)),
                           static_cast<std::uint32_t>(std::lround(upper.y - lower.y)), _look.ownColor, BOX_FILL_ALPHA);
    const std::array<Float2, 4> corners{{lower, {upper.x, lower.y}, upper, {lower.x, upper.y}}};
    for (std::size_t corner = 0; corner < corners.size(); ++corner)
    {
      _surface.DrawSegment(corners[corner], corners[(corner + 1) % corners.size()], _look.lineWidthPixels, _look.ownColor, RING_ALPHA);
    }
  }
}

void Commander::SelectOnly(std::uint32_t _id)
{
  m_selection.assign(1, _id);
}

void Commander::Keep(const NeuronClient::WorldSample& _sample)
{
  std::erase_if(m_selection, [&_sample](std::uint32_t _id) { return Find(_sample, _id) == nullptr; });
}

std::vector<std::uint32_t> Commander::Ordered(std::span<const std::uint32_t> _orderable) const
{
  std::vector<std::uint32_t> ordered;
  if (m_side == NeuronCore::OBSERVER_SIDE)
  {
    return ordered;
  }
  std::ranges::copy_if(m_selection, std::back_inserter(ordered), [&_orderable](std::uint32_t _id) { return Holds(_orderable, _id); });
  return ordered;
}

} // namespace GameLib
