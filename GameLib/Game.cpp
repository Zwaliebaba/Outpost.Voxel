#include "pch.h"

#include "Game.h"

#include "Canvas.h"
#include "ClientSession.h"
#include "Clock.h"
#include "FailureReport.h"
#include "FrameQueries.h"
#include "InputState.h"
#include "Renderer.h"
#include "SnapshotBuffer.h"

#include "ChaseCamera.h"
#include "OrbitCamera.h"
#include "Scene.h"

#include "DebugView.h"
#include "Lighting.h"
#include "Message.h"
#include "Quaternion.h"
#include "RigidTransform.h"
#include "Sky.h"
#include "Sphere.h"
#include "StarCatalog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace GameLib
{
namespace
{

using NeuronClient::InputState;
using NeuronClient::MouseButton;
using NeuronClient::SampledEntity;
using NeuronClient::WorldSample;

// Fly speed in fractions of the framing distance per second; Shift quadruples it.
constexpr float FLY_SPEED_PER_SECOND = 0.25f;
constexpr float FLY_BOOST = 4.0f;
// How often the frame time is brought up to date, in the title and on screen; it is the mean over that interval.
constexpr double TITLE_INTERVAL_SECONDS = 0.5;

// How long the client waits for its server's welcome and first snapshot, which a server on its own thread sends within a
// tick, and how often it looks.
constexpr double CONNECT_SECONDS = 10.0;
constexpr std::chrono::milliseconds CONNECT_POLL{1};

// While the window is minimized, how long the client sleeps between taking the snapshots that keep arriving.
constexpr DWORD MINIMIZED_WAIT_MILLISECONDS = 100;

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

// Keys 2 to 6 choose these debug views; key 1 returns to the lit image.
constexpr std::array<const wchar_t*, NeuronCore::DEBUG_VIEW_COUNT> DEBUG_VIEW_NAMES{L"albedo", L"normal", L"voxel index", L"shadow map",
                                                                                    L"overdraw"};

// The passes the title and the panel time, by NeuronClient::GpuPass.
constexpr std::array<const wchar_t*, NeuronClient::GPU_PASS_COUNT> GPU_PASS_NAMES{
  L"shadow splat", L"view splat", L"coverage", L"lighting", L"sky", L"bloom", L"tone map", L"debug view", L"canvas"};

constexpr const wchar_t* KEY_MAP = L"Left drag\torbit (fly mode: look)\n"
                                   L"Right drag\tpan\n"
                                   L"Wheel\tdolly\n"
                                   L"N, B\tthe next or the previous target\n"
                                   L"F\tframe the target\n"
                                   L"C\tchase the target\n"
                                   L"Tab\tfly mode: W A S D move, Page Down and Page Up sink and rise, Shift faster\n"
                                   L"E\tdetonate the target\n"
                                   L"R\trestore the target\n"
                                   L"Space\tpause or resume the server\n"
                                   L"1\tthe lit image\n"
                                   L"2 - 6\talbedo, normal, voxel index, shadow map, overdraw\n"
                                   L"[ ]\temissive glow down, up\n"
                                   L"V\tvsync\n"
                                   L"F1\tthis key map\n"
                                   L"F2\tthe figures on screen\n"
                                   L"Alt+F4\tquit";

struct Controls
{
  std::optional<NeuronCore::DebugView> debugView; // empty: the lit image
  bool vsync = true;
  bool figures = true; // on screen; the title always carries them
  float emissiveGain = 1.0f;
};

// The camera of §13: an orbit about the target entity, which follows it as it moves; a chase camera behind it; or free
// flight. N and B choose the target. The design has them cycle the stations and the flights' leaders, but no flight
// crosses the wire (§5.1), so until the owner says otherwise they cycle every entity in the order of their ids.
struct Camera
{
  OrbitCamera orbit;
  ChaseCamera chase;
  bool chasing = false;
  std::uint32_t target = 0;                   // the entity's id; 0 when there is none
  std::optional<NeuronCore::Float3> followed; // where the target was in the frame before

  [[nodiscard]] NeuronCore::PerspectiveView View(std::uint32_t _widthPixels, std::uint32_t _heightPixels) const noexcept
  {
    return chasing ? chase.View(_widthPixels, _heightPixels) : orbit.View(_widthPixels, _heightPixels);
  }
};

// The GPU's figures over one title interval (§8, §13): the mean of each pass over the frames that ran it, of the whole
// frame, and of the view splat's pixel-shader invocations; and the latest frame's draw counts (Design/SpaceScene.md §7.4).
struct GpuFigures
{
  std::array<double, NeuronClient::GPU_PASS_COUNT> passMilliseconds{};
  std::array<std::uint32_t, NeuronClient::GPU_PASS_COUNT> passFrames{};
  double gpuMilliseconds = 0.0;
  double pixelShaderInvocations = 0.0;
  NeuronClient::DrawCounts draws{};
  std::uint32_t frames = 0;

  void Add(const NeuronClient::FrameStatistics& _statistics) noexcept
  {
    for (std::size_t pass = 0; pass < NeuronClient::GPU_PASS_COUNT; ++pass)
    {
      if (const std::optional<float>& milliseconds = _statistics.passMilliseconds[pass]; milliseconds.has_value())
      {
        passMilliseconds[pass] += *milliseconds;
        ++passFrames[pass];
      }
    }
    gpuMilliseconds += _statistics.gpuMilliseconds;
    pixelShaderInvocations += static_cast<double>(_statistics.pixelShaderInvocations);
    draws = _statistics.draws;
    ++frames;
  }
};

// What the frame shows of the world (§13): the server's clock and how far behind it the frame is drawn, the entities
// and the detonations in progress, and the camera's target.
struct WorldFigures
{
  std::uint64_t tick;
  std::uint32_t tickRate;
  double behindMilliseconds;
  std::size_t entities;
  std::size_t detonations;
  bool paused;
  std::uint32_t target;
  bool chasing;
};

void ThrowOnRefusal(const std::expected<void, NeuronClient::SessionError>& _polled)
{
  if (!_polled)
  {
    throw std::runtime_error("The session with the server ended: " + NeuronClient::DescribeSessionError(_polled.error()));
  }
}

// Takes messages until the welcome and the first snapshot have come, which a server on its own thread sends within a
// tick; throws, saying why, when the session is refused or nothing comes for CONNECT_SECONDS.
template <typename Now> void AwaitWorld(NeuronClient::ClientSession& _session, const Now& _now)
{
  const double deadline = _now() + CONNECT_SECONDS;
  for (;;)
  {
    ThrowOnRefusal(_session.Poll(_now()));
    if (_session.IsWelcomed() && !_session.Buffer().IsEmpty())
    {
      return;
    }
    if (_now() > deadline)
    {
      throw std::runtime_error(std::format("The server sent no world within {} seconds.", CONNECT_SECONDS));
    }
    std::this_thread::sleep_for(CONNECT_POLL);
  }
}

[[nodiscard]] const SampledEntity* FindEntity(const WorldSample& _sample, std::uint32_t _id) noexcept
{
  const auto found = std::ranges::lower_bound(_sample.entities, _id, {}, &SampledEntity::id);
  return found != _sample.entities.end() && found->id == _id ? &*found : nullptr;
}

// The entity after _id in the order of their ids, or before it when _backward, round from the last to the first: what N
// and B choose. Nothing when there is no entity.
[[nodiscard]] const SampledEntity* NextEntity(const WorldSample& _sample, std::uint32_t _id, bool _backward) noexcept
{
  if (_sample.entities.empty())
  {
    return nullptr;
  }
  if (!_backward)
  {
    const auto after = std::ranges::upper_bound(_sample.entities, _id, {}, &SampledEntity::id);
    return after == _sample.entities.end() ? &_sample.entities.front() : &*after;
  }
  const auto from = std::ranges::lower_bound(_sample.entities, _id, {}, &SampledEntity::id);
  return from == _sample.entities.begin() ? &_sample.entities.back() : &*std::prev(from);
}

void Steer(Camera& _camera, const Scene& _scene, const WorldSample& _sample, const InputState& _input, std::uint32_t _heightPixels,
           float _seconds)
{
  const SampledEntity* target = FindEntity(_sample, _camera.target);
  const bool next = _input.WasKeyPressed('N');
  if (next || _input.WasKeyPressed('B'))
  {
    if (const SampledEntity* chosen = NextEntity(_sample, _camera.target, !next); chosen != nullptr)
    {
      target = chosen;
      _camera.target = chosen->id;
      _camera.followed.reset();
      const NeuronCore::Sphere extent = _scene.Models().Extent(*chosen);
      _camera.orbit.Frame(extent.center, extent.radius);
      if (_camera.chasing)
      {
        _camera.chase.Reset(chosen->position, NeuronCore::RotationOf(chosen->rotation), _scene.Models().Radius(chosen->modelIndex));
      }
    }
  }
  if (_input.WasKeyPressed('F') && target != nullptr)
  {
    const NeuronCore::Sphere extent = _scene.Models().Extent(*target);
    _camera.orbit.Frame(extent.center, extent.radius);
    _camera.chasing = false;
  }
  if (_input.WasKeyPressed('C') && target != nullptr)
  {
    _camera.chasing = !_camera.chasing;
    _camera.chase.Reset(target->position, NeuronCore::RotationOf(target->rotation), _scene.Models().Radius(target->modelIndex));
  }
  if (_input.WasKeyPressed(VK_TAB))
  {
    _camera.chasing = false;
    _camera.orbit.ToggleFlying();
  }

  // The orbit keeps its place relative to the target as the target moves; the chase camera rides behind it. A target
  // that has gone leaves the camera where it is.
  if (target != nullptr)
  {
    if (_camera.followed.has_value() && !_camera.orbit.IsFlying())
    {
      _camera.orbit.MoveTarget(target->position - _camera.followed.value());
    }
    _camera.followed = target->position;
    if (_camera.chasing)
    {
      _camera.chase.Follow(target->position, NeuronCore::RotationOf(target->rotation), _scene.Models().Radius(target->modelIndex),
                           _seconds);
    }
  }
  else
  {
    _camera.chasing = false;
    _camera.followed.reset();
  }
  if (_camera.chasing)
  {
    return;
  }

  OrbitCamera& orbit = _camera.orbit;
  if (_input.IsButtonDown(MouseButton::Left))
  {
    orbit.Orbit(_input.MouseDeltaXPixels(), _input.MouseDeltaYPixels());
  }
  if (_input.IsButtonDown(MouseButton::Right))
  {
    orbit.Pan(_input.MouseDeltaXPixels(), _input.MouseDeltaYPixels(), _heightPixels);
  }
  orbit.Dolly(_input.WheelNotches());
  if (orbit.IsFlying())
  {
    const float step = FLY_SPEED_PER_SECOND * orbit.Distance() * _seconds * (_input.IsKeyDown(VK_SHIFT) ? FLY_BOOST : 1.0f);
    const auto axis = [&_input](std::uint32_t _positive, std::uint32_t _negative)
    { return (_input.IsKeyDown(_positive) ? 1.0f : 0.0f) - (_input.IsKeyDown(_negative) ? 1.0f : 0.0f); };
    orbit.Fly(axis('W', 'S') * step, axis('D', 'A') * step, axis(VK_PRIOR, VK_NEXT) * step);
  }
}

// E, R and Space, as commands to the server (§5.5, §13): detonate the target, restore it, and pause or resume the world.
void Command(NeuronClient::ClientSession& _session, std::uint32_t _target, bool _paused, const InputState& _input)
{
  if (_target != 0 && _input.WasKeyPressed('E'))
  {
    _session.Send({NeuronCore::CommandKind::Detonate, _target});
  }
  if (_target != 0 && _input.WasKeyPressed('R'))
  {
    _session.Send({NeuronCore::CommandKind::Restore, _target});
  }
  if (_input.WasKeyPressed(VK_SPACE))
  {
    _session.Send({_paused ? NeuronCore::CommandKind::Resume : NeuronCore::CommandKind::Pause, 0});
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

// The largest emissive scale in any model's palette: what the title reports, times the gain, as the glow the viewer tunes.
[[nodiscard]] float BrightestEmissiveScale(std::span<const NeuronCore::VoxModel> _models) noexcept
{
  float brightest = 0.0f;
  for (const NeuronCore::VoxModel& model : _models)
  {
    for (const NeuronCore::PaletteEntry& entry : model.palette)
    {
      brightest = std::max(brightest, NeuronCore::EmissiveScale(entry));
    }
  }
  return brightest;
}

// What the title and the panel on screen carry (§13): the adapter, the view, the mean frame rate and the GPU's figures
// once there are some, the world's, and whatever else is not at its default.
[[nodiscard]] std::vector<std::wstring> Figures(const NeuronClient::GraphicsDevice& _device, const Controls& _controls,
                                                const WorldFigures& _world, std::optional<double> _framesPerSecond, const GpuFigures& _gpu,
                                                float _brightestEmissive)
{
  std::vector<std::wstring> figures{_device.AdapterName(),
                                    _controls.debugView ? DEBUG_VIEW_NAMES[static_cast<std::size_t>(*_controls.debugView)] : L"lit"};
  if (_framesPerSecond)
  {
    figures.push_back(std::format(L"{:.0f} fps", *_framesPerSecond));
  }
  if (_gpu.frames > 0)
  {
    figures.push_back(std::format(L"GPU {:.2f} ms", _gpu.gpuMilliseconds / _gpu.frames));
    for (std::size_t pass = 0; pass < NeuronClient::GPU_PASS_COUNT; ++pass)
    {
      if (_gpu.passFrames[pass] > 0)
      {
        figures.push_back(std::format(L"{} {:.2f} ms", GPU_PASS_NAMES[pass], _gpu.passMilliseconds[pass] / _gpu.passFrames[pass]));
      }
    }
    figures.push_back(std::format(L"PSInvocations {:.2f} M", _gpu.pixelShaderInvocations / _gpu.frames / 1.0e6));
    figures.push_back(std::format(L"placements: view {} drawn, {} culled; sun {} drawn, {} culled", _gpu.draws.viewDrawn,
                                  _gpu.draws.viewCulled, _gpu.draws.shadowDrawn, _gpu.draws.shadowCulled));
    figures.push_back(
      std::format(L"voxels drawn: view {:.3f} M, sun {:.3f} M", _gpu.draws.viewVoxels / 1.0e6, _gpu.draws.shadowVoxels / 1.0e6));
  }
  figures.push_back(
    std::format(L"server tick {} at {} a second, drawn {:.0f} ms behind", _world.tick, _world.tickRate, _world.behindMilliseconds));
  figures.push_back(std::format(L"{} entities, {} detonated", _world.entities, _world.detonations));
  if (_world.target != 0)
  {
    figures.push_back(std::format(L"target {}{}", _world.target, _world.chasing ? L", chased" : L""));
  }
  if (_world.paused)
  {
    figures.emplace_back(L"paused");
  }
  if (_brightestEmissive > 0.0f)
  {
    figures.push_back(std::format(L"emissive {:.2f}", _brightestEmissive * _controls.emissiveGain));
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

} // namespace

void RunGame(const GameOptions& _options, std::unique_ptr<NeuronCore::Transport> _transport)
{
  const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  const auto now = [start] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
  NeuronClient::ClientSession session(std::move(_transport), _options.modelDirectory);
  AwaitWorld(session, now);

  // The world's lighting and sky, from the welcome (Design/SpaceScene.md §11, §12.1).
  const NeuronCore::WorldSettings settings = session.Settings();
  const std::vector<NeuronCore::StarRecord> stars =
    NeuronCore::MakeStarCatalog(settings.skySeed, settings.galacticPlane, NeuronCore::STAR_COUNT);
  const NeuronCore::SkyParameters sky = NeuronCore::MakeSkyParameters(settings);
  Scene scene(session.Models(), settings.toSun);
  const float brightestEmissive = BrightestEmissiveScale(session.Models());
  WorldSample sample = session.Buffer().Sample(session.Buffer().RenderTick(now()));
  scene.FitShadowView(sample);

  // The first entity, a station of the sector's, is the first target.
  const SampledEntity* first = sample.entities.empty() ? nullptr : &sample.entities.front();
  const NeuronCore::Sphere framed = first != nullptr ? scene.Models().Extent(*first) : NeuronCore::Sphere{{0.0f, 0.0f, 0.0f}, 100.0f};
  Camera camera{OrbitCamera(framed.center, framed.radius), ChaseCamera{}, false, first != nullptr ? first->id : 0u, std::nullopt};

  NeuronClient::Window window({L"Outpost", _options.windowSize});
  const NeuronClient::ClientSize size = window.Size();
  NeuronClient::Renderer renderer({_options.device, window.Handle(), size.widthPixels, size.heightPixels, scene.ShadowView(), stars},
                                  scene.Models().Models());
  try
  {
    // A borderless window has no title bar to show it (§13), so the debugger's output says it too.
    if (renderer.Device().DebugLayer() == NeuronClient::DebugLayerState::Unavailable)
    {
      OutputDebugStringW(L"The Direct3D 12 debug layer is not installed; running without it.\n");
    }
    Controls controls;
    NeuronClient::Clock clock;
    double sinceTitleSeconds = 0.0;
    std::uint32_t framesSinceTitle = 0;
    std::optional<double> framesPerSecond;
    GpuFigures gpu;      // shown
    GpuFigures gpuSince; // collected since the title was last brought up to date
    while (window.PumpMessages())
    {
      const double seconds = clock.Tick();
      ThrowOnRefusal(session.Poll(now()));
      NeuronClient::SnapshotBuffer& buffer = session.Buffer();
      const double renderTick = buffer.RenderTick(now());
      sample = buffer.Sample(renderTick);
      const NeuronClient::ClientSize current = window.Size();
      NeuronClient::InputState& input = window.Input();
      Steer(camera, scene, sample, input, current.heightPixels, static_cast<float>(seconds));
      Command(session, camera.target, buffer.Newest().paused, input);
      Choose(controls, input, window.Handle());
      input.EndFrame();
      if (current.widthPixels == 0 || current.heightPixels == 0)
      {
        // Minimized: nothing to draw until a message says something changed, but the snapshots keep coming.
        MsgWaitForMultipleObjects(0, nullptr, FALSE, MINIMIZED_WAIT_MILLISECONDS, QS_ALLINPUT);
        continue;
      }
      sinceTitleSeconds += seconds;
      ++framesSinceTitle;
      const bool titleDue = sinceTitleSeconds >= TITLE_INTERVAL_SECONDS;
      for (const NeuronClient::FrameStatistics& statistics : renderer.TakeStatistics())
      {
        gpuSince.Add(statistics);
      }
      if (titleDue)
      {
        framesPerSecond = framesSinceTitle / sinceTitleSeconds;
        sinceTitleSeconds = 0.0;
        framesSinceTitle = 0;
        gpu = gpuSince;
        gpuSince = GpuFigures{};
      }
      renderer.Resize(current.widthPixels, current.heightPixels);
      const WorldFigures world{buffer.Newest().tick,
                               buffer.TickRate(),
                               (static_cast<double>(buffer.Newest().tick) - renderTick) * 1000.0 / buffer.TickRate(),
                               sample.entities.size(),
                               static_cast<std::size_t>(std::ranges::count_if(sample.entities, [](const SampledEntity& _entity)
                                                                              { return _entity.detonation.has_value(); })),
                               buffer.Newest().paused,
                               camera.target,
                               camera.chasing};
      const std::vector<std::wstring> figures = Figures(renderer.Device(), controls, world, framesPerSecond, gpu, brightestEmissive);
      if (titleDue)
      {
        window.SetTitle(L"Outpost - " + Joined(figures, L" - "));
      }
      if (controls.figures)
      {
        DrawFigures(renderer.Overlay(), figures, static_cast<float>(GetDpiForWindow(window.Handle())) / USER_DEFAULT_SCREEN_DPI);
      }
      if (scene.FitShadowView(sample))
      {
        renderer.SetShadowView(scene.ShadowView());
      }
      const NeuronCore::LightingParameters lighting = NeuronCore::MakeLightingParameters(settings, controls.emissiveGain);
      renderer.Render(camera.View(current.widthPixels, current.heightPixels), scene.Place(sample),
                      {controls.debugView, lighting, sky, EXPOSURE, controls.vsync, false, false});
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
