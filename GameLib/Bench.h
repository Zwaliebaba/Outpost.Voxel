#pragma once

#include "Game.h"

#include "Transport.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace GameLib
{

// --bench renders at this size whatever the window or the display, and the swap chain stretches it over the window
// (Design/Archive/SampleRenderer.md §13, D11).
inline constexpr std::uint32_t BENCH_WIDTH_PIXELS = 1920;
inline constexpr std::uint32_t BENCH_HEIGHT_PIXELS = 1080;

// --bench <seconds> (Design/SpaceScene.md §14), on the one-station preset until S-M8's space bench: one station and no
// ships, the camera orbiting the station at the default framing, once round over the run. The bench owns time. Frame i
// of the timeline is at i / 60 s however long a frame takes, and before it is drawn, whoever runs the server steps it
// until it has sent the tick the frame needs (TickNeeded). No thread runs and no wall clock enters, so every run of one
// build draws the same frames. A quarter of the way through, the bench asks the server to detonate the station, as E
// does, and poses the debris from the server's event: the station is intact, its debris in flight, then drifted to a
// stop, which a run shorter than a quarter of it plus the envelope's stop time never reaches. Each frame is drawn twice,
// with the view splat's conservative depth and with plain SV_Depth (Design/Archive/SampleRenderer.md §9.3), and the
// pixels it covers are counted. Every drawn frame's GPU timings, view-splat statistics and draw counts (§7.4) go to a CSV
// file in the working directory, and a summary to a text file beside it.
class Bench
{
public:
  // Opens the window and says Hello through _transport, the client's end of its link with the server (AGENTS.md R18).
  // The timeline is _seconds long; _world says what the server simulates, for the summary.
  Bench(const GameOptions& _options, std::uint32_t _seconds, std::string _world, std::unique_ptr<NeuronCore::Transport> _transport);
  ~Bench();
  Bench(const Bench&) = delete;
  Bench& operator=(const Bench&) = delete;
  Bench(Bench&&) = delete;
  Bench& operator=(Bench&&) = delete;

  // The server tick whose snapshot the next frame needs to have been sent: its caller steps the server until it has sent
  // it, then calls Frame. Nothing once the run is over, or the window has been closed.
  [[nodiscard]] std::optional<std::uint64_t> TickNeeded() const noexcept;

  // Takes what the server sent and draws the next frame. Throws std::runtime_error when the session ends, or with the
  // whole story of a GPU failure.
  void Frame();

  // Once the run is over: writes the CSV and the summary, and shows the summary in the debugger's output and over the
  // window. False when the window was closed first.
  bool Finish();

private:
  struct Run;
  std::unique_ptr<Run> m_run;
};

} // namespace GameLib
