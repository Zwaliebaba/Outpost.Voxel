#include "pch.h"

#include "CommandLine.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
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
  L"Outpost.exe [--vox <path>] [--size <width>x<height>] [--warp | --adapter <n>] [--d3d-debug] [--gbv] [--bench <seconds>]";

// --bench's timeline runs at 60 frames a second, so an hour is 216,000 frames in each of the two variants.
constexpr std::uint32_t BENCH_SECONDS_MAXIMUM = 3600;

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

// The executable's folder, where the build copies GameData (§13).
[[nodiscard]] std::filesystem::path ExecutableFolder()
{
  std::array<wchar_t, 32768> path{};
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  return std::filesystem::path(std::wstring_view(path.data(), length)).parent_path();
}

} // namespace

std::expected<GameLib::GameOptions, std::wstring> ParseCommandLine(std::span<const std::wstring> _arguments)
{
  GameLib::GameOptions options{
    ExecutableFolder() / L"GameData" / L"MilitaryStation.vox", std::nullopt, {false, std::nullopt, DEBUG_BUILD, false}, std::nullopt};
  for (std::size_t i = 0; i < _arguments.size(); ++i)
  {
    const std::wstring_view argument = _arguments[i];
    const bool hasValue = i + 1 < _arguments.size();
    if (argument == L"--vox" && hasValue)
    {
      options.voxPath = _arguments[++i];
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
      options.windowSize = NeuronClient::ClientSize{width, height};
    }
    else if (argument == L"--warp")
    {
      options.device.warp = true;
    }
    else if (argument == L"--adapter" && hasValue)
    {
      std::uint32_t adapter = 0;
      if (!ParseNumber(_arguments[++i], adapter))
      {
        return std::unexpected(Mistake(std::format(L"--adapter takes a number, not {}.", _arguments[i])));
      }
      options.device.adapter = adapter;
    }
    else if (argument == L"--d3d-debug")
    {
      options.device.debugLayer = true;
    }
    else if (argument == L"--gbv")
    {
      options.device.gpuBasedValidation = true;
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
  if (options.device.warp && options.device.adapter)
  {
    return std::unexpected(Mistake(L"--warp and --adapter each choose the adapter; give one of them."));
  }
  // GPU-based validation runs inside the debug layer.
  options.device.debugLayer = options.device.debugLayer || options.device.gpuBasedValidation;
  return options;
}

} // namespace Outpost
