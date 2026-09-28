#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace NvfImport
{

// What the command line asks for (Design/NeuronVoxelFormat.md §6.3).
enum class Mode : std::uint8_t
{
  Import,  // write the .nvf from the .vox, merging with the .nvf already there
  Replace, // write the .nvf from the .vox, discarding the .nvf already there
  Check,   // say whether importing would change the .nvf, and change nothing
  Dump     // print an .nvf as text
};

struct Options
{
  Mode mode;
  std::filesystem::path input;  // the .vox; for Dump, the .nvf
  std::filesystem::path output; // the .nvf; empty for Dump
};

inline constexpr std::string_view USAGE = "Usage:\n"
                                          "  NvfImport <input.vox> <output.nvf>            import; merges with output.nvf if it exists\n"
                                          "  NvfImport <input.vox> <output.nvf> --replace  import, discarding output.nvf's hardpoints\n"
                                          "  NvfImport <input.vox> <output.nvf> --check    exit 1 if importing would change output.nvf\n"
                                          "  NvfImport --dump <file.nvf>                   print parts, pivots and hardpoints as text\n";

// Parses the arguments after the program's name. On a mistake, the message says what was wrong.
[[nodiscard]] std::expected<Options, std::string> ParseCommandLine(std::span<const std::wstring> _arguments);

} // namespace NvfImport
