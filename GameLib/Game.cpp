#include "pch.h"

#include "Game.h"

#include "Canvas.h"
#include "Clock.h"
#include "FailureReport.h"
#include "InputState.h"
#include "Renderer.h"

#include "OrbitCamera.h"
#include "Scene.h"

#include "DebugView.h"
#include "Explosion.h"
#include "Lighting.h"
#include "OrthographicView.h"
#include "RenderSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace GameLib
{
namespace
{

using NeuronClient::InputState;
using NeuronClient::MouseButton;

// Fly speed in fractions of the framing distance per second; Shift quadruples it.
constexpr float FLY_SPEED_PER_SECOND = 0.25f;
constexpr float FLY_BOOST = 4.0f;
// How often the frame time is brought up to date, in the title and on screen; it is the mean over that interval.
constexpr double TITLE_INTERVAL_SECONDS = 0.5;

// The figures on screen (§13, ADR-010): one per line on a translucent panel in the top-left corner, at these sizes on a
// 96 DPI monitor and larger in proportion on a denser one.
constexpr NeuronClient::TextStyle FIGURES_STYLE{L"Consolas", 15.0f, DWRITE_FONT_WEIGHT_NORMAL};
constexpr float FIGURES_MARGIN_PIXELS = 8.0f;
constexpr float FIGURES_PADDING_PIXELS = 6.0f;
constexpr float FIGURES_PANEL_ALPHA = 0.55f;

// [ and ] scale the emissive gain by this much, within these bounds (§7.2: the multiplier is tuned by eye).
constexpr float EMISSIVE_STEP = 1.1f;
constexpr float EMISSIVE_GAIN_MINIMUM = 0.01f;
constexpr float EMISSIVE_GAIN_MAXIMUM = 100.0f;

// + and - scale the explosion's time by this much, within these bounds (§13).
constexpr float TIME_SCALE_STEP = 2.0f;
constexpr float TIME_SCALE_MINIMUM = 1.0f / 16.0f;
constexpr float TIME_SCALE_MAXIMUM = 8.0f;

// Keys 2 to 5 choose these debug views; key 1 returns to the lit image.
constexpr std::array<const wchar_t*, NeuronCore::DEBUG_VIEW_COUNT> DEBUG_VIEW_NAMES{L"albedo", L"normal", L"voxel index", L"shadow map"};

constexpr const wchar_t* KEY_MAP = L"Left drag\torbit (fly mode: look)\n"
                                   L"Right drag\tpan\n"
                                   L"Wheel\tdolly\n"
                                   L"F\tframe the model, or the explosion's reach once it has started\n"
                                   L"Tab\tfly mode: W A S D move, Page Down and Page Up sink and rise, Shift faster\n"
                                   L"E\tdetonate\n"
                                   L"R\treassemble\n"
                                   L"Space\tpause\n"
                                   L"+ -\ttime faster, slower\n"
                                   L"1\tthe lit image\n"
                                   L"2 - 5\talbedo, normal, voxel index, shadow map\n"
                                   L"[ ]\temissive glow down, up\n"
                                   L"G\tground\n"
                                   L"V\tvsync\n"
                                   L"F1\tthis key map\n"
                                   L"F2\tthe figures on screen\n"
                                   L"Alt+F4\tquit";

struct Controls
{
  std::optional<NeuronCore::DebugView> debugView; // empty: the lit image
  bool ground = true;
  bool vsync = true;
  bool figures = true; // on screen; the title always carries them
  float emissiveGain = 1.0f;
};

// The explosion's time (§12, §13): E runs it forward from wherever it is, R runs it back to the intact model, Space
// pauses it, and + and - scale it. It stops at the time by which every voxel rests, so that reassembly never takes
// longer than the explosion did.
struct ExplosionClock
{
  float seconds = 0.0f;   // since the detonation; 0 is the intact model
  float direction = 0.0f; // 1 forward, -1 back, 0 still
  float scale = 1.0f;
  bool paused = false;
};

void RunClock(ExplosionClock& _clock, const InputState& _input, float _restSeconds, float _elapsedSeconds)
{
  if (_input.WasKeyPressed('E'))
  {
    _clock.direction = 1.0f;
    _clock.paused = false;
  }
  if (_input.WasKeyPressed('R'))
  {
    _clock.direction = -1.0f;
    _clock.paused = false;
  }
  if (_input.WasKeyPressed(VK_SPACE))
  {
    _clock.paused = !_clock.paused;
  }
  if (_input.WasKeyPressed(VK_OEM_PLUS) || _input.WasKeyPressed(VK_ADD))
  {
    _clock.scale = std::min(_clock.scale * TIME_SCALE_STEP, TIME_SCALE_MAXIMUM);
  }
  if (_input.WasKeyPressed(VK_OEM_MINUS) || _input.WasKeyPressed(VK_SUBTRACT))
  {
    _clock.scale = std::max(_clock.scale / TIME_SCALE_STEP, TIME_SCALE_MINIMUM);
  }
  if (!_clock.paused)
  {
    _clock.seconds = std::clamp(_clock.seconds + _clock.direction * _clock.scale * _elapsedSeconds, 0.0f, _restSeconds);
  }
  if (_clock.seconds == 0.0f && _clock.direction < 0.0f)
  {
    _clock.direction = 0.0f;
  }
}

// The sphere around a box, which the camera frames.
struct Sphere
{
  NeuronCore::Float3 center;
  float radius;
};

void Steer(OrbitCamera& _camera, const Sphere& _framed, const InputState& _input, std::uint32_t _heightPixels, float _seconds)
{
  if (_input.WasKeyPressed('F'))
  {
    _camera.Frame(_framed.center, _framed.radius);
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
    _camera.Fly(axis('W', 'S') * step, axis('D', 'A') * step, axis(VK_PRIOR, VK_NEXT) * step);
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
  if (_input.WasKeyPressed(VK_F2))
  {
    _controls.figures = !_controls.figures;
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

// What the title and the panel on screen carry (§13): the adapter, the view, the mean frame time once there is one, and
// whatever else is not at its default.
[[nodiscard]] std::vector<std::wstring> Figures(const NeuronClient::GraphicsDevice& _device, const Controls& _controls,
                                                const ExplosionClock& _clock, std::optional<double> _frameSeconds, float _brightestEmissive)
{
  std::vector<std::wstring> figures{_device.AdapterName(),
                                    _controls.debugView ? DEBUG_VIEW_NAMES[static_cast<std::size_t>(*_controls.debugView)] : L"lit"};
  if (_frameSeconds)
  {
    figures.push_back(std::format(L"frame {:.2f} ms", *_frameSeconds * 1000.0));
  }
  if (_brightestEmissive > 0.0f)
  {
    figures.push_back(std::format(L"emissive {:.2f}", _brightestEmissive * _controls.emissiveGain));
  }
  if (_clock.seconds > 0.0f)
  {
    figures.push_back(std::format(L"t {:.2f} s", _clock.seconds));
  }
  if (_clock.scale != 1.0f)
  {
    figures.push_back(std::format(L"time x{}", _clock.scale));
  }
  if (_clock.paused)
  {
    figures.emplace_back(L"paused");
  }
  if (!_controls.vsync)
  {
    figures.emplace_back(L"vsync off");
  }
  if (_device.DebugLayer() == NeuronClient::DebugLayerState::Unavailable)
  {
    figures.emplace_back(L"debug layer unavailable");
  }
  return figures;
}

[[nodiscard]] std::wstring Joined(const std::vector<std::wstring>& _figures, std::wstring_view _separator)
{
  std::wstring joined;
  for (std::size_t i = 0; i < _figures.size(); ++i)
  {
    if (i > 0)
    {
      joined += _separator;
    }
    joined += _figures[i];
  }
  return joined;
}

// The figures on the canvas, one per line, white on a translucent black panel in the top-left corner. _scale is the
// monitor's DPI over 96.
void DrawFigures(NeuronClient::Canvas& _canvas, const std::vector<std::wstring>& _figures, float _scale)
{
  const NeuronClient::TextStyle style{FIGURES_STYLE.fontFamily, FIGURES_STYLE.sizePixels * _scale, FIGURES_STYLE.weight};
  const std::wstring text = Joined(_figures, L"\n");
  const NeuronClient::TextExtent extent = _canvas.Measure(text, style);
  const float margin = std::round(FIGURES_MARGIN_PIXELS * _scale);
  const float padding = std::round(FIGURES_PADDING_PIXELS * _scale);
  _canvas.FillRectangle(static_cast<std::int32_t>(margin), static_cast<std::int32_t>(margin),
                        static_cast<std::uint32_t>(std::ceil(extent.widthPixels + 2.0f * padding)),
                        static_cast<std::uint32_t>(std::ceil(extent.heightPixels + 2.0f * padding)), {0.0f, 0.0f, 0.0f},
                        FIGURES_PANEL_ALPHA);
  _canvas.Print(text, margin + padding, margin + padding, style, {1.0f, 1.0f, 1.0f}, 1.0f);
}

// The sun's view (§10): fitted once to the explosion's envelope, which holds the model's box and reaches the ground, so
// that it never moves and shadows do not swim.
[[nodiscard]] NeuronCore::OrthographicView FitShadowView(const Scene& _scene, const NeuronCore::RenderSettings& _settings,
                                                         const NeuronCore::ExplosionEnvelope& _envelope) noexcept
{
  const NeuronCore::Float3 toSun = NeuronCore::SunDirection(_settings.sunElevationRadians, _settings.sunAzimuthRadians);
  return NeuronCore::MakeShadowView(toSun, _scene.center, NeuronCore::SHADOW_HALF_EXTENT, _envelope.lower, _envelope.upper,
                                    NeuronCore::SHADOW_MAP_PIXELS);
}

[[nodiscard]] Sphere SphereAround(NeuronCore::Float3 _lower, NeuronCore::Float3 _upper) noexcept
{
  const NeuronCore::Float3 center = (_lower + _upper) * 0.5f;
  return {center, NeuronCore::Length(_upper - center)};
}

} // namespace

void RunGame(const GameOptions& _options)
{
  const Scene scene = LoadScene(_options.voxPath);
  const NeuronCore::RenderSettings settings = NeuronCore::ReadRenderSettings(scene.model.renderObjects);
  const float brightestEmissive = BrightestEmissiveScale(scene.model);
  const NeuronCore::ExplosionParameters explosion = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(scene.model));
  const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(explosion, scene.lower, scene.upper);
  NeuronClient::Window window({L"Outpost", _options.windowSize});
  const NeuronClient::ClientSize size = window.Size();
  NeuronClient::Renderer renderer(
    {_options.device, window.Handle(), size.widthPixels, size.heightPixels, FitShadowView(scene, settings, envelope), explosion},
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
    ExplosionClock explosionClock;
    const Sphere intact{scene.center, scene.radius};
    const Sphere exploded = SphereAround(envelope.lower, envelope.upper);
    NeuronClient::Clock clock;
    double sinceTitleSeconds = 0.0;
    std::uint32_t framesSinceTitle = 0;
    std::optional<double> frameSeconds;
    while (window.PumpMessages())
    {
      const double seconds = clock.Tick();
      const NeuronClient::ClientSize current = window.Size();
      NeuronClient::InputState& input = window.Input();
      Steer(camera, explosionClock.seconds > 0.0f ? exploded : intact, input, current.heightPixels, static_cast<float>(seconds));
      Choose(controls, input, window.Handle());
      RunClock(explosionClock, input, envelope.restTimeSeconds, static_cast<float>(seconds));
      input.EndFrame();
      if (current.widthPixels == 0 || current.heightPixels == 0)
      {
        // Minimized: nothing to draw until a message says something changed.
        WaitMessage();
        continue;
      }
      sinceTitleSeconds += seconds;
      ++framesSinceTitle;
      const bool titleDue = sinceTitleSeconds >= TITLE_INTERVAL_SECONDS;
      if (titleDue)
      {
        frameSeconds = sinceTitleSeconds / framesSinceTitle;
        sinceTitleSeconds = 0.0;
        framesSinceTitle = 0;
      }
      renderer.Resize(current.widthPixels, current.heightPixels);
      const std::vector<std::wstring> figures = Figures(renderer.Device(), controls, explosionClock, frameSeconds, brightestEmissive);
      if (titleDue)
      {
        window.SetTitle(L"Outpost - " + Joined(figures, L" - "));
      }
      if (controls.figures)
      {
        DrawFigures(renderer.Overlay(), figures, static_cast<float>(GetDpiForWindow(window.Handle())) / USER_DEFAULT_SCREEN_DPI);
      }
      NeuronCore::LightingParameters lighting = NeuronCore::MakeLightingParameters(settings, controls.emissiveGain);
      lighting.groundVisible = controls.ground;
      renderer.Render(camera.View(current.widthPixels, current.heightPixels),
                      {controls.debugView, lighting, settings.exposure, explosionClock.seconds, controls.vsync});
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
