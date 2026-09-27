#include "pch.h"

#include "Game.h"

#include "Clock.h"
#include "FailureReport.h"
#include "InputState.h"
#include "Renderer.h"

#include "OrbitCamera.h"
#include "Scene.h"

#include "DebugView.h"
#include "Lighting.h"
#include "OrthographicView.h"
#include "RenderSettings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
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

// The shadow map of §10: 4096 texels across a 1,024-unit square, four to a voxel's edge.
constexpr std::uint32_t SHADOW_MAP_PIXELS = 4096;
constexpr float SHADOW_HALF_EXTENT = 512.0f;

// [ and ] scale the emissive gain by this much, within these bounds (§7.2: the multiplier is tuned by eye).
constexpr float EMISSIVE_STEP = 1.1f;
constexpr float EMISSIVE_GAIN_MINIMUM = 0.01f;
constexpr float EMISSIVE_GAIN_MAXIMUM = 100.0f;

// Keys 2 to 5 choose these debug views; key 1 returns to the lit image.
constexpr std::array<const wchar_t*, NeuronCore::DEBUG_VIEW_COUNT> DEBUG_VIEW_NAMES{L"albedo", L"normal", L"voxel index", L"shadow map"};

constexpr const wchar_t* KEY_MAP = L"Left drag\torbit (fly mode: look)\n"
                                   L"Right drag\tpan\n"
                                   L"Wheel\tdolly\n"
                                   L"F\tframe the model\n"
                                   L"Tab\tfly mode: W A S D move, Q E down and up, Shift faster\n"
                                   L"1\tthe lit image\n"
                                   L"2 - 5\talbedo, normal, voxel index, shadow map\n"
                                   L"[ ]\temissive glow down, up\n"
                                   L"G\tground\n"
                                   L"V\tvsync\n"
                                   L"F1\tthis key map\n"
                                   L"Alt+F4\tquit";

struct Controls
{
  std::optional<NeuronCore::DebugView> debugView; // empty: the lit image
  bool ground = true;
  bool vsync = true;
  float emissiveGain = 1.0f;
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
  if (_input.WasKeyPressed('1'))
  {
    _controls.debugView.reset();
  }
  for (std::uint32_t view = 0; view < NeuronCore::DEBUG_VIEW_COUNT; ++view)
  {
    if (_input.WasKeyPressed('2' + view))
    {
      _controls.debugView = static_cast<NeuronCore::DebugView>(view);
    }
  }
  // [ and ] on a US layout, where most keyboards put them.
  if (_input.WasKeyPressed(VK_OEM_4))
  {
    _controls.emissiveGain = std::max(_controls.emissiveGain / EMISSIVE_STEP, EMISSIVE_GAIN_MINIMUM);
  }
  if (_input.WasKeyPressed(VK_OEM_6))
  {
    _controls.emissiveGain = std::min(_controls.emissiveGain * EMISSIVE_STEP, EMISSIVE_GAIN_MAXIMUM);
  }
  if (_input.WasKeyPressed('G'))
  {
    _controls.ground = !_controls.ground;
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

// The largest emissive scale in the palette: what the title reports, times the gain, as the glow the viewer tunes.
[[nodiscard]] float BrightestEmissiveScale(const NeuronCore::VoxModel& _model) noexcept
{
  float brightest = 0.0f;
  for (const NeuronCore::PaletteEntry& entry : _model.palette)
  {
    brightest = std::max(brightest, NeuronCore::EmissiveScale(entry));
  }
  return brightest;
}

[[nodiscard]] std::wstring Title(const NeuronClient::GraphicsDevice& _device, const Controls& _controls, double _frameSeconds,
                                 float _brightestEmissive)
{
  const wchar_t* view = _controls.debugView ? DEBUG_VIEW_NAMES[static_cast<std::size_t>(*_controls.debugView)] : L"lit";
  std::wstring title = std::format(L"Outpost - {} - {} - {:.2f} ms", _device.AdapterName(), view, _frameSeconds * 1000.0);
  if (_brightestEmissive > 0.0f)
  {
    title += std::format(L" - emissive {:.2f}", _brightestEmissive * _controls.emissiveGain);
  }
  if (!_controls.vsync)
  {
    title += L" - vsync off";
  }
  if (_device.DebugLayer() == NeuronClient::DebugLayerState::Unavailable)
  {
    title += L" - debug layer unavailable";
  }
  return title;
}

// The sun's view (§10): fitted once to the model's box, grown down to the ground, so that it never moves.
[[nodiscard]] NeuronCore::OrthographicView FitShadowView(const Scene& _scene, const NeuronCore::RenderSettings& _settings) noexcept
{
  const NeuronCore::Float3 toSun = NeuronCore::SunDirection(_settings.sunElevationRadians, _settings.sunAzimuthRadians);
  const NeuronCore::Float3 lower{_scene.lower.x, _scene.lower.y, std::min(_scene.lower.z, 0.0f)};
  return NeuronCore::MakeShadowView(toSun, _scene.center, SHADOW_HALF_EXTENT, lower, _scene.upper, SHADOW_MAP_PIXELS);
}

} // namespace

void RunGame(const GameOptions& _options)
{
  const Scene scene = LoadScene(_options.voxPath);
  const NeuronCore::RenderSettings settings = NeuronCore::ReadRenderSettings(scene.model.renderObjects);
  const float brightestEmissive = BrightestEmissiveScale(scene.model);
  NeuronClient::Window window({L"Outpost", _options.windowSize});
  const NeuronClient::ClientSize size = window.Size();
  NeuronClient::Renderer renderer({_options.device, window.Handle(), size.widthPixels, size.heightPixels, FitShadowView(scene, settings)},
                                  scene.model);
  try
  {
    // A borderless window has no title bar to show it (§13), so the debugger's output says it too.
    if (renderer.Device().DebugLayer() == NeuronClient::DebugLayerState::Unavailable)
    {
      OutputDebugStringW(L"The Direct3D 12 debug layer is not installed; running without it.\n");
    }
    OrbitCamera camera(scene.center, scene.radius);
    Controls controls;
    controls.ground = settings.groundVisible;
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
      NeuronCore::LightingParameters lighting = NeuronCore::MakeLightingParameters(settings, controls.emissiveGain);
      lighting.groundVisible = controls.ground;
      renderer.Render(camera.View(current.widthPixels, current.heightPixels),
                      {controls.debugView, lighting, settings.exposure, controls.vsync});
      sinceTitleSeconds += seconds;
      ++framesSinceTitle;
      if (sinceTitleSeconds >= TITLE_INTERVAL_SECONDS)
      {
        window.SetTitle(Title(renderer.Device(), controls, sinceTitleSeconds / framesSinceTitle, brightestEmissive));
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
