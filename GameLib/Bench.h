#pragma once

#include "Renderer.h"
#include "Window.h"

#include "Scene.h"

#include "RenderSettings.h"

#include <cstdint>
#include <optional>
#include <string>

namespace GameLib
{

// --bench renders at this size whatever the window or the display, and the swap chain stretches it over the window
// (Design/Archive/SampleRenderer.md §13, D11).
inline constexpr std::uint32_t BENCH_WIDTH_PIXELS = 1920;
inline constexpr std::uint32_t BENCH_HEIGHT_PIXELS = 1080;

// --bench <seconds> (§13): a fixed camera path and detonation timeline, _seconds long at 60 frames a second however
// long a frame takes, so that every run draws the same frames; vsync off. The detonation runs to _stopSeconds, the
// envelope's stop time (Design/SpaceScene.md §5.5). Each frame is drawn twice, with the view splat's conservative depth
// and with plain SV_Depth (§9.3), and the pixels it covers are counted (§14). Every drawn frame's GPU timings, view-splat
// statistics and draw counts (Design/SpaceScene.md §7.4) go to a CSV file in the working directory, and a summary to a
// text file beside it. Returns the summary, or nothing when the window was closed first. _renderer must render at
// BENCH_WIDTH_PIXELS by BENCH_HEIGHT_PIXELS.
[[nodiscard]] std::optional<std::wstring> RunBench(NeuronClient::Window& _window, NeuronClient::Renderer& _renderer, const Scene& _scene,
                                                   const NeuronCore::RenderSettings& _settings, float _stopSeconds, std::uint32_t _seconds);

} // namespace GameLib
