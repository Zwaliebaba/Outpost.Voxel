#pragma once

// The one owner of the Windows macro family (AGENTS.md §4). No other header and no project file
// defines any of these; a file that needs <windows.h> includes this header instead.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// C++/WinRT, for winrt::com_ptr and winrt::check_hresult (AGENTS.md R12). <unknwn.h> must come first: it is what
// makes winrt::com_ptr work with classic COM interfaces such as ID3D12Device, and WIN32_LEAN_AND_MEAN keeps
// <windows.h> from including it.
#include <unknwn.h>
#include <winrt/base.h>
