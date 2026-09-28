#pragma once

#include "Game.h"

#include <expected>
#include <span>
#include <string>

namespace Outpost
{

// The command line of Design/SampleRenderer.md §13, arguments after the program's name. On a mistake, the message
// says what was wrong and how the line is used.
[[nodiscard]] std::expected<GameLib::GameOptions, std::wstring> ParseCommandLine(std::span<const std::wstring> _arguments);

} // namespace Outpost
