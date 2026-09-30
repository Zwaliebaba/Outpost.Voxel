#pragma once

#include "Game.h"

#include "Sector.h"
#include "Skirmish.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Outpost
{

// What the command line asks for (Design/Archive/SpaceScene.md §13, §14): how the client runs, the world the server simulates,
// and --bench, which runs the one-station preset in place of the world the other options describe.
struct Options
{
  GameLib::GameOptions game;
  GameLogic::SectorParameters world;                     // the space scene's sector, which the server simulates by default
  std::optional<GameLogic::SkirmishParameters> skirmish; // --skirmish: the MVP's skirmish in its place (Design/ADR/ADR-030),
                                                         // with --battle, each side's battle fleet (Design/ADR/ADR-035)
  std::optional<std::uint32_t> benchSeconds;             // the timeline's length in seconds, rather than the interactive client
  bool observe;                                 // --observe: the window observes the skirmish rather than play side 1 (Design/ADR/ADR-032)
  std::optional<std::filesystem::path> logFile; // --log: the skirmish's command log, written as it runs
  std::optional<std::filesystem::path> replayFile; // --replay: the log to replay without a window, in place of everything else
};

// Parses the arguments after the program's name. On a mistake, the message says what was wrong and how the line is used.
[[nodiscard]] std::expected<Options, std::wstring> ParseCommandLine(std::span<const std::wstring> _arguments);

} // namespace Outpost
