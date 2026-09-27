#include "pch.h"

#include "FailureReport.h"

#include <cstdint>
#include <exception>
#include <format>
#include <string_view>

namespace NeuronClient
{
namespace
{

struct FailureLocation
{
  std::uint32_t line;
  const char* file;
  const char* function; // null outside a Debug build
  std::int32_t result;
};

// The last HRESULT C++/WinRT threw on this thread, and where it was checked.
thread_local FailureLocation g_lastFailure{};

void __stdcall RecordFailure(std::uint32_t _lineNumber, const char* _fileName, const char* _functionName, void* /*_returnAddress*/,
                             winrt::hresult _result) noexcept
{
  g_lastFailure = {_lineNumber, _fileName, _functionName, _result};
}

} // namespace

void InstallFailureReport() noexcept
{
  winrt_throw_hresult_handler = &RecordFailure;
}

std::string DescribeCurrentException()
{
  try
  {
    throw;
  }
  catch (const winrt::hresult_error& error)
  {
    const std::int32_t code = error.code();
    std::string message = std::format("HRESULT 0x{:08X}: {}", static_cast<std::uint32_t>(code), winrt::to_string(error.message()));
    if (g_lastFailure.file != nullptr && g_lastFailure.result == code)
    {
      std::string_view file = g_lastFailure.file;
      const std::size_t slash = file.find_last_of("/\\");
      if (slash != std::string_view::npos)
      {
        file.remove_prefix(slash + 1);
      }
      message += std::format("\nchecked at {}:{}", file, g_lastFailure.line);
      if (g_lastFailure.function != nullptr)
      {
        message += std::format(", in {}", g_lastFailure.function);
      }
    }
    return message;
  }
  catch (const std::exception& error)
  {
    return error.what();
  }
  catch (...)
  {
    return "an exception that is neither a winrt::hresult_error nor a std::exception";
  }
}

} // namespace NeuronClient
