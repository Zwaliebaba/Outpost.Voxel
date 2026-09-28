#pragma once

#include "WindowsSdk.h"

#include "InputState.h"

#include <cstdint>
#include <optional>
#include <string>

namespace NeuronClient
{

struct ClientSize
{
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
};

struct WindowDesc
{
  const wchar_t* title;
  // Without a size, the window is borderless fullscreen on the monitor the mouse is on (Design/Archive/SampleRenderer.md §13);
  // with one, it is an ordinary window with that client area.
  std::optional<ClientSize> clientSize;
};

// The application's one window: per-monitor DPI aware, so every size is in physical pixels. Its window procedure feeds
// the input state; Alt+F4 closes it, as it closes any window.
class Window
{
public:
  explicit Window(const WindowDesc& _desc);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;

  // Handles every message waiting. False once the window has been closed.
  [[nodiscard]] bool PumpMessages();

  [[nodiscard]] HWND Handle() const noexcept
  {
    return m_window;
  }

  // The client area; zero while the window is minimized.
  [[nodiscard]] ClientSize Size() const noexcept
  {
    return m_size;
  }

  [[nodiscard]] InputState& Input() noexcept
  {
    return m_input;
  }

  void SetTitle(const std::wstring& _title) const noexcept;

private:
  static LRESULT CALLBACK Procedure(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept;
  LRESULT OnMessage(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept;
  void OnButton(MouseButton _button, bool _down) noexcept;

  // Covers the monitor the window is on, or frames the client size asked for at _dpi.
  void Fit(UINT _dpi) noexcept;

  HWND m_window = nullptr;
  std::optional<ClientSize> m_requestedSize; // absent: borderless fullscreen
  ClientSize m_size{};
  InputState m_input;
  bool m_open = true;
};

} // namespace NeuronClient
