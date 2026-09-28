#pragma once

#include "GraphicsDevice.h"
#include "Window.h"

#include "Transport.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace GameLib
{

// How the client runs, from the command line (Design/SpaceScene.md §13).
struct GameOptions
{
  std::filesystem::path modelDirectory;               // where the client reads the models the server's welcome names (§6.2)
  std::optional<NeuronClient::ClientSize> windowSize; // an ordinary window of this size, rather than borderless fullscreen
  NeuronClient::GraphicsDeviceDesc device;
};

// Runs the client until its window closes (§13). The server runs on a thread of its own, and everything it knows reaches
// the client as messages through _transport, the client's end of their link (AGENTS.md R18). A failure throws
// std::runtime_error with the whole story: a session the client refused and why, or the HRESULT, where it was checked,
// and, when the device was lost, what DRED and the debug layer recorded.
void RunGame(const GameOptions& _options, std::unique_ptr<NeuronCore::Transport> _transport);

} // namespace GameLib
