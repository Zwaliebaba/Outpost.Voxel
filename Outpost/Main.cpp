#include "pch.h"

#include <shellapi.h>

#include "CommandLine.h"
#include "FailureReport.h"
#include "Game.h"

#include <string>
#include <vector>

// The client, and until the server moves into Server.exe the server process too (Design/ADR/ADR-003). Everything it
// does is GameLib's; this reads the command line and says why the game stopped, if it stopped for a reason.
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

  const std::expected<GameLib::GameOptions, std::wstring> options = Outpost::ParseCommandLine(arguments);
  if (!options)
  {
    MessageBoxW(nullptr, options.error().c_str(), L"Outpost", MB_OK | MB_ICONWARNING);
    return 2;
  }
  try
  {
    GameLib::RunGame(*options);
  }
  catch (...)
  {
    const std::wstring message = winrt::to_hstring(NeuronClient::DescribeCurrentException()).c_str();
    MessageBoxW(nullptr, message.c_str(), L"Outpost stopped", MB_OK | MB_ICONERROR);
    return 1;
  }
  return 0;
}
