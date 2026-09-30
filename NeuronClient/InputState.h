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

// The modifier keys, as the Windows SDK numbers them: VK_SHIFT, VK_CONTROL and VK_MENU, which is Alt.
inline constexpr std::uint32_t SHIFT_KEY = 0x10;
inline constexpr std::uint32_t CONTROL_KEY = 0x11;
inline constexpr std::uint32_t ALT_KEY = 0x12;

// The keyboard and mouse as the game reads them once a frame. The window procedure writes it as messages arrive; the
// game reads it and then calls EndFrame, which clears what happened only this frame: presses, releases, motion and the
// wheel.
class InputState
{
public:
  void OnKey(std::uint32_t _virtualKey, bool _down, bool _repeat) noexcept;
  void OnButton(MouseButton _button, bool _down) noexcept;
  void OnMouseMove(std::int32_t _xPixels, std::int32_t _yPixels) noexcept;

  // The pointer has left the client area: it is no longer seen until it moves in again.
  void OnPointerLeft() noexcept;
  void OnWheel(float _notches) noexcept;

  // Losing focus loses the key-up and button-up messages, so everything is released.
  void OnFocusLost() noexcept;

  [[nodiscard]] bool IsKeyDown(std::uint32_t _virtualKey) const noexcept;

  // Pressed since the last EndFrame; a key's auto-repeat does not count.
  [[nodiscard]] bool WasKeyPressed(std::uint32_t _virtualKey) const noexcept;

  [[nodiscard]] bool IsButtonDown(MouseButton _button) const noexcept;
  [[nodiscard]] bool IsAnyButtonDown() const noexcept;

  // Pressed, or released, since the last EndFrame. A click shorter than a frame is both. Losing focus releases a button
  // without its release counting.
  [[nodiscard]] bool WasButtonPressed(MouseButton _button) const noexcept;
  [[nodiscard]] bool WasButtonReleased(MouseButton _button) const noexcept;

  // Where the pointer was last seen, in physical pixels of the client area, +x right and +y down (Design/ADR/ADR-034),
  // beyond its edges while a drag holds it; and whether it has been seen since the window last lost focus or the pointer
  // last left the client area.
  [[nodiscard]] std::int32_t PointerXPixels() const noexcept
  {
    return m_mouseXPixels;
  }

  [[nodiscard]] std::int32_t PointerYPixels() const noexcept
  {
    return m_mouseYPixels;
  }

  [[nodiscard]] bool HasPointer() const noexcept
  {
    return m_mouseKnown;
  }

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
  std::array<bool, 3> m_buttonsPressed{};
  std::array<bool, 3> m_buttonsReleased{};
  std::int32_t m_mouseXPixels = 0;
  std::int32_t m_mouseYPixels = 0;
  bool m_mouseKnown = false;
  float m_deltaXPixels = 0.0f;
  float m_deltaYPixels = 0.0f;
  float m_wheelNotches = 0.0f;
};

} // namespace NeuronClient
