#pragma once

#include <array>
#include <bitset>
#include <cstdint>

namespace NeuronClient
{

enum class MouseButton : std::uint8_t
{
  Left,
  Right,
  Middle
};

// The keyboard and mouse as the game reads them once a frame. The window procedure writes it as messages arrive; the
// game reads it and then calls EndFrame, which clears what happened only this frame: presses, motion and the wheel.
class InputState
{
public:
  void OnKey(std::uint32_t _virtualKey, bool _down, bool _repeat) noexcept;
  void OnButton(MouseButton _button, bool _down) noexcept;
  void OnMouseMove(std::int32_t _xPixels, std::int32_t _yPixels) noexcept;
  void OnWheel(float _notches) noexcept;

  // Losing focus loses the key-up and button-up messages, so everything is released.
  void OnFocusLost() noexcept;

  [[nodiscard]] bool IsKeyDown(std::uint32_t _virtualKey) const noexcept;

  // Pressed since the last EndFrame; a key's auto-repeat does not count.
  [[nodiscard]] bool WasKeyPressed(std::uint32_t _virtualKey) const noexcept;

  [[nodiscard]] bool IsButtonDown(MouseButton _button) const noexcept;
  [[nodiscard]] bool IsAnyButtonDown() const noexcept;

  // The pointer's motion since the last EndFrame, in physical pixels, +x right and +y down.
  [[nodiscard]] float MouseDeltaXPixels() const noexcept
  {
    return m_deltaXPixels;
  }

  [[nodiscard]] float MouseDeltaYPixels() const noexcept
  {
    return m_deltaYPixels;
  }

  // Wheel notches since the last EndFrame, positive away from the user.
  [[nodiscard]] float WheelNotches() const noexcept
  {
    return m_wheelNotches;
  }

  void EndFrame() noexcept;

private:
  static constexpr std::size_t KEY_COUNT = 256;

  std::bitset<KEY_COUNT> m_keysDown;
  std::bitset<KEY_COUNT> m_keysPressed;
  std::array<bool, 3> m_buttonsDown{};
  std::int32_t m_mouseXPixels = 0;
  std::int32_t m_mouseYPixels = 0;
  bool m_mouseKnown = false;
  float m_deltaXPixels = 0.0f;
  float m_deltaYPixels = 0.0f;
  float m_wheelNotches = 0.0f;
};

} // namespace NeuronClient
