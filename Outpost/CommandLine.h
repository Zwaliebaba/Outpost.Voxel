#pragma once

#include "Game.h"

#include "Sector.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace Outpost
{

// What the command line asks for (Design/SpaceScene.md §13, §14): how the client runs, the world the server simulates,
// and --bench, which runs the one-station preset in place of the world the other options describe.
struct Options
{
  GameLib::GameOptions game;
  GameLogic::SectorParameters world;
  std::optional<std::uint32_t> benchSeconds; // the timeline's length in seconds, rather than the interactive client
};

// Parses the arguments after the program's name. On a mistake, the message says what was wrong and how the line is used.
[[nodiscard]] std::expected<Options, std::wstring> ParseCommandLine(std::span<const std::wstring> _arguments);

} // namespace Outpost
