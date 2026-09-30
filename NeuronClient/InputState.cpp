#include "pch.h"

#include "InputState.h"

#include <algorithm>

namespace NeuronClient
{

void InputState::OnKey(std::uint32_t _virtualKey, bool _down, bool _repeat) noexcept
{
  if (_virtualKey >= KEY_COUNT)
  {
    return;
  }
  if (_down && !_repeat)
  {
    m_keysPressed.set(_virtualKey);
  }
  m_keysDown.set(_virtualKey, _down);
}

void InputState::OnButton(MouseButton _button, bool _down) noexcept
{
  const auto button = static_cast<std::size_t>(_button);
  if (_down && !m_buttonsDown[button])
  {
    m_buttonsPressed[button] = true;
  }
  if (!_down && m_buttonsDown[button])
  {
    m_buttonsReleased[button] = true;
  }
  m_buttonsDown[button] = _down;
}

void InputState::OnMouseMove(std::int32_t _xPixels, std::int32_t _yPixels) noexcept
{
  if (m_mouseKnown)
  {
    m_deltaXPixels += static_cast<float>(_xPixels - m_mouseXPixels);
    m_deltaYPixels += static_cast<float>(_yPixels - m_mouseYPixels);
  }
  m_mouseXPixels = _xPixels;
  m_mouseYPixels = _yPixels;
  m_mouseKnown = true;
}

void InputState::OnPointerLeft() noexcept
{
  m_mouseKnown = false;
}

void InputState::OnWheel(float _notches) noexcept
{
  m_wheelNotches += _notches;
}

void InputState::OnFocusLost() noexcept
{
  m_keysDown.reset();
  m_buttonsDown.fill(false);
  m_mouseKnown = false;
}

bool InputState::IsKeyDown(std::uint32_t _virtualKey) const noexcept
{
  return _virtualKey < KEY_COUNT && m_keysDown.test(_virtualKey);
}

bool InputState::WasKeyPressed(std::uint32_t _virtualKey) const noexcept
{
  return _virtualKey < KEY_COUNT && m_keysPressed.test(_virtualKey);
}

bool InputState::IsButtonDown(MouseButton _button) const noexcept
{
  return m_buttonsDown[static_cast<std::size_t>(_button)];
}

bool InputState::IsAnyButtonDown() const noexcept
{
  return std::ranges::any_of(m_buttonsDown, [](bool _down) { return _down; });
}

bool InputState::WasButtonPressed(MouseButton _button) const noexcept
{
  return m_buttonsPressed[static_cast<std::size_t>(_button)];
}

bool InputState::WasButtonReleased(MouseButton _button) const noexcept
{
  return m_buttonsReleased[static_cast<std::size_t>(_button)];
}

void InputState::EndFrame() noexcept
{
  m_keysPressed.reset();
  m_buttonsPressed.fill(false);
  m_buttonsReleased.fill(false);
  m_deltaXPixels = 0.0f;
  m_deltaYPixels = 0.0f;
  m_wheelNotches = 0.0f;
}

} // namespace NeuronClient
