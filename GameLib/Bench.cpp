#include "pch.h"

#include "Bench.h"

#include "Canvas.h"
#include "FrameQueries.h"

#include "OrbitCamera.h"

#include "Lighting.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <numeric>
#include <stdexcept>
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

// The timeline in fractions of the run: intact until the detonation, the explosion running to rest until REST_FRACTION,
// then at rest. The camera turns once around the model over the whole run.
constexpr double DETONATION_FRACTION = 0.25;
constexpr double REST_FRACTION = 0.75;

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
  Rest
};

constexpr std::array<const char*, 3> PHASE_NAMES{"intact", "in flight", "at rest"};

// One measured frame: where on the timeline it was, how it was drawn, and what was measured of it.
struct Shot
{
  std::uint32_t index; // on the timeline
  Depth depth;
  Phase phase;
  float explosionSeconds;
  float yawRadians;
  double intervalMilliseconds; // from this frame's Render to the next one's, on the CPU's clock
  std::optional<NeuronClient::FrameStatistics> statistics;
};

struct Spread
{
  double median;
  double mean;
  double percentile95;
};

[[nodiscard]] Phase PhaseOf(double _fraction) noexcept
{
  if (_fraction < DETONATION_FRACTION)
  {
    return Phase::Intact;
  }
  return _fraction < REST_FRACTION ? Phase::Flight : Phase::Rest;
}

[[nodiscard]] float ExplosionSeconds(double _fraction, float _restSeconds) noexcept
{
  switch (PhaseOf(_fraction))
  {
  case Phase::Intact:
    return 0.0f;
  case Phase::Flight:
    return static_cast<float>((_fraction - DETONATION_FRACTION) / (REST_FRACTION - DETONATION_FRACTION)) * _restSeconds;
  case Phase::Rest:
    break;
  }
  return _restSeconds;
}

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

// The frame of §8 without what only the bench adds: the two splats, the lighting and the tone map.
[[nodiscard]] double FramePassMilliseconds(const NeuronClient::FrameStatistics& _statistics) noexcept
{
  return PassMilliseconds(_statistics, NeuronClient::GpuPass::ShadowSplat) +
         PassMilliseconds(_statistics, NeuronClient::GpuPass::ViewSplat) + PassMilliseconds(_statistics, NeuronClient::GpuPass::Lighting) +
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
  csv << "frame,depth,explosionSeconds,yawDegrees,shadowSplatMs,viewSplatMs,coverageMs,lightingMs,toneMapMs,canvasMs,framePassesMs,gpuMs,"
         "intervalMs,vsInvocations,psInvocations,primitives,coveredPixels,psPerCoveredPixel\n";
  for (const Shot& shot : _shots)
  {
    if (!shot.statistics)
    {
      continue;
    }
    const NeuronClient::FrameStatistics& statistics = *shot.statistics;
    csv << std::format(
      "{},{},{:.4f},{:.2f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{},{},{},{},{:.4f}\n", shot.index,
      DEPTH_NAMES[static_cast<std::size_t>(shot.depth)], shot.explosionSeconds, shot.yawRadians * 180.0f / std::numbers::pi_v<float>,
      PassMilliseconds(statistics, NeuronClient::GpuPass::ShadowSplat), PassMilliseconds(statistics, NeuronClient::GpuPass::ViewSplat),
      PassMilliseconds(statistics, NeuronClient::GpuPass::Coverage), PassMilliseconds(statistics, NeuronClient::GpuPass::Lighting),
      PassMilliseconds(statistics, NeuronClient::GpuPass::ToneMap), PassMilliseconds(statistics, NeuronClient::GpuPass::Canvas),
      FramePassMilliseconds(statistics), statistics.gpuMilliseconds, shot.intervalMilliseconds, statistics.vertexShaderInvocations,
      statistics.pixelShaderInvocations, statistics.primitives, statistics.coveredPixels.value_or(0),
      PixelShaderInvocationsPerCoveredPixel(statistics));
  }
  if (!csv)
  {
    throw std::runtime_error(std::format("--bench could not write {}.", winrt::to_string(_path.wstring())));
  }
}

[[nodiscard]] std::string Summarize(const std::vector<Shot>& _shots, std::uint32_t _seconds, std::uint32_t _frames,
                                    const std::string& _adapter, const std::filesystem::path& _csvPath)
{
  const auto pass = [](NeuronClient::GpuPass _pass)
  { return [_pass](const Shot& _shot) { return PassMilliseconds(*_shot.statistics, _pass); }; };
  const auto framePasses = [](const Shot& _shot) { return FramePassMilliseconds(*_shot.statistics); };
  const auto interval = [](const Shot& _shot) { return _shot.intervalMilliseconds; };
  const auto invocations = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->pixelShaderInvocations) / 1.0e6; };
  const auto perCovered = [](const Shot& _shot) { return PixelShaderInvocationsPerCoveredPixel(*_shot.statistics); };
  const auto covered = [](const Shot& _shot) { return static_cast<double>(_shot.statistics->coveredPixels.value_or(0)) / 1.0e6; };

  std::string summary =
    std::format("Outpost --bench {}: {} frames of a fixed camera path and explosion timeline at {} x {}, vsync off, each "
                "drawn with conservative and with plain depth, after {} warm-up frames.\n",
                _seconds, _frames, BENCH_WIDTH_PIXELS, BENCH_HEIGHT_PIXELS, WARMUP_FRAMES);
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
  row("tone map", pass(NeuronClient::GpuPass::ToneMap), 3, "ms");
  row("the frame's four passes", framePasses, 3, "ms");
  row("coverage count, the bench's only", pass(NeuronClient::GpuPass::Coverage), 3, "ms");
  row("canvas, this progress line", pass(NeuronClient::GpuPass::Canvas), 3, "ms");
  row("CPU frame interval", interval, 3, "ms");
  row("view splat PSInvocations", invocations, 3, "M");
  row("PSInvocations per covered pixel", perCovered, 2, "");
  row("covered pixels", covered, 3, "M");
  summary += "\nPlain over conservative, each timeline frame's pair, median / mean / 95th percentile:\n";
  summary += std::format("view splat PSInvocations: {}\n", Describe(RatioSpread(_shots, invocations), 3));
  summary += std::format("view splat time: {}\n", Describe(RatioSpread(_shots, pass(NeuronClient::GpuPass::ViewSplat)), 3));
  summary += "\nView splat by phase, conservative depth, median / mean / 95th percentile:\n";
  for (const Phase phase : {Phase::Intact, Phase::Flight, Phase::Rest})
  {
    summary += std::format("{}: {} ms, PSInvocations per covered pixel {}\n", PHASE_NAMES[static_cast<std::size_t>(phase)],
                           Describe(SpreadOver(_shots, Depth::Conservative, phase, pass(NeuronClient::GpuPass::ViewSplat)), 3),
                           Describe(SpreadOver(_shots, Depth::Conservative, phase, perCovered), 2));
  }
  summary += std::format("\nEvery frame: {}\n", winrt::to_string(_csvPath.wstring()));
  return summary;
}

} // namespace

std::optional<std::wstring> RunBench(NeuronClient::Window& _window, NeuronClient::Renderer& _renderer, const Scene& _scene,
                                     const NeuronCore::RenderSettings& _settings, float _restSeconds, std::uint32_t _seconds)
{
  const std::uint32_t frames = std::max(_seconds * FRAMES_PER_SECOND, 1u);
  OrbitCamera camera(_scene.center, _scene.radius);
  const float startYawRadians = camera.YawRadians();
  const NeuronCore::LightingParameters lighting = NeuronCore::MakeLightingParameters(_settings, 1.0f);
  std::vector<Shot> shots;
  shots.reserve(DEPTHS.size() * frames);

  // Draws timeline frame _index with _depth, as a measured shot when _measured; false once the window has been closed.
  const auto draw = [&](std::uint32_t _index, Depth _depth, bool _measured) -> bool
  {
    if (!_window.PumpMessages())
    {
      return false;
    }
    _window.Input().EndFrame();
    const double fraction = static_cast<double>(_index) / static_cast<double>(frames);
    const float yawRadians = startYawRadians + static_cast<float>(2.0 * std::numbers::pi * fraction);
    const float explosionSeconds = ExplosionSeconds(fraction, _restSeconds);
    camera.SetYawRadians(yawRadians);
    if (_measured)
    {
      shots.push_back({_index, _depth, PhaseOf(fraction), explosionSeconds, yawRadians, 0.0, std::nullopt});
    }
    const std::wstring progress = _measured ? std::format(L"--bench {} s: frame {} of {}, {} depth", _seconds, _index + 1, frames,
                                                          _depth == Depth::Plain ? L"plain" : L"conservative")
                                            : std::wstring(L"--bench: warming up");
    NeuronClient::Canvas& canvas = _renderer.Overlay();
    const NeuronClient::TextExtent extent = canvas.Measure(progress, PROGRESS_STYLE);
    canvas.FillRectangle(PROGRESS_MARGIN_PIXELS, PROGRESS_MARGIN_PIXELS,
                         static_cast<std::uint32_t>(std::ceil(extent.widthPixels + 2.0f * PROGRESS_PADDING_PIXELS)),
                         static_cast<std::uint32_t>(std::ceil(extent.heightPixels + 2.0f * PROGRESS_PADDING_PIXELS)), {0.0f, 0.0f, 0.0f},
                         0.55f);
    canvas.Print(progress, static_cast<float>(PROGRESS_MARGIN_PIXELS) + PROGRESS_PADDING_PIXELS,
                 static_cast<float>(PROGRESS_MARGIN_PIXELS) + PROGRESS_PADDING_PIXELS, PROGRESS_STYLE, {1.0f, 1.0f, 1.0f}, 1.0f);
    _renderer.Render(camera.View(BENCH_WIDTH_PIXELS, BENCH_HEIGHT_PIXELS),
                     {std::nullopt, lighting, _settings.exposure, explosionSeconds, false, _depth == Depth::Plain, true});
    return true;
  };

  for (std::uint32_t i = 0; i < WARMUP_FRAMES; ++i)
  {
    if (!draw(0, DEPTHS[i % DEPTHS.size()], false))
    {
      return std::nullopt;
    }
  }
  _renderer.FinishFrames();
  static_cast<void>(_renderer.TakeStatistics());

  const std::uint64_t first = _renderer.NextFrame();
  std::vector<SteadyClock::time_point> starts;
  starts.reserve(shots.capacity() + 1);
  for (std::uint32_t index = 0; index < frames; ++index)
  {
    for (const Depth depth : DEPTHS)
    {
      starts.push_back(SteadyClock::now());
      if (!draw(index, depth, true))
      {
        return std::nullopt;
      }
      Collect(_renderer.TakeStatistics(), first, shots);
    }
  }
  _renderer.FinishFrames();
  starts.push_back(SteadyClock::now());
  Collect(_renderer.TakeStatistics(), first, shots);
  for (std::size_t i = 0; i < shots.size(); ++i)
  {
    shots[i].intervalMilliseconds = std::chrono::duration<double, std::milli>(starts[i + 1] - starts[i]).count();
  }

  const std::string stamp = LocalStamp();
  const std::filesystem::path csvPath = std::filesystem::current_path() / std::format("Outpost-bench-{}.csv", stamp);
  const std::filesystem::path summaryPath = std::filesystem::current_path() / std::format("Outpost-bench-{}.txt", stamp);
  WriteCsv(csvPath, shots);
  const std::string summary = Summarize(shots, _seconds, frames, winrt::to_string(_renderer.Device().AdapterName()), csvPath);
  std::ofstream text(summaryPath, std::ios::binary);
  text << summary;
  if (!text)
  {
    throw std::runtime_error(std::format("--bench could not write {}.", winrt::to_string(summaryPath.wstring())));
  }
  return std::wstring(winrt::to_hstring(summary));
}

} // namespace GameLib
