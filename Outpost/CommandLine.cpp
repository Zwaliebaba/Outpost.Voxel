#include "pch.h"

#include "CommandLine.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string_view>

namespace Outpost
{
namespace
{

// A Debug build always asks for the debug layer; a Release build only with --d3d-debug.
#ifdef _DEBUG
constexpr bool DEBUG_BUILD = true;
#else
constexpr bool DEBUG_BUILD = false;
#endif

constexpr std::wstring_view USAGE =
  L"Outpost.exe [--seed <n>] [--stations <n>] [--frigates <n>] [--capitals <n>] [--debris-lifetime "
  L"<seconds>] [--size <width>x<height>] [--warp | --adapter <n>] [--d3d-debug] [--gbv] [--bench <seconds>]";

// --bench's timeline runs at 60 frames a second, so an hour is 216,000 frames in each of the two variants.
constexpr std::uint32_t BENCH_SECONDS_MAXIMUM = 3600;

// The most of each the command line takes: far more stations than the world's bound holds, which the sector refuses by
// name (Design/Archive/SpaceScene.md §5.2), and fewer ships than would fill memory before anything is drawn.
constexpr std::uint32_t STATIONS_MAXIMUM = 1024;
constexpr std::uint32_t SHIPS_MAXIMUM = 100000;

// A day: debris that outlasts it may as well last for ever, which 0, the default, says (§5.5).
constexpr float DEBRIS_LIFETIME_MAXIMUM_SECONDS = 86400.0f;

[[nodiscard]] std::wstring Mistake(std::wstring_view _what)
{
  return std::format(L"{}\n\nUsage: {}", _what, USAGE);
}

[[nodiscard]] bool ParseNumber(std::wstring_view _text, std::uint32_t& _value) noexcept
{
  // Any 32-bit value fits in ten digits: text longer than the buffer is refused unread, and from_chars refuses the rest
  // of what is out of range.
  std::array<char, 16> narrow{};
  if (_text.empty() || _text.size() > narrow.size())
  {
    return false;
  }
  for (std::size_t i = 0; i < _text.size(); ++i)
  {
    if (_text[i] < L'0' || _text[i] > L'9')
    {
      return false;
    }
    narrow[i] = static_cast<char>(_text[i]);
  }
  const char* const end = narrow.data() + _text.size();
  const std::from_chars_result result = std::from_chars(narrow.data(), end, _value);
  return result.ec == std::errc{} && result.ptr == end;
}

// A number of seconds, whole or with a decimal point, such as 30 or 2.5.
[[nodiscard]] bool ParseSeconds(std::wstring_view _text, float& _value) noexcept
{
  std::array<char, 16> narrow{};
  if (_text.empty() || _text.size() > narrow.size() || std::ranges::count(_text, L'.') > 1)
  {
    return false;
  }
  for (std::size_t i = 0; i < _text.size(); ++i)
  {
    if ((_text[i] < L'0' || _text[i] > L'9') && _text[i] != L'.')
    {
      return false;
    }
    narrow[i] = static_cast<char>(_text[i]);
  }
  const char* const end = narrow.data() + _text.size();
  const std::from_chars_result result = std::from_chars(narrow.data(), end, _value, std::chars_format::fixed);
  return result.ec == std::errc{} && result.ptr == end;
}

// The executable's folder, where the build copies GameData (§13).
[[nodiscard]] std::filesystem::path ExecutableFolder()
{
  std::array<wchar_t, 32768> path{};
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  return std::filesystem::path(std::wstring_view(path.data(), length)).parent_path();
}

} // namespace

std::expected<Options, std::wstring> ParseCommandLine(std::span<const std::wstring> _arguments)
{
  // The client and the server read the same GameData until they are separate programs (Design/Archive/SpaceScene.md §6.2).
  Options options{{ExecutableFolder() / L"GameData", std::nullopt, {false, std::nullopt, DEBUG_BUILD, false}}, {}, std::nullopt};
  std::optional<std::wstring_view> worldOption; // the first option that shapes the world beyond its seed
  for (std::size_t i = 0; i < _arguments.size(); ++i)
  {
    const std::wstring_view argument = _arguments[i];
    const bool hasValue = i + 1 < _arguments.size();
    if (argument == L"--seed" && hasValue)
    {
      if (!ParseNumber(_arguments[++i], options.world.seed))
      {
        return std::unexpected(Mistake(std::format(L"--seed takes a whole number from 0 to 4294967295, not {}.", _arguments[i])));
      }
    }
    else if ((argument == L"--stations" || argument == L"--frigates" || argument == L"--capitals") && hasValue)
    {
      const bool stations = argument == L"--stations";
      const std::uint32_t maximum = stations ? STATIONS_MAXIMUM : SHIPS_MAXIMUM;
      std::uint32_t count = 0;
      if (!ParseNumber(_arguments[++i], count) || count > maximum)
      {
        return std::unexpected(Mistake(std::format(L"{} takes a count from 0 to {}, not {}.", argument, maximum, _arguments[i])));
      }
      if (stations)
      {
        options.world.stations = count;
      }
      else if (argument == L"--frigates")
      {
        options.world.frigates = count;
      }
      else
      {
        options.world.capitalShips = count;
      }
      worldOption = worldOption.value_or(argument);
    }
    else if (argument == L"--debris-lifetime" && hasValue)
    {
      float seconds = 0.0f;
      if (!ParseSeconds(_arguments[++i], seconds) || seconds > DEBRIS_LIFETIME_MAXIMUM_SECONDS)
      {
        return std::unexpected(
          Mistake(std::format(L"--debris-lifetime takes seconds from 0, for ever, to {:.0f}, such as 30 or 2.5, not {}.",
                              DEBRIS_LIFETIME_MAXIMUM_SECONDS, _arguments[i])));
      }
      options.world.debrisLifetimeSeconds = seconds;
      worldOption = worldOption.value_or(argument);
    }
    else if (argument == L"--size" && hasValue)
    {
      const std::wstring_view size = _arguments[++i];
      const std::size_t times = size.find(L'x');
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      if (times == std::wstring_view::npos || !ParseNumber(size.substr(0, times), width) || !ParseNumber(size.substr(times + 1), height) ||
          width == 0 || height == 0)
      {
        return std::unexpected(Mistake(std::format(L"--size takes <width>x<height> in pixels, such as 1280x720, not {}.", size)));
      }
      options.game.windowSize = NeuronClient::ClientSize{width, height};
    }
    else if (argument == L"--warp")
    {
      options.game.device.warp = true;
    }
    else if (argument == L"--adapter" && hasValue)
    {
      std::uint32_t adapter = 0;
      if (!ParseNumber(_arguments[++i], adapter))
      {
        return std::unexpected(Mistake(std::format(L"--adapter takes a number, not {}.", _arguments[i])));
      }
      options.game.device.adapter = adapter;
    }
    else if (argument == L"--d3d-debug")
    {
      options.game.device.debugLayer = true;
    }
    else if (argument == L"--gbv")
    {
      options.game.device.gpuBasedValidation = true;
    }
    else if (argument == L"--bench" && hasValue)
    {
      std::uint32_t seconds = 0;
      if (!ParseNumber(_arguments[++i], seconds) || seconds == 0 || seconds > BENCH_SECONDS_MAXIMUM)
      {
        return std::unexpected(Mistake(
          std::format(L"--bench takes the timeline's length in whole seconds, 1 to {}, not {}.", BENCH_SECONDS_MAXIMUM, _arguments[i])));
      }
      options.benchSeconds = seconds;
    }
    else
    {
      return std::unexpected(Mistake(std::format(L"{} is not an option here{}.", argument, hasValue ? L"" : L", or it needs a value")));
    }
  }
  if (options.game.device.warp && options.game.device.adapter)
  {
    return std::unexpected(Mistake(L"--warp and --adapter each choose the adapter; give one of them."));
  }
  // --bench runs the one-station preset until S-M8's space bench (§14): one station and no ships, from the seed.
  if (options.benchSeconds)
  {
    if (worldOption)
    {
      return std::unexpected(Mistake(std::format(L"--bench runs one station and no ships, so {} does not go with it.", *worldOption)));
    }
    options.world.stations = 1;
    options.world.frigates = 0;
    options.world.capitalShips = 0;
  }
  // GPU-based validation runs inside the debug layer.
  options.game.device.debugLayer = options.game.device.debugLayer || options.game.device.gpuBasedValidation;
  return options;
}

} // namespace Outpost
