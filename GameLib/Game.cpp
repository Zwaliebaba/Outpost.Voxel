#include "pch.h"

#include "Game.h"

#include "Canvas.h"
#include "CapturedFrame.h"
#include "ClientSession.h"
#include "Clock.h"
#include "DeferredSurface.h"
#include "FailureReport.h"
#include "FrameQueries.h"
#include "InputState.h"
#include "Interface.h"
#include "LastSeen.h"
#include "Pick.h"
#include "Renderer.h"
#include "SnapshotBuffer.h"

#include "ChaseCamera.h"
#include "Commander.h"
#include "Hud.h"
#include "OrbitCamera.h"
#include "Scene.h"
#include "StrategicCamera.h"

#include "Catalogue.h"
#include "Orders.h"
#include "WelcomeNames.h"

#include "Blast.h"
#include "Composite.h"
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
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
constexpr NeuronClient::TextStyle FIGURES_STYLE{L"Consolas", 15.0f, NeuronClient::REGULAR_WEIGHT};
constexpr float FIGURES_MARGIN_PIXELS = 8.0f;
constexpr float FIGURES_PADDING_PIXELS = 6.0f;
constexpr float FIGURES_PANEL_ALPHA = 0.55f;

// [ and ] scale the emissive gain by this much, within these bounds (§7.2: the multiplier is tuned by eye).
constexpr float EMISSIVE_STEP = 1.1f;
constexpr float EMISSIVE_GAIN_MINIMUM = 0.01f;
constexpr float EMISSIVE_GAIN_MAXIMUM = 100.0f;

// F3's tuning of the look, which the owner judges by eye (Design/SpaceScene.md §12.1): Up and Down choose a value, Left
// and Right scale it by TUNING_STEP within the emissive gain's bounds, or move the balance by BALANCE_STEP.
enum class Tuned : std::uint8_t
{
  Ambient,
  Balance,
  Sun,
  Exposure
};
constexpr std::uint32_t TUNED_COUNT = 4;
constexpr float TUNING_STEP = 1.1f;
constexpr float BALANCE_STEP = 0.1f;
constexpr float BALANCE_LIMIT = 0.9f;

// Keys 2 to 6 choose these debug views; key 1 returns to the lit image.
constexpr std::array<const wchar_t*, NeuronCore::DEBUG_VIEW_COUNT> DEBUG_VIEW_NAMES{L"albedo", L"normal", L"voxel index", L"shadow map",
                                                                                    L"overdraw"};

// The passes the title and the panel time, by NeuronClient::GpuPass.
constexpr std::array<const wchar_t*, NeuronClient::GPU_PASS_COUNT> GPU_PASS_NAMES{
  L"shadow splat", L"view splat", L"coverage", L"lighting", L"sky", L"gas shells", L"bloom", L"tone map", L"debug view", L"canvas"};

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
                                   L"F3\ttune the lighting: Up Down choose, Left Right change, Home reset, P print to the debugger\n"
                                   L"Alt+F4\tquit";

// A skirmish's keys (Design/MvpPlan.md phase 4, Design/ADR/ADR-034): the commander's, and the debug keys behind Alt.
constexpr const wchar_t* COMMAND_KEY_MAP =
  L"Arrows, the screen's edges\tpan\n"
  L"Wheel\tzoom\n"
  L"Middle drag\tturn\n"
  L"Home\tframe your core\n"
  L"Click, drag\tselect; Shift adds\n"
  L"Right click\tmove the selected ships there\n"
  L"M, then click\tmove\n"
  L"S\tstop\n"
  L"H\thold\n"
  L"Ctrl+0 - 9\tmake the selected ships a group\n"
  L"0 - 9\tselect a group\n"
  L"Esc\tforget an armed move, or the selection\n"
  L"Space\tpause or resume the server\n"
  L"Alt+O, Alt+C\torbit or chase the first selected, until Home\n"
  L"Alt+F\tfly mode, while orbiting: W A S D move, Page Down and Page Up sink and rise\n"
  L"Alt+E, Alt+R\tdetonate or restore the selected\n"
  L"Alt+1\tthe lit image\n"
  L"Alt+2 - 6\talbedo, normal, voxel index, shadow map, overdraw\n"
  L"[ ]\temissive glow down, up\n"
  L"V\tvsync\n"
  L"F1\tthis key map\n"
  L"F2\tthe figures on screen\n"
  L"F3\ttune the lighting: Up Down choose, Left Right change, Home reset, P print to the debugger\n"
  L"Alt+F4\tquit";

// The strategic camera (Design/ADR/ADR-034): it pans a screen's worth of its distance a second, with the arrows or with the
// pointer within EDGE_PIXELS of the screen's edge, and Home frames the side's core from HOME_DISTANCE.
constexpr float PAN_SCREENS_PER_SECOND = 1.0f;
constexpr float EDGE_PIXELS = 4.0f;
constexpr float HOME_DISTANCE = 1400.0f;

// The interface's look at 96 DPI, and larger in proportion on a denser monitor.
constexpr NeuronClient::InterfaceStyle INTERFACE_STYLE{{L"Segoe UI", 15.0f, NeuronClient::REGULAR_WEIGHT},
                                                       5.0f,
                                                       {0.0f, 0.0f, 0.0f},
                                                       0.55f,
                                                       {0.08f, 0.1f, 0.13f},
                                                       {0.16f, 0.22f, 0.3f},
                                                       {0.25f, 0.35f, 0.5f},
                                                       {0.95f, 0.95f, 0.95f},
                                                       {0.45f, 0.45f, 0.45f},
                                                       {0.12f, 0.3f, 0.5f},
                                                       {0.05f, 0.05f, 0.05f}};

// How the commander marks the world, at 96 DPI.
constexpr CommanderLook COMMANDER_LOOK{
  {0.35f, 0.85f, 1.0f}, {1.0f, 0.35f, 0.3f}, {0.9f, 0.85f, 0.5f}, {0.4f, 1.0f, 0.5f}, 1.5f, 1.5f, 12.0f};

// What F3 tunes: multipliers on the welcome's world settings, which stay as the server sent them, and the exposure, which
// only the client has. Nothing of it reaches the server (AGENTS.md R18).
struct Tuning
{
  bool shown = false;
  Tuned chosen = Tuned::Ambient;
  float ambientScale = 1.0f;
  float balance = 0.0f; // from the ground's color, -1, to the sky's, +1; their mean, what a side face sees, stays
  float sunScale = 1.0f;
  float exposure = EXPOSURE;
};

struct Controls
{
  std::optional<NeuronCore::DebugView> debugView; // empty: the lit image
  bool vsync = false;
  bool figures = true; // on screen; the title always carries them
  float emissiveGain = 1.0f;
  Tuning tuning;
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
// frame, and of the view splat's pixel-shader invocations; and the latest frame's draw counts (Design/Archive/SpaceScene.md §7.4).
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

// What the frame shows of the world (§13): the server's clock and how far behind it the frame is drawn, the side the
// session plays, the entities, the detonations in progress and the structures remembered out of sight (Design/ADR/ADR-032),
// and the camera's target and what the welcome names it (Design/ADR/ADR-030).
struct WorldFigures
{
  std::uint64_t tick;
  std::uint32_t tickRate;
  double behindMilliseconds;
  std::wstring side; // empty in a world without sides
  std::size_t entities;
  std::size_t detonations;
  std::size_t remembered;
  bool paused;
  std::uint32_t target;
  std::wstring targetName; // empty when the welcome names nothing
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

// The names the welcome's payload gives the composites and the sides (Design/ADR/ADR-030), which the figures show: none
// for a world that sends none, as the space scene's does. Throws when the payload is refused, or names other counts than
// the welcome holds.
[[nodiscard]] GameCore::WelcomeNames NamesOf(const NeuronClient::ClientSession& _session)
{
  std::expected<GameCore::WelcomeNames, GameCore::NamesError> names = GameCore::DecodeWelcomeNames(_session.WelcomePayload());
  if (!names)
  {
    throw std::runtime_error(std::format("The welcome's names were refused: {}.", GameCore::NamesErrorName(names.error())));
  }
  const bool named = !names->composites.empty() || !names->sides.empty();
  if (named && (names->composites.size() != _session.Composites().size() || names->sides.size() != _session.Sides().size()))
  {
    throw std::runtime_error(std::format("The welcome names {} composites and {} sides, of its {} and {}.", names->composites.size(),
                                         names->sides.size(), _session.Composites().size(), _session.Sides().size()));
  }
  return std::move(*names);
}

// What the figures call _entity: its composite's name and its side's, as the welcome names them; nothing when it names
// none.
[[nodiscard]] std::wstring NameOf(const GameCore::WelcomeNames& _names, const SampledEntity& _entity)
{
  if (_entity.composite >= _names.composites.size())
  {
    return {};
  }
  std::string name = _names.composites[_entity.composite];
  if (_entity.side != 0 && _entity.side <= _names.sides.size())
  {
    name.append(", ").append(_names.sides[_entity.side - 1u]);
  }
  return std::wstring(winrt::to_hstring(name));
}

// What the figures say the session plays (Design/ADR/ADR-032): its side, by number and by the name the welcome gives it,
// or that it observes; nothing in a world without sides, as the space scene's is.
[[nodiscard]] std::wstring SideOf(const NeuronClient::ClientSession& _session, const GameCore::WelcomeNames& _names)
{
  if (_session.Sides().empty())
  {
    return {};
  }
  if (_session.Side() == NeuronCore::OBSERVER_SIDE)
  {
    return L"observer";
  }
  std::string side = std::format("side {}", _session.Side());
  if (_session.Side() <= _names.sides.size())
  {
    side.append(", ").append(_names.sides[_session.Side() - 1u]);
  }
  return std::wstring(winrt::to_hstring(side));
}

// The composites whose entities the client remembers out of sight (Design/ADR/ADR-032): each the welcome names for a
// structure's design in GameCore's catalogue, the core in the MVP.
[[nodiscard]] std::vector<bool> StructureComposites(const GameCore::WelcomeNames& _names)
{
  std::vector<bool> structures;
  structures.reserve(_names.composites.size());
  for (const std::string& name : _names.composites)
  {
    const GameCore::DesignSpec* design = GameCore::FindDesign(name);
    structures.push_back(design != nullptr && design->kind == GameCore::DesignKind::Structure);
  }
  return structures;
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

// The first view (§13): the first entity, a station of the sector's, framed and targeted; or for an overview every entity
// framed, and none targeted until N chooses one (Design/ADR/ADR-030).
[[nodiscard]] Camera FirstView(const Scene& _scene, const WorldSample& _sample, bool _overview)
{
  if (_sample.entities.empty())
  {
    return {OrbitCamera({0.0f, 0.0f, 0.0f}, 100.0f), ChaseCamera{}, false, 0, std::nullopt};
  }
  if (!_overview)
  {
    const SampledEntity& first = _sample.entities.front();
    const NeuronCore::Sphere framed = _scene.Models().Extent(first);
    return {OrbitCamera(framed.center, framed.radius), ChaseCamera{}, false, first.id, std::nullopt};
  }
  std::vector<NeuronCore::Sphere> extents;
  extents.reserve(_sample.entities.size());
  for (const SampledEntity& entity : _sample.entities)
  {
    extents.push_back(_scene.Models().Extent(entity));
  }
  const NeuronCore::Sphere everything = NeuronCore::EnclosingSphere(extents);
  return {OrbitCamera(everything.center, everything.radius), ChaseCamera{}, false, 0, std::nullopt};
}

// The space scene's camera keys (§13): N and B choose the target, F frames it, C chases it, and Tab flies.
void SteerKeys(Camera& _camera, const Scene& _scene, const WorldSample& _sample, const InputState& _input)
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
        _camera.chase.Reset(chosen->position, NeuronCore::RotationOf(chosen->rotation), _scene.Models().Radius(chosen->composite));
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
    _camera.chase.Reset(target->position, NeuronCore::RotationOf(target->rotation), _scene.Models().Radius(target->composite));
  }
  if (_input.WasKeyPressed(VK_TAB))
  {
    _camera.chasing = false;
    _camera.orbit.ToggleFlying();
  }
}

// The orbit and chase cameras (§13), following their target, and the orbit's pointer and fly keys. _pointer says whether
// the pointer is the world's to steer with.
void SteerOrbit(Camera& _camera, const Scene& _scene, const WorldSample& _sample, const InputState& _input, bool _pointer,
                std::uint32_t _heightPixels, float _seconds)
{
  // The orbit keeps its place relative to the target as the target moves; the chase camera rides behind it. A target
  // that has gone leaves the camera where it is.
  const SampledEntity* target = FindEntity(_sample, _camera.target);
  if (target != nullptr)
  {
    if (_camera.followed.has_value() && !_camera.orbit.IsFlying())
    {
      _camera.orbit.MoveTarget(target->position - _camera.followed.value());
    }
    _camera.followed = target->position;
    if (_camera.chasing)
    {
      _camera.chase.Follow(target->position, NeuronCore::RotationOf(target->rotation), _scene.Models().Radius(target->composite), _seconds);
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
  if (_pointer && _input.IsButtonDown(MouseButton::Left))
  {
    orbit.Orbit(_input.MouseDeltaXPixels(), _input.MouseDeltaYPixels());
  }
  if (_pointer && _input.IsButtonDown(MouseButton::Right))
  {
    orbit.Pan(_input.MouseDeltaXPixels(), _input.MouseDeltaYPixels(), _heightPixels);
  }
  if (_pointer)
  {
    orbit.Dolly(_input.WheelNotches());
  }
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

// The controls' keys. A skirmish's digits are its control groups, so while _commanding the debug views are behind Alt
// (Design/ADR/ADR-034).
void Choose(Controls& _controls, const InputState& _input, HWND _window, bool _commanding)
{
  const bool views = !_commanding || _input.IsKeyDown(VK_MENU);
  if (views && _input.WasKeyPressed('1'))
  {
    _controls.debugView.reset();
  }
  for (std::uint32_t view = 0; views && view < NeuronCore::DEBUG_VIEW_COUNT; ++view)
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
    MessageBoxW(_window, _commanding ? COMMAND_KEY_MAP : KEY_MAP, L"Outpost keys", MB_OK | MB_ICONINFORMATION);
  }
  if (_input.WasKeyPressed(VK_F2))
  {
    _controls.figures = !_controls.figures;
  }
  if (_input.WasKeyPressed(VK_F3))
  {
    _controls.tuning.shown = !_controls.tuning.shown;
  }
}

// F3's keys, while the tuning is shown.
void Tune(Tuning& _tuning, const InputState& _input) noexcept
{
  if (!_tuning.shown)
  {
    return;
  }
  auto chosen = static_cast<std::uint32_t>(_tuning.chosen);
  if (_input.WasKeyPressed(VK_DOWN))
  {
    chosen = (chosen + 1) % TUNED_COUNT;
  }
  if (_input.WasKeyPressed(VK_UP))
  {
    chosen = (chosen + TUNED_COUNT - 1) % TUNED_COUNT;
  }
  _tuning.chosen = static_cast<Tuned>(chosen);
  const float steps = (_input.WasKeyPressed(VK_RIGHT) ? 1.0f : 0.0f) - (_input.WasKeyPressed(VK_LEFT) ? 1.0f : 0.0f);
  const float scale = std::pow(TUNING_STEP, steps);
  const auto scaled = [scale](float _value) { return std::clamp(_value * scale, EMISSIVE_GAIN_MINIMUM, EMISSIVE_GAIN_MAXIMUM); };
  switch (_tuning.chosen)
  {
  case Tuned::Ambient:
    _tuning.ambientScale = scaled(_tuning.ambientScale);
    break;
  case Tuned::Balance:
    _tuning.balance = std::clamp(_tuning.balance + steps * BALANCE_STEP, -BALANCE_LIMIT, BALANCE_LIMIT);
    break;
  case Tuned::Sun:
    _tuning.sunScale = scaled(_tuning.sunScale);
    break;
  case Tuned::Exposure:
    _tuning.exposure = scaled(_tuning.exposure);
    break;
  }
  if (_input.WasKeyPressed(VK_HOME))
  {
    _tuning = Tuning{.shown = true, .chosen = _tuning.chosen};
  }
}

// The world's settings as the tuning has them: what the lighting and the sky draw, and what P prints for the owner to make
// the defaults in GameLogic's Sector.cpp.
[[nodiscard]] NeuronCore::WorldSettings TunedSettings(const NeuronCore::WorldSettings& _settings, const Tuning& _tuning) noexcept
{
  NeuronCore::WorldSettings tuned = _settings;
  tuned.ambientUpper = _settings.ambientUpper * (_tuning.ambientScale * (1.0f + _tuning.balance));
  tuned.ambientLower = _settings.ambientLower * (_tuning.ambientScale * (1.0f - _tuning.balance));
  tuned.sunRadiance = _settings.sunRadiance * _tuning.sunScale;
  return tuned;
}

// The tuning's lines on the panel, the chosen one marked. The colors are shown by their red, as the defaults are white.
[[nodiscard]] std::vector<std::wstring> TuningFigures(const NeuronCore::WorldSettings& _tuned, const Tuning& _tuning)
{
  const auto line = [&_tuning](Tuned _value, const std::wstring& _text)
  { return std::wstring(_tuning.chosen == _value ? L"> " : L"  ") + _text; };
  return {L"tuning: Up Down choose, Left Right change, Home reset, P print",
          line(Tuned::Ambient, std::format(L"ambient x{:.2f}: sky {:.3f}, ground {:.3f}", _tuning.ambientScale, _tuned.ambientUpper.x,
                                           _tuned.ambientLower.x)),
          line(Tuned::Balance, std::format(L"ambient balance {:+.1f}", _tuning.balance)),
          line(Tuned::Sun, std::format(L"sun x{:.2f}: {:.3f}", _tuning.sunScale, _tuned.sunRadiance.x)),
          line(Tuned::Exposure, std::format(L"exposure {:.3f}", _tuning.exposure))};
}

// What P prints to the debugger: the tuned values, as Sector.cpp and Scene.h spell them.
void PrintTuning(const NeuronCore::WorldSettings& _tuned, const Tuning& _tuning, float _emissiveGain)
{
  const std::wstring text =
    std::format(L"Tuned lighting: sun radiance {:.4f}, ambient upper {:.4f}, ambient lower {:.4f}, exposure {:.4f}, emissive gain {:.4f}\n",
                _tuned.sunRadiance.x, _tuned.ambientUpper.x, _tuned.ambientLower.x, _tuning.exposure, _emissiveGain);
  OutputDebugStringW(text.c_str());
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
  if (!_world.side.empty())
  {
    figures.push_back(_world.side);
  }
  figures.push_back(_world.remembered == 0
                      ? std::format(L"{} entities, {} detonated", _world.entities, _world.detonations)
                      : std::format(L"{} entities, {} detonated, {} remembered", _world.entities, _world.detonations, _world.remembered));
  if (_world.target != 0)
  {
    figures.push_back(std::format(L"target {}{}{}{}", _world.target, _world.targetName.empty() ? L"" : L": ", _world.targetName,
                                  _world.chasing ? L", chased" : L""));
  }
  if (_world.paused)
  {
    figures.emplace_back(L"paused");
  }
  if (_brightestEmissive > 0.0f)
  {
    figures.push_back(std::format(L"emissive {:.2f}", _brightestEmissive * _controls.emissiveGain));
  }
  if (_controls.vsync)
  {
    figures.emplace_back(L"vsync on");
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

// The figures on the canvas, one per line, white on a translucent black panel in the top-left corner, _topPixels down.
// _scale is the monitor's DPI over 96.
void DrawFigures(NeuronClient::Canvas& _canvas, const std::vector<std::wstring>& _figures, float _scale, float _topPixels)
{
  const NeuronClient::TextStyle style{FIGURES_STYLE.fontFamily, FIGURES_STYLE.sizePixels * _scale, FIGURES_STYLE.weight};
  const std::wstring text = Joined(_figures, L"\n");
  const NeuronClient::TextExtent extent = _canvas.Measure(text, style);
  const float margin = std::round(FIGURES_MARGIN_PIXELS * _scale);
  const float padding = std::round(FIGURES_PADDING_PIXELS * _scale);
  const float top = _topPixels + margin;
  _canvas.FillRectangle(static_cast<std::int32_t>(margin), static_cast<std::int32_t>(top),
                        static_cast<std::uint32_t>(std::ceil(extent.widthPixels + 2.0f * padding)),
                        static_cast<std::uint32_t>(std::ceil(extent.heightPixels + 2.0f * padding)), {0.0f, 0.0f, 0.0f},
                        FIGURES_PANEL_ALPHA);
  _canvas.Print(text, margin + padding, top + padding, style, {1.0f, 1.0f, 1.0f}, 1.0f);
}

// Writes the frame the renderer has just captured to _file (Design/ADR/ADR-031), or throws saying why it could not.
void WriteCapture(NeuronClient::Renderer& _renderer, const std::filesystem::path& _file)
{
  const std::optional<NeuronClient::CapturedFrame> frame = _renderer.TakeCapture();
  if (!frame.has_value())
  {
    throw std::logic_error("The frame to capture was not kept.");
  }
  try
  {
    NeuronClient::WritePng(*frame, _file);
  }
  catch (...)
  {
    throw std::runtime_error(std::format("The capture could not be written to {}: {}", winrt::to_string(_file.wstring()),
                                         NeuronClient::DescribeCurrentException()));
  }
}

// What the client keeps to command a skirmish (Design/ADR/ADR-034).
struct Commanding
{
  StrategicCamera strategic;
  Commander commander;
  NeuronClient::Interface widgets;
  CommanderLook look;
  std::vector<NeuronCore::Float3> halfSizes; // by composite: half its box, which the pointer picks
  std::vector<float> radii;                  // by composite: its sphere, from which a selected entity's ring stands out
  std::vector<bool> ships;                   // by composite: whether it is a ship's design
  NeuronCore::Float3 home;                   // where Home looks
  float homeDistance;                        // and from how far
  bool inspecting;                           // the orbit or the chase camera, from Alt+O or Alt+C until Home
};

// The skirmish's commanding, as it starts: the strategic camera looks at the side's core from HOME_DISTANCE, toward the
// sector's middle, or for an observer at everything. _scale is the monitor's DPI over 96.
[[nodiscard]] Commanding MakeCommanding(const NeuronClient::ClientSession& _session, const GameCore::WelcomeNames& _names,
                                        const Scene& _scene, const WorldSample& _sample, float _scale)
{
  const std::span<const NeuronCore::CompositeModel> composites = _session.Composites();
  std::vector<NeuronCore::Float3> halfSizes;
  std::vector<float> radii;
  std::vector<bool> ships;
  halfSizes.reserve(composites.size());
  radii.reserve(composites.size());
  ships.reserve(composites.size());
  for (std::size_t index = 0; index < composites.size(); ++index)
  {
    const NeuronCore::VoxelBounds bounds =
      NeuronCore::CompositeBounds(_session.Models(), composites[index]).value_or(NeuronCore::VoxelBounds{});
    const NeuronCore::Int3 extent = bounds.upper - bounds.lower;
    halfSizes.push_back({0.5f * static_cast<float>(extent.x), 0.5f * static_cast<float>(extent.y), 0.5f * static_cast<float>(extent.z)});
    radii.push_back(_scene.Models().Radius(static_cast<std::uint16_t>(index)));
    const GameCore::DesignSpec* design = index < _names.composites.size() ? GameCore::FindDesign(_names.composites[index]) : nullptr;
    ships.push_back(design != nullptr && design->kind == GameCore::DesignKind::Ship);
  }

  const std::vector<bool> structures = StructureComposites(_names);
  const auto core = std::ranges::find_if(_sample.entities,
                                         [&_session, &structures](const SampledEntity& _entity)
                                         {
                                           return _session.Side() != NeuronCore::OBSERVER_SIDE && _entity.side == _session.Side() &&
                                                  _entity.composite < structures.size() && structures[_entity.composite];
                                         });
  NeuronCore::Float3 home{0.0f, 0.0f, 0.0f};
  float homeDistance = HOME_DISTANCE;
  float heading = 0.0f;
  if (core != _sample.entities.end())
  {
    home = {core->position.x, 0.0f, core->position.z};
    heading = std::atan2(-core->position.z, -core->position.x);
  }
  else if (!_sample.entities.empty())
  {
    std::vector<NeuronCore::Sphere> extents;
    extents.reserve(_sample.entities.size());
    for (const SampledEntity& entity : _sample.entities)
    {
      extents.push_back(_scene.Models().Extent(entity));
    }
    const NeuronCore::Sphere everything = NeuronCore::EnclosingSphere(extents);
    home = {everything.center.x, 0.0f, everything.center.z};
    homeDistance = everything.radius / std::tan(0.5f * StrategicCamera::FOV_Y_RADIANS);
  }

  NeuronClient::InterfaceStyle style = INTERFACE_STYLE;
  style.text.sizePixels *= _scale;
  style.paddingPixels = std::round(style.paddingPixels * _scale);
  CommanderLook look = COMMANDER_LOOK;
  look.ringWidthPixels *= _scale;
  look.lineWidthPixels *= _scale;
  look.markerPixels *= _scale;
  return {StrategicCamera(home, homeDistance, heading),
          Commander(_session.Side()),
          NeuronClient::Interface(style),
          look,
          std::move(halfSizes),
          std::move(radii),
          std::move(ships),
          home,
          homeDistance,
          false};
}

// The strategic camera's keys and pointer (Design/MvpPlan.md phase 4): the arrows, unless the tuning has them, and the
// pointer at the screen's edges pan; and, while the interface leaves the pointer to the world, the wheel zooms and a
// middle drag turns.
void SteerStrategic(StrategicCamera& _camera, const InputState& _input, bool _pointerIsWorlds, bool _arrows, NeuronClient::ClientSize _size,
                    float _seconds)
{
  float right = 0.0f;
  float ahead = 0.0f;
  if (_arrows)
  {
    right += (_input.IsKeyDown(VK_RIGHT) ? 1.0f : 0.0f) - (_input.IsKeyDown(VK_LEFT) ? 1.0f : 0.0f);
    ahead += (_input.IsKeyDown(VK_UP) ? 1.0f : 0.0f) - (_input.IsKeyDown(VK_DOWN) ? 1.0f : 0.0f);
  }
  if (_input.HasPointer())
  {
    const auto x = static_cast<float>(_input.PointerXPixels());
    const auto y = static_cast<float>(_input.PointerYPixels());
    right += x < EDGE_PIXELS ? -1.0f : (x >= static_cast<float>(_size.widthPixels) - EDGE_PIXELS ? 1.0f : 0.0f);
    ahead += y < EDGE_PIXELS ? 1.0f : (y >= static_cast<float>(_size.heightPixels) - EDGE_PIXELS ? -1.0f : 0.0f);
  }
  const float step = PAN_SCREENS_PER_SECOND * _camera.Distance() * _seconds;
  _camera.Pan(std::clamp(right, -1.0f, 1.0f) * step, std::clamp(ahead, -1.0f, 1.0f) * step);
  if (_pointerIsWorlds)
  {
    _camera.Zoom(_input.WheelNotches());
    if (_input.IsButtonDown(MouseButton::Middle))
    {
      _camera.Turn(_input.MouseDeltaXPixels(), _input.MouseDeltaYPixels());
    }
  }
}

// The order states the newest snapshot's payload carries (Design/ADR/ADR-033): none in a world that sends none.
[[nodiscard]] std::vector<GameCore::ShipOrderState> OrderStatesOf(const NeuronCore::Snapshot& _snapshot)
{
  if (_snapshot.payload.empty())
  {
    return {};
  }
  return GameCore::DecodeOrderStates(_snapshot.payload).value_or(std::vector<GameCore::ShipOrderState>{});
}

// The selection panel's rows: each selected entity's composite's name and its id, and what it does, for a ship of the
// side's.
[[nodiscard]] std::vector<SelectionRow> SelectionRows(std::span<const std::uint32_t> _selection, const WorldSample& _sample,
                                                      const GameCore::WelcomeNames& _names,
                                                      std::span<const GameCore::ShipOrderState> _states, const std::vector<bool>& _ships,
                                                      std::uint8_t _side)
{
  std::vector<SelectionRow> rows;
  for (const std::uint32_t id : _selection)
  {
    const SampledEntity* entity = FindEntity(_sample, id);
    if (entity == nullptr)
    {
      continue;
    }
    const std::wstring name = entity->composite < _names.composites.size()
                                ? std::wstring(winrt::to_hstring(_names.composites[entity->composite]))
                                : std::wstring(L"entity");
    std::wstring text = std::format(L"{} {}", name, id);
    if (entity->detonation.has_value())
    {
      text += L"   debris";
    }
    else if (entity->side == _side && entity->composite < _ships.size() && _ships[entity->composite])
    {
      const auto state = std::ranges::find(_states, id, &GameCore::ShipOrderState::ship);
      text += state == _states.end() ? L"   idle" : (state->state == GameCore::ShipState::Moving ? L"   moving" : L"   holding");
    }
    rows.push_back({id, std::move(text)});
  }
  return rows;
}

// A frame of a skirmish's commanding (Design/ADR/ADR-034), before the frame draws. The HUD is laid out on _hud first, to
// be drawn later over the world's overlay, and takes the input it owns. Then the debug keys behind Alt, and Space; the
// camera, strategic or, from Alt+O or Alt+C until Home, the orbit or the chase camera; and the commander's selection and
// orders, which go to the server at once. Returns the view the frame draws.
[[nodiscard]] NeuronCore::PerspectiveView CommandSkirmish(Commanding& _command, Camera& _camera, NeuronClient::ClientSession& _session,
                                                          const GameCore::WelcomeNames& _names, const Scene& _scene,
                                                          const WorldSample& _sample, std::span<const GameCore::ShipOrderState> _states,
                                                          const InputState& _input, NeuronClient::DeferredSurface& _hud,
                                                          NeuronClient::ClientSize _size, float _seconds, double _nowSeconds, bool _tuning,
                                                          const std::wstring& _sideName)
{
  const std::uint8_t side = _session.Side();
  const bool observing = side == NeuronCore::OBSERVER_SIDE;
  const NeuronCore::Snapshot& newest = _session.Buffer().Newest();

  // What the pointer can pick, the whole entities, for it never picks debris (the concept's §9); and of them the side's
  // ships, which it may order.
  std::vector<std::uint32_t> orderable;
  std::vector<NeuronClient::PickBox> boxes;
  boxes.reserve(_sample.entities.size());
  for (const SampledEntity& entity : _sample.entities)
  {
    if (entity.detonation.has_value())
    {
      continue;
    }
    boxes.push_back({entity.id, entity.position, NeuronCore::RotationOf(entity.rotation), _command.halfSizes[entity.composite]});
    if (!observing && entity.side == side && _command.ships[entity.composite])
    {
      orderable.push_back(entity.id);
    }
  }
  _command.commander.Keep(_sample);
  const std::span<const std::uint32_t> selection = _command.commander.Selection();
  const bool canOrder =
    std::ranges::any_of(selection, [&orderable](std::uint32_t _id) { return std::ranges::find(orderable, _id) != orderable.end(); });

  // The HUD.
  const std::vector<SelectionRow> rows = SelectionRows(selection, _sample, _names, _states, _command.ships, side);
  _command.widgets.Begin(_hud, _input);
  const HudRequest request = DrawHud(
    _command.widgets, static_cast<float>(_size.widthPixels), static_cast<float>(_size.heightPixels),
    {_sideName, orderable.size(), static_cast<double>(newest.worldTick) / static_cast<double>(_session.Buffer().TickRate()), newest.paused},
    rows, !observing && !_command.inspecting, canOrder, _command.commander.IsMoveArmed());
  _command.widgets.End();
  const bool pointerIsWorlds = !_command.widgets.OwnsPointer();
  if (request.chosen.has_value())
  {
    _command.commander.SelectOnly(*request.chosen);
  }

  // The debug keys, behind Alt, on the selection; Space; and Home, unless the tuning has it.
  const bool alt = _input.IsKeyDown(VK_MENU);
  const SampledEntity* first = selection.empty() ? nullptr : FindEntity(_sample, selection.front());
  if (alt && first != nullptr && (_input.WasKeyPressed('O') || _input.WasKeyPressed('C')))
  {
    _camera.target = first->id;
    _camera.followed.reset();
    const NeuronCore::Sphere extent = _scene.Models().Extent(*first);
    _camera.orbit.Frame(extent.center, extent.radius);
    _camera.chasing = _input.WasKeyPressed('C');
    if (_camera.chasing)
    {
      _camera.chase.Reset(first->position, NeuronCore::RotationOf(first->rotation), _scene.Models().Radius(first->composite));
    }
    _command.inspecting = true;
  }
  if (alt && _command.inspecting && _input.WasKeyPressed('F'))
  {
    _camera.chasing = false;
    _camera.orbit.ToggleFlying();
  }
  if (alt && (_input.WasKeyPressed('E') || _input.WasKeyPressed('R')))
  {
    const NeuronCore::CommandKind kind = _input.WasKeyPressed('E') ? NeuronCore::CommandKind::Detonate : NeuronCore::CommandKind::Restore;
    for (const std::uint32_t id : selection)
    {
      _session.Send({kind, id, {}});
    }
  }
  if (_input.WasKeyPressed(VK_SPACE))
  {
    _session.Send({newest.paused ? NeuronCore::CommandKind::Resume : NeuronCore::CommandKind::Pause, 0, {}});
  }
  if (!_tuning && _input.WasKeyPressed(VK_HOME))
  {
    _command.inspecting = false;
    _camera.target = 0;
    _command.strategic.Frame(_command.home, _command.homeDistance);
  }

  // The camera, and the commander.
  if (_command.inspecting)
  {
    SteerOrbit(_camera, _scene, _sample, _input, pointerIsWorlds, _size.heightPixels, _seconds);
  }
  else
  {
    SteerStrategic(_command.strategic, _input, pointerIsWorlds, !_tuning, _size, _seconds);
  }
  const NeuronCore::PerspectiveView view = _command.inspecting ? _camera.View(_size.widthPixels, _size.heightPixels)
                                                               : _command.strategic.View(_size.widthPixels, _size.heightPixels);
  const std::vector<GameCore::Order> orders =
    _command.commander.Update(_input, pointerIsWorlds && !_command.inspecting, view, boxes, orderable, request.action, _nowSeconds);
  for (const GameCore::Order& order : orders)
  {
    _session.Send({NeuronCore::CommandKind::Game, 0, GameCore::EncodeOrder(order)});
  }
  return view;
}

} // namespace

void RunGame(const GameOptions& _options, std::unique_ptr<NeuronCore::Transport> _transport)
{
  const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  const auto now = [start] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
  NeuronClient::ClientSession session(std::move(_transport), _options.modelDirectory);
  AwaitWorld(session, now);
  const GameCore::WelcomeNames names = NamesOf(session);
  const std::wstring side = SideOf(session, names);
  NeuronClient::LastSeen lastSeen(StructureComposites(names));

  // --capture's file and the tick of the world it names (Design/ADR/ADR-031): the frame is drawn at that tick, the first time
  // the render time reaches it. Without --capture, that time never comes.
  const std::filesystem::path captureFile = _options.capture ? _options.capture->file : std::filesystem::path{};
  const double captureTick =
    _options.capture ? _options.capture->worldSeconds * session.Buffer().TickRate() : std::numeric_limits<double>::infinity();

  // The world's lighting and sky, from the welcome (Design/Archive/SpaceScene.md §11, §12.1).
  const NeuronCore::WorldSettings settings = session.Settings();
  const std::vector<NeuronCore::StarRecord> stars =
    NeuronCore::MakeStarCatalog(settings.skySeed, settings.galacticPlane, NeuronCore::STAR_COUNT);
  Scene scene(session.Models(), session.Composites(), session.Sides().size(), settings.toSun);
  const float brightestEmissive = BrightestEmissiveScale(session.Models());
  WorldSample sample = session.Buffer().Sample(session.Buffer().RenderTick(now()));
  scene.FitShadowView(sample);

  Camera camera = FirstView(scene, sample, _options.overview);

  NeuronClient::Window window({L"Outpost", _options.windowSize});
  const NeuronClient::ClientSize size = window.Size();
  NeuronClient::Renderer renderer({_options.device, window.Handle(), size.widthPixels, size.heightPixels, scene.ShadowView(), stars},
                                  scene.Models().Models(), scene.Models().Fragments(), session.Sides());

  // A world with sides, the skirmish, is commanded (Design/ADR/ADR-034); the space scene keeps its cameras and keys. The
  // interface and the commander's marks take their sizes from the monitor's DPI as the game starts.
  std::optional<Commanding> command;
  if (!session.Sides().empty())
  {
    command.emplace(
      MakeCommanding(session, names, scene, sample, static_cast<float>(GetDpiForWindow(window.Handle())) / USER_DEFAULT_SCREEN_DPI));
  }
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
      const bool capturing = renderTick >= captureTick;
      const double drawnTick = capturing ? captureTick : renderTick;
      sample = buffer.Sample(drawnTick);
      lastSeen.See(sample);
      const std::vector<SampledEntity> remembered = lastSeen.Remembered(sample);
      const NeuronClient::ClientSize current = window.Size();
      NeuronClient::InputState& input = window.Input();
      std::optional<NeuronClient::DeferredSurface> hud;
      std::optional<NeuronCore::PerspectiveView> commandView;
      std::vector<GameCore::ShipOrderState> states;
      if (command.has_value())
      {
        states = OrderStatesOf(buffer.Newest());
        hud.emplace(renderer.Overlay());
        commandView = CommandSkirmish(*command, camera, session, names, scene, sample, states, input, *hud, current,
                                      static_cast<float>(seconds), now(), controls.tuning.shown, side);
      }
      else
      {
        SteerKeys(camera, scene, sample, input);
        SteerOrbit(camera, scene, sample, input, true, current.heightPixels, static_cast<float>(seconds));
        Command(session, camera.target, buffer.Newest().paused, input);
      }
      Choose(controls, input, window.Handle(), command.has_value());
      Tune(controls.tuning, input);
      const NeuronCore::WorldSettings tuned = TunedSettings(settings, controls.tuning);
      if (controls.tuning.shown && input.WasKeyPressed('P'))
      {
        PrintTuning(tuned, controls.tuning, controls.emissiveGain);
      }
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
      const SampledEntity* targeted = FindEntity(sample, camera.target);
      const WorldFigures world{buffer.Newest().tick,
                               buffer.TickRate(),
                               (static_cast<double>(buffer.Newest().tick) - drawnTick) * 1000.0 / buffer.TickRate(),
                               side,
                               sample.entities.size(),
                               static_cast<std::size_t>(std::ranges::count_if(sample.entities, [](const SampledEntity& _entity)
                                                                              { return _entity.detonation.has_value(); })),
                               remembered.size(),
                               buffer.Newest().paused,
                               camera.target,
                               targeted != nullptr ? NameOf(names, *targeted) : std::wstring{},
                               camera.chasing};
      const std::vector<std::wstring> figures = Figures(renderer.Device(), controls, world, framesPerSecond, gpu, brightestEmissive);
      if (titleDue)
      {
        window.SetTitle(L"Outpost - " + Joined(figures, L" - "));
      }
      std::vector<std::wstring> panel = controls.figures ? figures : std::vector<std::wstring>{};
      if (controls.tuning.shown)
      {
        std::ranges::move(TuningFigures(tuned, controls.tuning), std::back_inserter(panel));
      }
      // The world's overlay, the HUD over it, and the figures over both.
      if (command.has_value() && commandView.has_value() && hud.has_value())
      {
        command->commander.Draw(renderer.Overlay(), *commandView, sample, states, command->radii, command->look, now());
        hud->Replay();
      }
      if (!panel.empty())
      {
        DrawFigures(renderer.Overlay(), panel, static_cast<float>(GetDpiForWindow(window.Handle())) / USER_DEFAULT_SCREEN_DPI,
                    command.has_value() ? command->widgets.RowHeightPixels() : 0.0f);
      }
      if (scene.FitShadowView(sample))
      {
        renderer.SetShadowView(scene.ShadowView());
      }
      const NeuronCore::LightingParameters lighting = NeuronCore::MakeLightingParameters(tuned, controls.emissiveGain);
      const std::vector<NeuronCore::Blast> blasts = scene.Blasts(sample);
      renderer.Render(commandView.value_or(camera.View(current.widthPixels, current.heightPixels)), scene.Place(sample, remembered),
                      {controls.debugView, lighting, NeuronCore::MakeSkyParameters(tuned), controls.tuning.exposure, controls.vsync, false,
                       false, blasts, capturing});
      if (capturing)
      {
        WriteCapture(renderer, captureFile);
        return;
      }
    }
    // The window closed before the capture's time came, so nothing was written (Design/ADR/ADR-031).
    if (_options.capture)
    {
      throw std::runtime_error(
        std::format("The window closed before the world's {} seconds, so no capture was written.", _options.capture->worldSeconds));
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
