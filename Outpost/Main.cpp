#include "pch.h"

#include <shellapi.h>

#include "CommandLine.h"
#include "FailureReport.h"

#include "Bench.h"
#include "Game.h"

#include "Sector.h"

#include "ServerHost.h"

#include "LoopbackTransport.h"

#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

// The server and the client in one process (Design/ADR/ADR-003, AGENTS.md R18): the sector on a server host, a loopback
// between the two, and each side given its own end of it and nothing else. Returns the process's exit code.
[[nodiscard]] int Run(const Outpost::Options& _options)
{
  auto sector = GameLogic::Sector::Create(_options.world, _options.game.modelDirectory);
  if (!sector)
  {
    const std::string refusal = std::format("{}: {}", GameLogic::SectorRefusalName(sector.error().refusal), sector.error().detail);
    const std::wstring message = L"The world was refused. " + std::wstring(winrt::to_hstring(refusal));
    MessageBoxW(nullptr, message.c_str(), L"Outpost", MB_OK | MB_ICONWARNING);
    return 2;
  }
  NeuronServer::ServerHost host(**sector);
  NeuronCore::LoopbackPair link = NeuronCore::MakeLoopbackPair();
  host.AddSession(std::move(link.server));

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
