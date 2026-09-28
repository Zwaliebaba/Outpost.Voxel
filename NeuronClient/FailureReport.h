#pragma once

#include <string>

namespace NeuronClient
{

// Where a failed HRESULT was checked (Design/Archive/SampleRenderer.md §13). winrt::check_hresult captures its caller's file and
// line, and in a Debug build its function, and C++/WinRT hands them to winrt_throw_hresult_handler before it throws.
// This installs a handler that keeps them for DescribeCurrentException. Call it once, before anything is checked.
void InstallFailureReport() noexcept;

// The exception in flight as one message: for a winrt::hresult_error its HRESULT, the system's text for it and where it
// was checked; for a standard exception its what(). Only inside a catch block.
[[nodiscard]] std::string DescribeCurrentException();

} // namespace NeuronClient
