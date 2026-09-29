#include "pch.h"

#include "Bench.h"

#include "Canvas.h"
#include "ClientSession.h"
#include "FailureReport.h"
#include "FrameQueries.h"
#include "Renderer.h"
#include "SnapshotBuffer.h"
#include "Window.h"

#include "OrbitCamera.h"
#include "Scene.h"

#include "Explosion.h"
#include "Lighting.h"
#include "Message.h"
#include "Placement.h"
#include "Sky.h"
#include "Sphere.h"
#include "StarCatalog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace GameLib
{
namespace
{

using SteadyClock = std::chrono::steady_clock;

// The timeline advances 1/60 s a frame however long the frame took, so that every run draws the same frames.
constexpr std::uint32_t FRAMES_PER_SECOND = 60;

// Frames of the timeline's start, in both variants, before the measured ones, while pipelines, caches and clocks settle.
constexpr std::uint32_t WARMUP_FRAMES = 120;

// How far into the run, as a fraction of it, the bench asks the server to detonate the station, which the station
// sample's timeline detonated at. The camera turns once round the station over the whole run.
constexpr double DETONATION_FRACTION = 0.25;

// The run's progress on screen, which the canvas draws and times as a pass of its own.
constexpr NeuronClient::TextStyle PROGRESS_STYLE{L"Consolas", 15.0f, DWRITE_FONT_WEIGHT_NORMAL};
constexpr std::int32_t PROGRESS_MARGIN_PIXELS = 8;
constexpr float PROGRESS_PADDING_PIXELS = 6.0f;

enum class Depth : std::uint8_t
{
  Conservative, // SV_DepthLessEqual, as the renderer always draws (§9.3)
  Plain         // SV_Depth, the variant it is measured against
};

constexpr std::array<Depth, 2> DEPTHS{Depth::Conservative, Depth::Plain};
constexpr std::array<const char*, 2> DEPTH_NAMES{"conservative", "plain"};

enum class Phase : std::uint8_t
{
  Intact,
  Flight,
  Stopped
};

constexpr std::array<Phase, 3> PHASES{Phase::Intact, Phase::Flight, Phase::Stopped};
constexpr std::array<const char*, 3> PHASE_NAMES{"intact", "in flight", "drifted to a stop"};

// One measured frame: where on the timeline it was, how it was drawn, and what was measured of it.
struct Shot
{
  std::uint32_t index; // on the timeline
  Depth depth;
  Phase phase;
  float explosionSeconds; // since the detonation, on the world's clock; 0 while the station is intact
  float yawRadians;
  double intervalMilliseconds; // from this frame's start to the next one's, on the CPU's clock
  std::optional<NeuronClient::FrameStatistics> statistics;
};

struct Spread
{
  double median;
  double mean;
  double percentile95;
};

[[nodiscard]] Spread SpreadOf(std::vector<double> _values)
{
  if (_values.empty())
  {
    return {0.0, 0.0, 0.0};
  }
  std::ranges::sort(_values);
  const std::size_t count = _values.size();
  const double median = count % 2 == 1 ? _values[count / 2] : 0.5 * (_values[count / 2 - 1] + _values[count / 2]);
  const double mean = std::accumulate(_values.begin(), _values.end(), 0.0) / static_cast<double>(count);
  const auto rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(count)));
  return {median, mean, _values[std::clamp<std::size_t>(rank, 1, count) - 1]};
}

// A pass's time in a shot; 0 for a pass the frame did not run.
[[nodiscard]] double PassMilliseconds(const NeuronClient::FrameStatistics& _statistics, NeuronClient::GpuPass _pass) noexcept
{
  return _statistics.passMilliseconds[static_cast<std::size_t>(_pass)].value_or(0.0f);
}

// The frame of §8 without what only the bench adds: the two splats, the lighting, the sky, bloom and the tone map.
[[nodiscard]] double FramePassMilliseconds(const NeuronClient::FrameStatistics& _statistics) noexcept
{
  return PassMilliseconds(_statistics, NeuronClient::GpuPass::ShadowSplat) +
         PassMilliseconds(_statistics, NeuronClient::GpuPass::ViewSplat) + PassMilliseconds(_statistics, NeuronClient::GpuPass::Lighting) +
         PassMilliseconds(_statistics, NeuronClient::GpuPass::Sky) + PassMilliseconds(_statistics, NeuronClient::GpuPass::Bloom) +
         PassMilliseconds(_statistics, NeuronClient::GpuPass::ToneMap);
}

[[nodiscard]] double PixelShaderInvocationsPerCoveredPixel(const NeuronClient::FrameStatistics& _statistics) noexcept
{
  const std::uint64_t covered = _statistics.coveredPixels.value_or(0);
  return covered == 0 ? 0.0 : static_cast<double>(_statistics.pixelShaderInvocations) / static_cast<double>(covered);
}

// Gives each shot the statistics of its frame: frame _first is shot 0.
void Collect(const std::vector<NeuronClient::FrameStatistics>& _statistics, std::uint64_t _first, std::vector<Shot>& _shots)
{
  for (const NeuronClient::FrameStatistics& statistics : _statistics)
  {
    if (statistics.frame >= _first && statistics.frame - _first < _shots.size())
    {
      _shots[statistics.frame - _first].statistics = statistics;
    }
  }
}

// A figure over every measured shot drawn with _depth, and in _phase when one is given.
template <typename Figure>
[[nodiscard]] Spread SpreadOver(const std::vector<Shot>& _shots, Depth _depth, std::optional<Phase> _phase, Figure _figure)
{
  std::vector<double> values;
  for (const Shot& shot : _shots)
  {
    if (shot.statistics && shot.depth == _depth && (!_phase || shot.phase == *_phase))
    {
      values.push_back(_figure(shot));
    }
  }
  return SpreadOf(std::move(values));
}

// A figure of each timeline frame's plain shot over its conservative one, the two drawn back to back of the same scene.
template <typename Figure> [[nodiscard]] Spread RatioSpread(const std::vector<Shot>& _shots, Figure _figure)
{
  std::vector<double> ratios;
  for (std::size_t i = 0; i + 1 < _shots.size(); i += 2)
  {
    const Shot& conservative = _shots[i];
    const Shot& plain = _shots[i + 1];
    if (conservative.statistics && plain.statistics && _figure(conservative) > 0.0)
    {
      ratios.push_back(_figure(plain) / _figure(conservative));
    }
  }
  return SpreadOf(std::move(ratios));
}

[[nodiscard]] std::string Describe(const Spread& _spread, int _decimals)
{
  return std::format("{:.{}f} / {:.{}f} / {:.{}f}", _spread.median, _decimals, _spread.mean, _decimals, _spread.percentile95, _decimals);
}

[[nodiscard]] std::string LocalStamp()
{
  SYSTEMTIME time{};
  GetLocalTime(&time);
  return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
}

void WriteCsv(const std::filesystem::path& _path, const std::vector<Shot>& _shots)
{
  std::ofstream csv(_path);
  if (!csv)
  {
    throw std::runtime_error(std::format("--bench could not write {}.", winrt::to_string(_path.wstring())));
  }
  csv << "frame,depth,explosionSeconds,yawDegrees,shadowSplatMs,viewSplatMs,coverageMs,lightingMs,skyMs,bloomMs,toneMapMs,canvasMs,"
         "framePassesMs,gpuMs,"
         "intervalMs,vsInvocations,psInvocations,primitives,coveredPixels,psPerCoveredPixel,viewDrawn,viewCulled,shadowDrawn,"
         "shadowCulled,viewVoxels,shadowVoxels\n";
  for (const Shot& shot : _shots)
  {
    if (!shot.statistics)
    {
      continue;
    }
    const NeuronClient::FrameStatistics& statistics = *shot.statistics;
    csv << std::format(
      "{},{},{:.4f},{:.2f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{},{},{},{},{:.4f},{},{},{},{},{},{"
      "}\n",
      shot.index, DEPTH_NAMES[static_cast<std::size_t>(shot.depth)], shot.explosionSeconds,
      shot.yawRadians * 180.0f / std::numbers::pi_v<float>, PassMilliseconds(statistics, NeuronClient::GpuPass::ShadowSplat),
      PassMilliseconds(statistics, NeuronClient::GpuPass::ViewSplat), PassMilliseconds(statistics, NeuronClient::GpuPass::Coverage),
      PassMilliseconds(statistics, NeuronClient::GpuPass::Lighting), PassMilliseconds(statistics, NeuronClient::GpuPass::Sky),
      PassMilliseconds(statistics, NeuronClient::GpuPass::Bloom), PassMilliseconds(statistics, NeuronClient::GpuPass::ToneMap),
      PassMilliseconds(statistics, NeuronClient::GpuPass::Canvas), FramePassMilliseconds(statistics), statistics.gpuMilliseconds,
      shot.intervalMilliseconds, statistics.vertexShaderInvocations, statistics.pixelShaderInvocations, statistics.primitives,
      statistics.coveredPixels.value_or(0), PixelShaderInvocationsPerCoveredPixel(statistics), statistics.draws.viewDrawn,
      statistics.draws.viewCulled, statistics.draws.shadowDrawn, statistics.draws.shadowCulled, statistics.draws.viewVoxels,
      statistics.draws.shadowVoxels);
  }
  if (!csv)
  {
    throw std::runtime_error(std::format("--bench could not write {}.", winrt::to_string(_path.wstring())));
  }
}

[[nodiscard]] std::string Summarize(const std::vector<Shot>& _shots, std::uint32_t _seconds, std::uint32_t _frames,
                                    const std::string& _world, std::optional<float> _detonationSeconds, const std::string& _adapter,
                                    const std::filesystem::path& _csvPath)
{
  const auto pass = [](NeuronClient::GpuPass _pass)
  { return [_pass](const Shot& _shot) { return PassMilliseconds(*_shot.statistics, _pass); }; };
  const auto framePasses = [](const Shot& _shot) { return FramePassMilliseconds(*_shot.statistics); };
  const auto interval = [](const Shot& _shot) { return _shot.intervalMilliseconds; };
  const auto invocations = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->pixelShaderInvocations) / 1.0e6; };
  const auto perCovered = [](const Shot& _shot) { return PixelShaderInvocationsPerCoveredPixel(*_shot.statistics); };
  const auto covered = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->coveredPixels.value_or(0)) / 1.0e6; };
  const auto viewDrawn = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->draws.viewDrawn); };
  const auto viewCulled = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->draws.viewCulled); };
  const auto shadowDrawn = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->draws.shadowDrawn); };
  const auto shadowCulled = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->draws.shadowCulled); };
  const auto viewVoxels = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->draws.viewVoxels) / 1.0e6; };
  const auto shadowVoxels = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->draws.shadowVoxels) / 1.0e6; };

  std::string summary =
    std::format("Outpost --bench {}: {} frames of {} at {} x {}, the camera once round the station, vsync off, each frame drawn with "
                "conservative and with plain depth, after {} warm-up frames.\n",
                _seconds, _frames, _world, BENCH_WIDTH_PIXELS, BENCH_HEIGHT_PIXELS, WARMUP_FRAMES);
  summary += _detonationSeconds ? std::format("The server detonated the station {:.3f} s into the timeline.\n", *_detonationSeconds)
                                : std::string("The server did not detonate the station within the run.\n");
  summary += std::format("Adapter: {}\n\n", _adapter);
  summary += "Median / mean / 95th percentile, conservative depth | plain SV_Depth:\n";
  const auto row = [&](const char* _name, const auto& _figure, int _decimals, const char* _unit)
  {
    summary += std::format("{}: {} | {} {}\n", _name, Describe(SpreadOver(_shots, Depth::Conservative, std::nullopt, _figure), _decimals),
                           Describe(SpreadOver(_shots, Depth::Plain, std::nullopt, _figure), _decimals), _unit);
  };
  row("shadow splat", pass(NeuronClient::GpuPass::ShadowSplat), 3, "ms");
  row("view splat", pass(NeuronClient::GpuPass::ViewSplat), 3, "ms");
  row("lighting", pass(NeuronClient::GpuPass::Lighting), 3, "ms");
  row("sky", pass(NeuronClient::GpuPass::Sky), 3, "ms");
  row("bloom", pass(NeuronClient::GpuPass::Bloom), 3, "ms");
  row("tone map", pass(NeuronClient::GpuPass::ToneMap), 3, "ms");
  row("the frame's six passes", framePasses, 3, "ms");
  row("coverage count, the bench's only", pass(NeuronClient::GpuPass::Coverage), 3, "ms");
  row("canvas, this progress line", pass(NeuronClient::GpuPass::Canvas), 3, "ms");
  row("CPU frame interval, the server's steps included", interval, 3, "ms");
  row("view splat PSInvocations", invocations, 3, "M");
  row("PSInvocations per covered pixel", perCovered, 2, "");
  row("covered pixels", covered, 3, "M");
  row("placements the view drew", viewDrawn, 1, "");
  row("placements the view culled", viewCulled, 1, "");
  row("placements the sun drew", shadowDrawn, 1, "");
  row("placements the sun culled", shadowCulled, 1, "");
  row("voxels the view drew", viewVoxels, 3, "M");
  row("voxels the sun drew", shadowVoxels, 3, "M");
  summary += "\nPlain over conservative, each timeline frame's pair, median / mean / 95th percentile:\n";
  summary += std::format("view splat PSInvocations: {}\n", Describe(RatioSpread(_shots, invocations), 3));
  summary += std::format("view splat time: {}\n", Describe(RatioSpread(_shots, pass(NeuronClient::GpuPass::ViewSplat)), 3));
  summary += "\nView splat by phase, conservative depth, median / mean / 95th percentile:\n";
  for (const Phase phase : PHASES)
  {
    const auto frames =
      std::ranges::count_if(_shots, [phase](const Shot& _shot) { return _shot.phase == phase && _shot.depth == Depth::Conservative; });
    if (frames == 0)
    {
      summary += std::format("{}: no frames\n", PHASE_NAMES[static_cast<std::size_t>(phase)]);
      continue;
    }
    summary += std::format("{}, {} frames: {} ms, PSInvocations per covered pixel {}\n", PHASE_NAMES[static_cast<std::size_t>(phase)],
                           frames, Describe(SpreadOver(_shots, Depth::Conservative, phase, pass(NeuronClient::GpuPass::ViewSplat)), 3),
                           Describe(SpreadOver(_shots, Depth::Conservative, phase, perCovered), 2));
  }
  summary += std::format("\nEvery frame: {}\n", winrt::to_string(_csvPath.wstring()));
  return summary;
}

void ThrowOnRefusal(const std::expected<void, NeuronClient::SessionError>& _polled)
{
  if (!_polled)
  {
    throw std::runtime_error("The session with the server ended: " + NeuronClient::DescribeSessionError(_polled.error()));
  }
}

} // namespace

struct Bench::Run
{
  std::uint32_t seconds;
  std::uint32_t frames; // on the timeline
  std::string world;
  NeuronClient::GraphicsDeviceDesc device;
  NeuronClient::Window window;
  NeuronClient::ClientSession session;
  // Made by the first frame, once the welcome and the first snapshot have come.
  std::unique_ptr<Scene> scene;
  std::unique_ptr<OrbitCamera> camera;
  std::unique_ptr<NeuronClient::Renderer> renderer;
  std::uint32_t station = 0; // the entity the camera orbits and the bench detonates
  float startYawRadians = 0.0f;
  // The world's sky, made with the scene (Design/Archive/SpaceScene.md §11).
  std::vector<NeuronCore::StarRecord> stars;
  NeuronCore::SkyParameters sky{};
  // The run's progress: the shots drawn so far, warm-up included.
  std::uint32_t step = 0;
  bool detonationAsked = false;
  std::optional<float> detonationSeconds; // on the timeline, once the server's event has come
  std::uint64_t firstFrame = 0;           // the renderer's number for the first measured shot
  std::vector<Shot> shots;
  std::vector<SteadyClock::time_point> starts;
  bool closed = false;
  bool finished = false;

  Run(const GameOptions& _options, std::uint32_t _seconds, std::string _world, std::unique_ptr<NeuronCore::Transport> _transport)
    : seconds(_seconds),
      frames(std::max(_seconds * FRAMES_PER_SECOND, 1u)),
      world(std::move(_world)),
      device(_options.device),
      window({L"Outpost", _options.windowSize}),
      session(std::move(_transport), _options.modelDirectory)
  {
    shots.reserve(DEPTHS.size() * frames);
    starts.reserve(shots.capacity() + 1);
  }

  // The timeline frame of shot _step: the warm-up draws frame 0, then every frame is drawn in both variants.
  [[nodiscard]] std::uint32_t IndexOf(std::uint32_t _step) const noexcept
  {
    return _step < WARMUP_FRAMES ? 0 : (_step - WARMUP_FRAMES) / static_cast<std::uint32_t>(DEPTHS.size());
  }

  [[nodiscard]] std::uint32_t TickRate() const noexcept
  {
    return session.IsWelcomed() ? session.Buffer().TickRate() : 1u;
  }

  // Frame _index of the timeline is at _index / 60 s past the first snapshot's tick, on the server's clock.
  [[nodiscard]] double RenderTick(std::uint32_t _index) const noexcept
  {
    return 1.0 + static_cast<double>(_index) * TickRate() / FRAMES_PER_SECOND;
  }

  // What the first frame needs: the models and the station, the camera, the sun's view and the renderer.
  void Begin()
  {
    const NeuronCore::WorldSettings& settings = session.Settings();
    scene = std::make_unique<Scene>(session.Models(), settings.toSun);
    stars = NeuronCore::MakeStarCatalog(settings.skySeed, settings.galacticPlane, NeuronCore::STAR_COUNT);
    sky = NeuronCore::MakeSkyParameters(settings);
    const NeuronClient::WorldSample sample = session.Buffer().Sample(RenderTick(0));
    if (sample.entities.empty())
    {
      throw std::runtime_error("--bench found no station to orbit in the server's world.");
    }
    const NeuronClient::SampledEntity& first = sample.entities.front();
    station = first.id;
    const NeuronCore::Sphere framed = scene->Models().Extent(first);
    camera = std::make_unique<OrbitCamera>(framed.center, framed.radius);
    startYawRadians = camera->YawRadians();
    scene->FitShadowView(sample);
    renderer = std::make_unique<NeuronClient::Renderer>(
      NeuronClient::RendererDesc{device, window.Handle(), BENCH_WIDTH_PIXELS, BENCH_HEIGHT_PIXELS, scene->ShadowView(), stars},
      scene->Models().Models(), scene->Models().Fragments());
  }

  // Draws shot _step: frame _index of the timeline with _depth, and a measured shot unless it is a warm-up one.
  void Draw(std::uint32_t _index, Depth _depth, bool _measured)
  {
    const double fraction = static_cast<double>(_index) / static_cast<double>(frames);
    if (_measured && !detonationAsked && fraction >= DETONATION_FRACTION)
    {
      // The server applies it at its next tick, and its event poses the debris from then on (§5.5).
      session.Send({NeuronCore::CommandKind::Detonate, station});
      detonationAsked = true;
    }
    const NeuronClient::WorldSample sample = session.Buffer().Sample(RenderTick(_index));
    const std::vector<NeuronCore::Placement> placements = scene->Place(sample);
    const auto entity = std::ranges::find(sample.entities, station, &NeuronClient::SampledEntity::id);
    const std::optional<NeuronClient::SampledDetonation> detonation =
      entity != sample.entities.end() ? entity->detonation : std::optional<NeuronClient::SampledDetonation>{};
    float explosionSeconds = 0.0f;
    Phase phase = Phase::Intact;
    if (detonation.has_value() && detonation->seconds > 0.0f)
    {
      explosionSeconds = detonation->seconds;
      float stopSeconds = 0.0f;
      for (const NeuronCore::Placement& placement : placements)
      {
        if (placement.detonation)
        {
          stopSeconds = std::max(stopSeconds, NeuronCore::PlacementEnvelope(placement, *placement.detonation).stopSeconds);
        }
      }
      phase = explosionSeconds < stopSeconds ? Phase::Flight : Phase::Stopped;
      if (!detonationSeconds)
      {
        // The timeline starts at the first snapshot's tick, and the world ticks with the clock while nothing pauses it.
        detonationSeconds =
          static_cast<float>(static_cast<double>(std::max<std::uint64_t>(detonation->event.worldTick, 1) - 1) / TickRate());
      }
    }
    const float yawRadians = startYawRadians + static_cast<float>(2.0 * std::numbers::pi * fraction);
    camera->SetYawRadians(yawRadians);
    if (_measured)
    {
      shots.push_back({_index, _depth, phase, explosionSeconds, yawRadians, 0.0, std::nullopt});
    }
    if (scene->FitShadowView(sample))
    {
      renderer->SetShadowView(scene->ShadowView());
    }

    const std::wstring progress = _measured ? std::format(L"--bench {} s: frame {} of {}, {} depth", seconds, _index + 1, frames,
                                                          _depth == Depth::Plain ? L"plain" : L"conservative")
                                            : std::wstring(L"--bench: warming up");
    NeuronClient::Canvas& canvas = renderer->Overlay();
    const NeuronClient::TextExtent extent = canvas.Measure(progress, PROGRESS_STYLE);
    canvas.FillRectangle(PROGRESS_MARGIN_PIXELS, PROGRESS_MARGIN_PIXELS,
                         static_cast<std::uint32_t>(std::ceil(extent.widthPixels + 2.0f * PROGRESS_PADDING_PIXELS)),
                         static_cast<std::uint32_t>(std::ceil(extent.heightPixels + 2.0f * PROGRESS_PADDING_PIXELS)), {0.0f, 0.0f, 0.0f},
                         0.55f);
    canvas.Print(progress, static_cast<float>(PROGRESS_MARGIN_PIXELS) + PROGRESS_PADDING_PIXELS,
                 static_cast<float>(PROGRESS_MARGIN_PIXELS) + PROGRESS_PADDING_PIXELS, PROGRESS_STYLE, {1.0f, 1.0f, 1.0f}, 1.0f);
    const NeuronCore::LightingParameters lighting = NeuronCore::MakeLightingParameters(session.Settings(), 1.0f);
    renderer->Render(camera->View(BENCH_WIDTH_PIXELS, BENCH_HEIGHT_PIXELS), placements,
                     {std::nullopt, lighting, sky, EXPOSURE, false, _depth == Depth::Plain, true});
  }

  void Frame()
  {
    const std::uint32_t measuredStep = step < WARMUP_FRAMES ? 0 : step - WARMUP_FRAMES;
    if (step >= WARMUP_FRAMES)
    {
      starts.push_back(SteadyClock::now());
    }
    if (!window.PumpMessages())
    {
      closed = true;
      return;
    }
    window.Input().EndFrame();
    // The bench owns the clock, so the snapshots arrive when the timeline says.
    ThrowOnRefusal(session.Poll(static_cast<double>(IndexOf(step)) / FRAMES_PER_SECOND));
    if (!renderer)
    {
      Begin();
    }
    if (step == WARMUP_FRAMES)
    {
      renderer->FinishFrames();
      static_cast<void>(renderer->TakeStatistics());
      firstFrame = renderer->NextFrame();
    }
    const bool measured = step >= WARMUP_FRAMES;
    Draw(IndexOf(step), DEPTHS[(measured ? measuredStep : step) % DEPTHS.size()], measured);
    if (measured)
    {
      Collect(renderer->TakeStatistics(), firstFrame, shots);
    }
    ++step;
    if (step == WARMUP_FRAMES + DEPTHS.size() * frames)
    {
      renderer->FinishFrames();
      starts.push_back(SteadyClock::now());
      Collect(renderer->TakeStatistics(), firstFrame, shots);
      for (std::size_t i = 0; i < shots.size(); ++i)
      {
        shots[i].intervalMilliseconds = std::chrono::duration<double, std::milli>(starts[i + 1] - starts[i]).count();
      }
      finished = true;
    }
  }
};

Bench::Bench(const GameOptions& _options, std::uint32_t _seconds, std::string _world, std::unique_ptr<NeuronCore::Transport> _transport)
  : m_run(std::make_unique<Run>(_options, _seconds, std::move(_world), std::move(_transport)))
{
}

// Here, where Run is whole, as the unique_ptr to it needs; an empty body rather than a default one, which
// performance-trivially-destructible mistakes for a destructor that could be trivial.
Bench::~Bench() {}

std::optional<std::uint64_t> Bench::TickNeeded() const noexcept
{
  if (m_run->closed || m_run->finished)
  {
    return std::nullopt;
  }
  // The snapshot at or after the frame's render tick: 1 + ceil(index × rate / 60), in whole numbers.
  const std::uint64_t index = m_run->IndexOf(m_run->step);
  return 1 + (index * m_run->TickRate() + FRAMES_PER_SECOND - 1) / FRAMES_PER_SECOND;
}

void Bench::Frame()
{
  try
  {
    m_run->Frame();
  }
  catch (...)
  {
    // The device still exists here, so a removal can still be asked about.
    std::string message = NeuronClient::DescribeCurrentException();
    if (m_run->renderer)
    {
      const std::string removal = m_run->renderer->Device().DescribeRemoval();
      if (!removal.empty())
      {
        message += "\n" + removal;
      }
    }
    throw std::runtime_error(message);
  }
}

bool Bench::Finish()
{
  const Run& run = *m_run;
  if (!run.finished)
  {
    return false;
  }
  const std::string stamp = LocalStamp();
  const std::filesystem::path csvPath = std::filesystem::current_path() / std::format("Outpost-bench-{}.csv", stamp);
  const std::filesystem::path summaryPath = std::filesystem::current_path() / std::format("Outpost-bench-{}.txt", stamp);
  WriteCsv(csvPath, run.shots);
  const std::string summary = Summarize(run.shots, run.seconds, run.frames, run.world, run.detonationSeconds,
                                        winrt::to_string(run.renderer->Device().AdapterName()), csvPath);
  std::ofstream text(summaryPath, std::ios::binary);
  text << summary;
  if (!text)
  {
    throw std::runtime_error(std::format("--bench could not write {}.", winrt::to_string(summaryPath.wstring())));
  }
  const std::wstring shown(winrt::to_hstring(summary));
  OutputDebugStringW(shown.c_str());
  MessageBoxW(run.window.Handle(), shown.c_str(), L"Outpost --bench", MB_OK | MB_ICONINFORMATION);
  return true;
}

} // namespace GameLib
