#include "pch.h"

#include "CommandLine.h"

#include <optional>
#include <vector>

namespace NvfImport
{
namespace
{

// _text as UTF-8, whatever the console's code page.
[[nodiscard]] std::string Utf8(const std::wstring& _text)
{
  const std::u8string text = std::filesystem::path(_text).u8string();
  return {text.begin(), text.end()};
}

} // namespace

std::expected<Options, std::string> ParseCommandLine(std::span<const std::wstring> _arguments)
{
  std::optional<Mode> flag;
  std::vector<std::filesystem::path> paths;
  for (const std::wstring& argument : _arguments)
  {
    const std::optional<Mode> mode = argument == L"--replace" ? std::optional(Mode::Replace)
                                     : argument == L"--check" ? std::optional(Mode::Check)
                                     : argument == L"--dump"  ? std::optional(Mode::Dump)
                                                              : std::nullopt;
    if (mode)
    {
      if (flag)
      {
        return std::unexpected("--replace, --check and --dump each stand alone; give one of them at most.");
      }
      flag = mode;
    }
    else if (argument.starts_with(L"--"))
    {
      return std::unexpected("Unknown option: " + Utf8(argument) + ".");
    }
    else
    {
      paths.emplace_back(argument);
    }
  }
  if (flag == Mode::Dump)
  {
    if (paths.size() != 1)
    {
      return std::unexpected("--dump takes one .nvf file.");
    }
    return Options{Mode::Dump, paths[0], {}};
  }
  if (paths.size() != 2)
  {
    return std::unexpected("An import takes a .vox file and an .nvf file.");
  }
  return Options{flag.value_or(Mode::Import), paths[0], paths[1]};
}

} // namespace NvfImport
