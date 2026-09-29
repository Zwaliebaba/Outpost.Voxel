#pragma once

#include "GraphicsDevice.h"
#include "Window.h"

#include "Transport.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace GameLib
{

// --capture <file>.png --capture-at <seconds> (Design/MvpPlan.md §3.3, Design/ADR/ADR-031): the frame drawn at that time
// of the world's clock, written to the file through WIC.
struct CaptureOptions
{
  std::filesystem::path file;
  double worldSeconds; // after the world's tick 0
};

// How the client runs, from the command line (Design/Archive/SpaceScene.md §13).
struct GameOptions
{
  std::filesystem::path modelDirectory;               // where the client reads the models the server's welcome names (§6.2)
  std::optional<NeuronClient::ClientSize> windowSize; // an ordinary window of this size, rather than borderless fullscreen
  NeuronClient::GraphicsDeviceDesc device;
  std::optional<CaptureOptions> capture; // one frame to a file, after which the client closes
  bool overview; // the first view frames every entity and targets none, rather than framing the first (Design/ADR/ADR-030)
};

// Runs the client until its window closes (§13), or with a capture until it has written its frame. The server runs on a
// thread of its own, and everything it knows reaches the client as messages through _transport, the client's end of their
// link (AGENTS.md R18). A failure throws std::runtime_error with the whole story: a session the client refused and why, or
// the HRESULT, where it was checked, and, when the device was lost, what DRED and the debug layer recorded.
void RunGame(const GameOptions& _options, std::unique_ptr<NeuronCore::Transport> _transport);

} // namespace GameLib
