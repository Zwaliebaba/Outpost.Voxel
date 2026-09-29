#include "pch.h"

#include <shellapi.h>

#include "CommandLine.h"
#include "FailureReport.h"

#include "Bench.h"
#include "Game.h"

#include "Sector.h"
#include "Skirmish.h"

#include "CommandLog.h"
#include "ServerHost.h"
#include "World.h"

#include "LoopbackTransport.h"
#include "Message.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

// The world the command line asks for: the MVP's skirmish with --skirmish (Design/ADR/ADR-030), and otherwise the space
// scene's sector. Refused, the message says why.
[[nodiscard]] std::expected<std::unique_ptr<NeuronServer::World>, std::string> CreateWorld(const Outpost::Options& _options)
{
  if (_options.skirmish)
  {
    auto skirmish = GameLogic::Skirmish::Create(*_options.skirmish, _options.game.modelDirectory);
    if (!skirmish)
    {
      return std::unexpected(std::format("{}: {}", GameLogic::SkirmishRefusalName(skirmish.error().refusal), skirmish.error().detail));
    }
    return std::move(*skirmish);
  }
  auto sector = GameLogic::Sector::Create(_options.world, _options.game.modelDirectory);
  if (!sector)
  {
    return std::unexpected(std::format("{}: {}", GameLogic::SectorRefusalName(sector.error().refusal), sector.error().detail));
  }
  return std::move(*sector);
}

// Says _message in a box titled _title, and in the debugger's output.
void Report(const std::string& _message, const wchar_t* _title, UINT _icon)
{
  const std::wstring shown(winrt::to_hstring(_message));
  OutputDebugStringW((shown + L"\n").c_str());
  MessageBoxW(nullptr, shown.c_str(), _title, MB_OK | _icon);
}

// The bytes of _file, or nothing when it cannot be read.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadBytes(const std::filesystem::path& _file)
{
  std::ifstream stream(_file, std::ios::binary | std::ios::ate);
  const std::streamoff size = stream.is_open() ? static_cast<std::streamoff>(stream.tellg()) : -1;
  if (size < 0)
  {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
  {
    return std::nullopt;
  }
  return bytes;
}

// Replays the skirmish _file logged, without a window (Design/ADR/ADR-032): the skirmish made again from the log's
// parameters and GameData's models, and every command sent again at its tick. Says whether every tick matched, and
// returns 0 when it did and 1 when it did not, or the log could not be replayed.
[[nodiscard]] int Replay(const std::filesystem::path& _file, const std::filesystem::path& _gameData)
{
  const std::string name = winrt::to_string(_file.wstring());
  const std::optional<std::vector<std::uint8_t>> bytes = ReadBytes(_file);
  if (!bytes)
  {
    Report(std::format("--replay could not read {}.", name), L"Outpost --replay", MB_ICONWARNING);
    return 1;
  }
  const auto log = NeuronServer::DecodeCommandLog(*bytes);
  if (!log)
  {
    Report(std::format("{} is not a log this build reads: {}.", name, NeuronServer::LogErrorName(log.error())), L"Outpost --replay",
           MB_ICONWARNING);
    return 1;
  }
  const std::optional<GameLogic::SkirmishParameters> parameters = GameLogic::DecodeSkirmishParameters(log->world);
  if (!parameters)
  {
    Report(std::format("{} does not describe a skirmish this build makes.", name), L"Outpost --replay", MB_ICONWARNING);
    return 1;
  }
  auto skirmish = GameLogic::Skirmish::Create(*parameters, _gameData);
  if (!skirmish)
  {
    Report(
      std::format("The skirmish was refused. {}: {}", GameLogic::SkirmishRefusalName(skirmish.error().refusal), skirmish.error().detail),
      L"Outpost --replay", MB_ICONWARNING);
    return 1;
  }
  NeuronServer::ReplayOutcome outcome{};
  try
  {
    outcome = NeuronServer::Replay(**skirmish, *log);
  }
  catch (const std::invalid_argument& error)
  {
    Report(std::format("{} cannot be replayed here: {}", name, error.what()), L"Outpost --replay", MB_ICONWARNING);
    return 1;
  }
  const std::string summary = outcome.firstDifference ? std::format("The replay of {} parted from its log at tick {} of {}.", name,
                                                                    *outcome.firstDifference, outcome.ticks)
                                                      : std::format("The replay of {} matched its log: {} ticks and {} commands, seed {}.",
                                                                    name, outcome.ticks, outcome.commands, parameters->seed);
  Report(summary, L"Outpost --replay", outcome.firstDifference ? MB_ICONWARNING : MB_ICONINFORMATION);
  return outcome.firstDifference ? 1 : 0;
}

// The server and the client in one process (Design/ADR/ADR-003, AGENTS.md R18): the world on a server host, a loopback
// between the two, and each side given its own end of it and nothing else. Returns the process's exit code.
[[nodiscard]] int Run(const Outpost::Options& _options)
{
  if (_options.replayFile)
  {
    return Replay(*_options.replayFile, _options.game.modelDirectory);
  }
  auto world = CreateWorld(_options);
  if (!world)
  {
    const std::wstring message = L"The world was refused. " + std::wstring(winrt::to_hstring(world.error()));
    MessageBoxW(nullptr, message.c_str(), L"Outpost", MB_OK | MB_ICONWARNING);
    return 2;
  }
  // The log outlives the host, whose thread writes it until the host stops.
  std::ofstream log;
  NeuronServer::ServerHost host(**world);
  NeuronCore::LoopbackPair link = NeuronCore::MakeLoopbackPair();
  // The window plays side 1 of the skirmish, or observes it with --observe; it observes the space scene and the bench,
  // whose worlds have no sides (Design/ADR/ADR-032).
  host.AddSession(std::move(link.server), _options.skirmish && !_options.observe ? std::uint8_t{1} : NeuronCore::OBSERVER_SIDE);
  if (_options.logFile && _options.skirmish)
  {
    log.open(*_options.logFile, std::ios::binary | std::ios::trunc);
    if (!log)
    {
      throw std::runtime_error(std::format("--log could not open {}.", winrt::to_string(_options.logFile->wstring())));
    }
    host.Log(log, GameLogic::EncodeSkirmishParameters(*_options.skirmish));
  }

  if (_options.benchSeconds)
  {
    // The bench owns time (Design/Archive/SpaceScene.md §14): the server takes a step only when the next frame needs one, and no
    // thread runs.
    GameLib::Bench bench(_options.game, *_options.benchSeconds, std::format("the one-station preset, seed {}", _options.world.seed),
                         std::move(link.client));
    while (const std::optional<std::uint64_t> tick = bench.TickNeeded())
    {
      while (host.Tick() < *tick)
      {
        host.Step();
      }
      bench.Frame();
    }
    static_cast<void>(bench.Finish());
    return 0;
  }

  // The server's thread runs until the client's window closes, and stops with the host if the client throws (§6.3).
  host.Start();
  GameLib::RunGame(_options.game, std::move(link.client));
  host.Stop();
  return 0;
}

} // namespace

// The client, and until the server moves into Server.exe the server process too (Design/ADR/ADR-003). This reads the
// command line, starts the server and the client, and says why the game stopped, if it stopped for a reason.
int WINAPI wWinMain(_In_ HINSTANCE /*_instance*/, _In_opt_ HINSTANCE /*_previous*/, _In_ PWSTR /*_commandLine*/, _In_ int /*_show*/)
{
  NeuronClient::InstallFailureReport();

  int count = 0;
  wchar_t** words = CommandLineToArgvW(GetCommandLineW(), &count);
  std::vector<std::wstring> arguments;
  for (int i = 1; i < count && words != nullptr; ++i)
  {
    arguments.emplace_back(words[i]);
  }
  LocalFree(static_cast<HLOCAL>(words));

  const std::expected<Outpost::Options, std::wstring> options = Outpost::ParseCommandLine(arguments);
  if (!options)
  {
    MessageBoxW(nullptr, options.error().c_str(), L"Outpost", MB_OK | MB_ICONWARNING);
    return 2;
  }
  try
  {
    return Run(*options);
  }
  catch (...)
  {
    const std::wstring message = winrt::to_hstring(NeuronClient::DescribeCurrentException()).c_str();
    MessageBoxW(nullptr, message.c_str(), L"Outpost stopped", MB_OK | MB_ICONERROR);
    return 1;
  }
}
