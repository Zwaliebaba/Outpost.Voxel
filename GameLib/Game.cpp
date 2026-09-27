#include "pch.h"

#include "Game.h"

#include "Clock.h"
#include "FailureReport.h"
#include "InputState.h"
#include "Renderer.h"

#include "OrbitCamera.h"
#include "Scene.h"

#include "DebugView.h"

#include <array>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

namespace GameLib
{
namespace
{

using NeuronClient::InputState;
using NeuronClient::MouseButton;

// Fly speed in fractions of the framing distance per second; Shift quadruples it.
constexpr float FLY_SPEED_PER_SECOND = 0.25f;
constexpr float FLY_BOOST = 4.0f;
// How often the title's figures are brought up to date; the frame time it shows is the mean over that interval.
constexpr double TITLE_INTERVAL_SECONDS = 0.5;

constexpr std::array<const wchar_t*, NeuronCore::DEBUG_VIEW_COUNT> VIEW_NAMES{L"headlight", L"albedo", L"normal", L"voxel index"};

constexpr const wchar_t* KEY_MAP = L"Left drag\torbit (fly mode: look)\n"
                                   L"Right drag\tpan\n"
                                   L"Wheel\tdolly\n"
                                   L"F\tframe the station\n"
                                   L"Tab\tfly mode: W A S D move, Q E down and up, Shift faster\n"
                                   L"1 - 4\theadlight, albedo, normal, voxel index\n"
                                   L"V\tvsync\n"
                                   L"F1\tthis key map\n"
                                   L"Alt+F4\tquit";

struct Controls
{
  NeuronCore::DebugView view = NeuronCore::DebugView::Headlight;
  bool vsync = true;
};

void Steer(OrbitCamera& _camera, const Scene& _scene, const InputState& _input, std::uint32_t _heightPixels, float _seconds)
{
  if (_input.WasKeyPressed('F'))
  {
    _camera.Frame(_scene.center, _scene.radius);
  }
  if (_input.WasKeyPressed(VK_TAB))
  {
    _camera.ToggleFlying();
  }
  if (_input.IsButtonDown(MouseButton::Left))
  {
    _camera.Orbit(_input.MouseDeltaXPixels(), _input.MouseDeltaYPixels());
  }
  if (_input.IsButtonDown(MouseButton::Right))
  {
    _camera.Pan(_input.MouseDeltaXPixels(), _input.MouseDeltaYPixels(), _heightPixels);
  }
  _camera.Dolly(_input.WheelNotches());
  if (_camera.IsFlying())
  {
    const float step = FLY_SPEED_PER_SECOND * _camera.Distance() * _seconds * (_input.IsKeyDown(VK_SHIFT) ? FLY_BOOST : 1.0f);
    const auto axis = [&_input](std::uint32_t _positive, std::uint32_t _negative)
    { return (_input.IsKeyDown(_positive) ? 1.0f : 0.0f) - (_input.IsKeyDown(_negative) ? 1.0f : 0.0f); };
    _camera.Fly(axis('W', 'S') * step, axis('D', 'A') * step, axis('E', 'Q') * step);
  }
}

void Choose(Controls& _controls, const InputState& _input, HWND _window)
{
  for (std::uint32_t view = 0; view < NeuronCore::DEBUG_VIEW_COUNT; ++view)
  {
    if (_input.WasKeyPressed('1' + view))
    {
      _controls.view = static_cast<NeuronCore::DebugView>(view);
    }
  }
  if (_input.WasKeyPressed('V'))
  {
    _controls.vsync = !_controls.vsync;
  }
  if (_input.WasKeyPressed(VK_F1))
  {
    MessageBoxW(_window, KEY_MAP, L"Outpost keys", MB_OK | MB_ICONINFORMATION);
  }
}

[[nodiscard]] std::wstring Title(const NeuronClient::GraphicsDevice& _device, const Controls& _controls, double _frameSeconds)
{
  std::wstring title =
    std::format(L"Outpost - {} - {} - {:.2f} ms{}", _device.AdapterName(), VIEW_NAMES[static_cast<std::size_t>(_controls.view)],
                _frameSeconds * 1000.0, _controls.vsync ? L"" : L" - vsync off");
  if (_device.DebugLayer() == NeuronClient::DebugLayerState::Unavailable)
  {
    title += L" - debug layer unavailable";
  }
  return title;
}

} // namespace

void RunGame(const GameOptions& _options)
{
  const Scene scene = LoadScene(_options.voxPath);
  NeuronClient::Window window({L"Outpost", _options.windowSize});
  const NeuronClient::ClientSize size = window.Size();
  NeuronClient::Renderer renderer({_options.device, window.Handle(), size.widthPixels, size.heightPixels}, scene.model);
  try
  {
    // A borderless window has no title bar to show it (§13), so the debugger's output says it too.
    if (renderer.Device().DebugLayer() == NeuronClient::DebugLayerState::Unavailable)
    {
      OutputDebugStringW(L"The Direct3D 12 debug layer is not installed; running without it.\n");
    }
    OrbitCamera camera(scene.center, scene.radius);
    Controls controls;
    NeuronClient::Clock clock;
    double sinceTitleSeconds = 0.0;
    std::uint32_t framesSinceTitle = 0;
    while (window.PumpMessages())
    {
      const double seconds = clock.Tick();
      const NeuronClient::ClientSize current = window.Size();
      NeuronClient::InputState& input = window.Input();
      Steer(camera, scene, input, current.heightPixels, static_cast<float>(seconds));
      Choose(controls, input, window.Handle());
      input.EndFrame();
      if (current.widthPixels == 0 || current.heightPixels == 0)
      {
        // Minimized: nothing to draw until a message says something changed.
        WaitMessage();
        continue;
      }
      renderer.Resize(current.widthPixels, current.heightPixels);
      renderer.Render(camera.View(current.widthPixels, current.heightPixels), controls.view, controls.vsync);
      sinceTitleSeconds += seconds;
      ++framesSinceTitle;
      if (sinceTitleSeconds >= TITLE_INTERVAL_SECONDS)
      {
        window.SetTitle(Title(renderer.Device(), controls, sinceTitleSeconds / framesSinceTitle));
        sinceTitleSeconds = 0.0;
        framesSinceTitle = 0;
      }
    }
  }
  catch (...)
  {
    // The device still exists here, so a removal can still be asked about.
    std::string message = NeuronClient::DescribeCurrentException();
    const std::string removal = renderer.Device().DescribeRemoval();
    if (!removal.empty())
    {
      message += "\n" + removal;
    }
    throw std::runtime_error(message);
  }
}

} // namespace GameLib
