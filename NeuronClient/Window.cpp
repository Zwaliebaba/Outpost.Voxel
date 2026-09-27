#include "pch.h"

#include "Window.h"

namespace NeuronClient
{
namespace
{

constexpr const wchar_t* CLASS_NAME = L"NeuronClientWindow";

// The window property that holds the Window a handle belongs to.
constexpr const wchar_t* INSTANCE_PROPERTY = L"NeuronClient.Window";

// The Window whose handle CreateWindowExW is making on this thread: its first messages arrive before the handle does.
thread_local Window* g_creating = nullptr;

// Signed, because a captured pointer can leave the client area on either side.
[[nodiscard]] std::int32_t PointerX(LPARAM _lParam) noexcept
{
  return static_cast<std::int16_t>(LOWORD(static_cast<DWORD_PTR>(_lParam)));
}

[[nodiscard]] std::int32_t PointerY(LPARAM _lParam) noexcept
{
  return static_cast<std::int16_t>(HIWORD(static_cast<DWORD_PTR>(_lParam)));
}

} // namespace

Window::Window(const WindowDesc& _desc)
{
  // Sizes are physical pixels (§13). A process that has already chosen its awareness keeps it.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = &Window::Procedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = CLASS_NAME;
  if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
  {
    winrt::throw_last_error();
  }

  POINT pointer{};
  GetCursorPos(&pointer);
  MONITORINFO monitorInfo{};
  monitorInfo.cbSize = sizeof(monitorInfo);
  winrt::check_bool(GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTOPRIMARY), &monitorInfo));

  m_requestedSize = _desc.clientSize;
  const RECT& area = _desc.clientSize ? monitorInfo.rcWork : monitorInfo.rcMonitor;
  g_creating = this;
  m_window = CreateWindowExW(0, CLASS_NAME, _desc.title, _desc.clientSize ? WS_OVERLAPPEDWINDOW : WS_POPUP, area.left, area.top,
                             area.right - area.left, area.bottom - area.top, nullptr, nullptr, instance, nullptr);
  g_creating = nullptr;
  winrt::check_pointer(m_window);
  Fit(GetDpiForWindow(m_window));
  ShowWindow(m_window, SW_SHOW);
  RECT client{};
  GetClientRect(m_window, &client);
  m_size = {static_cast<std::uint32_t>(client.right - client.left), static_cast<std::uint32_t>(client.bottom - client.top)};
}

Window::~Window()
{
  if (m_window != nullptr)
  {
    RemovePropW(m_window, INSTANCE_PROPERTY);
    DestroyWindow(m_window);
  }
}

bool Window::PumpMessages()
{
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
  {
    if (message.message == WM_QUIT)
    {
      m_open = false;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return m_open;
}

void Window::SetTitle(const std::wstring& _title) const noexcept
{
  SetWindowTextW(m_window, _title.c_str());
}

LRESULT CALLBACK Window::Procedure(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept
{
  auto* self = static_cast<Window*>(GetPropW(_window, INSTANCE_PROPERTY));
  if (self == nullptr && g_creating != nullptr)
  {
    self = g_creating;
    SetPropW(_window, INSTANCE_PROPERTY, self);
  }
  if (_message == WM_NCDESTROY)
  {
    RemovePropW(_window, INSTANCE_PROPERTY);
  }
  return self != nullptr ? self->OnMessage(_window, _message, _wParam, _lParam) : DefWindowProcW(_window, _message, _wParam, _lParam);
}

LRESULT Window::OnMessage(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept
{
  switch (_message)
  {
  case WM_SIZE:
    m_size = {LOWORD(static_cast<DWORD_PTR>(_lParam)), HIWORD(static_cast<DWORD_PTR>(_lParam))};
    return 0;
  case WM_DPICHANGED:
    Fit(LOWORD(_wParam));
    return 0;
  case WM_DESTROY:
    m_window = nullptr;
    PostQuitMessage(0);
    return 0;
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    m_input.OnKey(static_cast<std::uint32_t>(_wParam), true, (static_cast<DWORD_PTR>(_lParam) & (1u << 30u)) != 0);
    break;
  case WM_KEYUP:
  case WM_SYSKEYUP:
    m_input.OnKey(static_cast<std::uint32_t>(_wParam), false, false);
    break;
  case WM_LBUTTONDOWN:
    OnButton(MouseButton::Left, true);
    return 0;
  case WM_LBUTTONUP:
    OnButton(MouseButton::Left, false);
    return 0;
  case WM_RBUTTONDOWN:
    OnButton(MouseButton::Right, true);
    return 0;
  case WM_RBUTTONUP:
    OnButton(MouseButton::Right, false);
    return 0;
  case WM_MBUTTONDOWN:
    OnButton(MouseButton::Middle, true);
    return 0;
  case WM_MBUTTONUP:
    OnButton(MouseButton::Middle, false);
    return 0;
  case WM_MOUSEMOVE:
    m_input.OnMouseMove(PointerX(_lParam), PointerY(_lParam));
    return 0;
  case WM_MOUSEWHEEL:
    m_input.OnWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(_wParam)) / static_cast<float>(WHEEL_DELTA));
    return 0;
  case WM_KILLFOCUS:
    m_input.OnFocusLost();
    break;
  default:
    break;
  }
  // The system keys pass on too, so that Alt+F4 still closes the window.
  return DefWindowProcW(_window, _message, _wParam, _lParam);
}

void Window::Fit(UINT _dpi) noexcept
{
  if (!m_requestedSize)
  {
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(MonitorFromWindow(m_window, MONITOR_DEFAULTTOPRIMARY), &monitorInfo);
    const RECT& area = monitorInfo.rcMonitor;
    SetWindowPos(m_window, nullptr, area.left, area.top, area.right - area.left, area.bottom - area.top, SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }
  // The client area stays the size asked for, in physical pixels, whatever the monitor's scale.
  RECT frame{0, 0, static_cast<LONG>(m_requestedSize->widthPixels), static_cast<LONG>(m_requestedSize->heightPixels)};
  AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, _dpi);
  SetWindowPos(m_window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void Window::OnButton(MouseButton _button, bool _down) noexcept
{
  m_input.OnButton(_button, _down);
  // A drag keeps the pointer's messages while it leaves the window, and gives them up when the last button is released.
  if (_down)
  {
    SetCapture(m_window);
  }
  else if (!m_input.IsAnyButtonDown())
  {
    ReleaseCapture();
  }
}

} // namespace NeuronClient
