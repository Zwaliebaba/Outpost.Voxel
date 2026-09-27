#pragma once

// The one owner of the Windows macro family (AGENTS.md §4). No other header and no project file
// defines any of these; a file that needs <windows.h> includes this header instead.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
