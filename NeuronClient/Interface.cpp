#include "pch.h"

#include "Interface.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace NeuronClient
{
namespace
{

using NeuronCore::Float2;
using NeuronCore::Float3;

// A widget's fill, over the panel it stands on.
constexpr float WIDGET_ALPHA = 0.9f;

// A disabled button's reason stands this far to the right of the pointer and above it.
constexpr float REASON_OFFSET_PIXELS = 16.0f;

constexpr std::array<MouseButton, 3> BUTTONS{MouseButton::Left, MouseButton::Right, MouseButton::Middle};

[[nodiscard]] bool AnyPressed(const InputState& _input) noexcept
{
  return std::ranges::any_of(BUTTONS, [&_input](MouseButton _button) { return _input.WasButtonPressed(_button); });
}

// A hotkey counts only on its own: Alt and Ctrl make other keys of it (Design/ADR/ADR-034).
[[nodiscard]] bool PlainlyPressed(const InputState& _input, std::uint32_t _key) noexcept
{
  return _input.WasKeyPressed(_key) && !_input.IsKeyDown(ALT_KEY) && !_input.IsKeyDown(CONTROL_KEY);
}

} // namespace

Interface::Interface(const InterfaceStyle& _style) noexcept
  : m_style(_style)
{
}

void Interface::Begin(Surface& _surface, const InputState& _input)
{
  m_surface = &_surface;
  m_input = &_input;
  m_room.clear();
  m_keys.clear();
  m_reason.clear();

  // A press ends with the frame of its last button's release, so that that frame still knows whose it was. A press made
  // this frame is given its owner at End, once the frame's room is known.
  if (m_pressEnded)
  {
    m_press.reset();
    m_owner = Owner::Nobody;
    m_pressEnded = false;
  }
  if (AnyPressed(_input) && m_owner == Owner::Nobody)
  {
    m_press = Pointer();
  }
}

void Interface::End()
{
  if (m_press.has_value() && m_owner == Owner::Nobody)
  {
    const Float2 press = *m_press;
    m_owner =
      std::ranges::any_of(m_room, [press](const PixelRect& _rect) { return _rect.Contains(press); }) ? Owner::Interface : Owner::World;
  }
  m_pressEnded = !m_input->IsAnyButtonDown();
  if (!m_reason.empty() && m_surface != nullptr)
  {
    const TextExtent extent = m_surface->Measure(m_reason, m_style.text);
    const float padding = m_style.paddingPixels;
    const Float2 pointer = Pointer();
    const PixelRect box{pointer.x + REASON_OFFSET_PIXELS, pointer.y - REASON_OFFSET_PIXELS - extent.heightPixels - 2.0f * padding,
                        extent.widthPixels + 2.0f * padding, extent.heightPixels + 2.0f * padding};
    Fill(box, m_style.panelColor, WIDGET_ALPHA);
    m_surface->Print(m_reason, box.x + padding, box.y + padding, m_style.text, m_style.textColor, 1.0f);
  }
}

void Interface::Panel(const PixelRect& _rect)
{
  m_room.push_back(_rect);
  Fill(_rect, m_style.panelColor, m_style.panelAlpha);
}

TextExtent Interface::Label(float _xPixels, float _yPixels, std::wstring_view _text, std::optional<Float3> _color)
{
  return m_surface->Print(_text, _xPixels, _yPixels, m_style.text, _color.value_or(m_style.textColor), 1.0f);
}

bool Interface::Button(const PixelRect& _rect, std::wstring_view _text, std::optional<Hotkey> _hotkey, std::wstring_view _disabledReason)
{
  m_room.push_back(_rect);
  const bool enabled = _disabledReason.empty();
  const bool hovered = m_input->HasPointer() && m_owner != Owner::World && _rect.Contains(Pointer());
  const bool held = hovered && m_press.has_value() && _rect.Contains(*m_press) && m_input->IsButtonDown(MouseButton::Left);
  const Float3 color =
    !enabled ? m_style.buttonColor : (held ? m_style.pressedColor : (hovered ? m_style.hoverColor : m_style.buttonColor));
  Fill(_rect, color, WIDGET_ALPHA);

  const float padding = m_style.paddingPixels;
  const TextExtent extent = m_surface->Measure(_text, m_style.text);
  const float textY = _rect.y + 0.5f * (_rect.height - extent.heightPixels);
  m_surface->Print(_text, _rect.x + padding, textY, m_style.text, enabled ? m_style.textColor : m_style.dimmedColor, 1.0f);
  bool clicked = false;
  if (_hotkey.has_value())
  {
    const TextExtent key = m_surface->Measure(_hotkey->name, m_style.text);
    m_surface->Print(_hotkey->name, _rect.x + _rect.width - padding - key.widthPixels, textY, m_style.text, m_style.dimmedColor, 1.0f);
    if (PlainlyPressed(*m_input, _hotkey->key))
    {
      m_keys.push_back(_hotkey->key);
      clicked = true;
    }
  }
  if (!enabled)
  {
    if (hovered)
    {
      m_reason = _disabledReason;
    }
    return false;
  }
  return clicked || Clicked(_rect);
}

void Interface::Bar(const PixelRect& _rect, float _fraction, Float3 _color)
{
  m_room.push_back(_rect);
  Fill(_rect, m_style.barBackColor, WIDGET_ALPHA);
  Fill({_rect.x, _rect.y, _rect.width * std::clamp(_fraction, 0.0f, 1.0f), _rect.height}, _color, WIDGET_ALPHA);
}

std::optional<std::size_t> Interface::List(const PixelRect& _rect, std::span<const std::wstring> _rows, std::optional<std::size_t> _chosen)
{
  m_room.push_back(_rect);
  const float rowHeight = RowHeightPixels();
  const auto fitting = static_cast<std::size_t>(std::max(0.0f, std::floor(_rect.height / rowHeight)));
  std::optional<std::size_t> clicked;
  for (std::size_t row = 0; row < std::min(_rows.size(), fitting); ++row)
  {
    const PixelRect rowRect{_rect.x, _rect.y + static_cast<float>(row) * rowHeight, _rect.width, rowHeight};
    const bool hovered = m_input->HasPointer() && m_owner != Owner::World && rowRect.Contains(Pointer());
    if (_chosen == row)
    {
      Fill(rowRect, m_style.selectedColor, WIDGET_ALPHA);
    }
    else if (hovered)
    {
      Fill(rowRect, m_style.hoverColor, WIDGET_ALPHA);
    }
    m_surface->Print(_rows[row], rowRect.x + m_style.paddingPixels, rowRect.y + m_style.paddingPixels, m_style.text, m_style.textColor,
                     1.0f);
    if (Clicked(rowRect))
    {
      clicked = row;
    }
  }
  return clicked;
}

float Interface::RowHeightPixels() const noexcept
{
  // DirectWrite's line of Consolas or Segoe UI is some 1.2 to 1.35 ems; 1.4 holds either.
  constexpr float LINE_EMS = 1.4f;
  return std::ceil(m_style.text.sizePixels * LINE_EMS) + 2.0f * m_style.paddingPixels;
}

bool Interface::OwnsPointer() const noexcept
{
  if (m_owner != Owner::Nobody)
  {
    return m_owner == Owner::Interface;
  }
  const Float2 pointer = Pointer();
  return m_input != nullptr && m_input->HasPointer() &&
         std::ranges::any_of(m_room, [pointer](const PixelRect& _rect) { return _rect.Contains(pointer); });
}

bool Interface::TookKey(std::uint32_t _key) const noexcept
{
  return std::ranges::find(m_keys, _key) != m_keys.end();
}

Float2 Interface::Pointer() const noexcept
{
  return m_input != nullptr ? Float2{static_cast<float>(m_input->PointerXPixels()), static_cast<float>(m_input->PointerYPixels())}
                            : Float2{0.0f, 0.0f};
}

bool Interface::Clicked(const PixelRect& _rect) const noexcept
{
  return m_owner != Owner::World && m_press.has_value() && _rect.Contains(*m_press) && m_input->WasButtonReleased(MouseButton::Left) &&
         _rect.Contains(Pointer());
}

void Interface::Fill(const PixelRect& _rect, Float3 _color, float _alpha)
{
  // Each edge rounds to its own pixel, so that rectangles side by side meet without a gap or an overlap.
  const long left = std::lround(_rect.x);
  const long top = std::lround(_rect.y);
  const long right = std::lround(_rect.x + _rect.width);
  const long bottom = std::lround(_rect.y + _rect.height);
  if (right > left && bottom > top)
  {
    m_surface->FillRectangle(static_cast<std::int32_t>(left), static_cast<std::int32_t>(top), static_cast<std::uint32_t>(right - left),
                             static_cast<std::uint32_t>(bottom - top), _color, _alpha);
  }
}

} // namespace NeuronClient
