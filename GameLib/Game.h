#pragma once

#include "GraphicsDevice.h"
#include "Window.h"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace GameLib
{

// How the client runs, from the command line (Design/SampleRenderer.md §13).
struct GameOptions
{
  std::filesystem::path voxPath;
  std::optional<NeuronClient::ClientSize> windowSize; // an ordinary window of this size, rather than borderless fullscreen
  NeuronClient::GraphicsDeviceDesc device;
  std::optional<std::uint32_t> benchSeconds; // --bench: the timeline's length in seconds, rather than the interactive client
};

// Runs the client until its window closes. A failure throws std::runtime_error with the whole story: the HRESULT, where
// it was checked, and, when the device was lost, what DRED and the debug layer recorded.
void RunGame(const GameOptions& _options);

} // namespace GameLib
