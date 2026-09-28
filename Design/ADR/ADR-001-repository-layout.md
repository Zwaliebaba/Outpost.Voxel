# ADR-001 — Repository layout and build settings

**Status:** accepted, 2026-09-27; the project layout is superseded by [ADR-003](ADR-003-engine-and-game-layout.md), [ADR-012](ADR-012-arm64-platform.md) adds ARM64 beside x64, and the build settings, output paths, tooling and test framework below still hold · **Lands with:** M0 of [`Design/SampleRenderer.md`](../SampleRenderer.md) (§15)

*This record is kept as it was decided. The five projects it names were renamed and joined by three more in ADR-003: `VoxelCore` is now `NeuronCore`, `VoxelRender` `NeuronClient`, `VoxelSample` `Outpost`, and the suites `NeuronCoreTests` and `NeuronClientTests`.*

## Context

`AGENTS.md` §2 leaves the concrete layout — the solution, the projects and the edges between them — to the first project, and asks for it to be recorded in `AGENTS.md` and in an ADR when that project lands. The design proposes five projects (§6.1). M0 creates all five as empty shells, together with the three checkers that make the rules of `AGENTS.md` executable, so that every later milestone lands on gates that already run.

## Decision

**One solution, five projects.** `Outpost.Voxel.slnx` sits at the root; MSBuild builds the XML solution format directly. Each project lives at `<Name>/<Name>.vcxproj`, and its namespace is its name.

| Project | Kind | References |
|---|---|---|
| `VoxelCore` | static library | — |
| `VoxelRender` | static library | `VoxelCore` |
| `VoxelSample` | Win32 application | `VoxelRender`, `VoxelCore` |
| `VoxelCoreTests` | test DLL | `VoxelCore` |
| `VoxelRenderTests` | test DLL | `VoxelRender`, `VoxelCore` |

Edges run from the application down. Nothing references the application or a test suite, and a project lists another project's folder on its include path only if it references that project. `Build/CheckProjectFiles.py` enforces all of this, and fails on a cycle.

**`VoxelCore` has no Windows dependency.** Its C++ twins of the GPU algorithms (R15) must share nothing with the GPU code they check except the algorithm, and a library that cannot see `<windows.h>` cannot lean on the SDK by accident.

**One Windows header.** `VoxelRender/WindowsSdk.h` defines `WIN32_LEAN_AND_MEAN` and `NOMINMAX` and then includes `<windows.h>` (`AGENTS.md` §4). A project that needs Windows reaches that header through its include path, and no project file defines a Windows macro. The name is deliberately not `Windows.h`: a project header of that name would shadow the SDK's on every include path that lists its folder.

**Build settings, stated in every project and both configurations:** toolset v145, x64 only, `/std:c++latest`, `/permissive-`, `/W4 /WX`, `/sdl`, `/fp:precise`, `/arch:AVX2`, Unicode, the precompiled header `pch.h`, `/Zi`, and `/utf-8`. The last is there because the sources are UTF-8 (`.editorconfig`) and carry non-ASCII characters in comments, and a compiler left to guess the code page can turn such a comment into a warning, which `/WX` makes fatal. Debug and Release differ only in `Optimization`, `_DEBUG` against `NDEBUG`, `FunctionLevelLinking`, `IntrinsicFunctions`, `UseDebugLibraries`, `RuntimeLibrary`, `LinkIncremental`, `WholeProgramOptimization`, `EnableCOMDATFolding` and `OptimizeReferences` — each stated explicitly on both sides, so that the checker compares written values rather than defaults.

**Output.** `OutDir` is `$(SolutionDir)$(Platform)\$(Configuration)\`, which is where CI's test step looks for the suites. `IntDir` is `$(SolutionDir)$(Platform)\$(Configuration)\obj\$(ProjectName)\`, which keeps build output out of the project folders, so a flat project folder holds nothing but its sources.

**No per-user property sheets.** Visual Studio's templates import `$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props`, a file outside the repository that can change the build on one machine without anyone seeing it. These projects do not import it.

**Tests** use Microsoft's native unit-test framework, which ships with the C++ workload: there is nothing to install, and `vstest.console.exe` runs it in CI. Each suite keeps its `SuiteSmoke` placeholder until its first real test lands (`AGENTS.md` §3). The framework's headers are Microsoft's code, so `Build/RunClangTidy.py` passes their directory to clang as a system directory, and the declarations its macros generate are not linted as ours.

**The Windows entry point keeps its name.** `wWinMain` is dictated by the SDK. `.clang-tidy` exempts exactly that name from the function-naming rule (R4), rather than the application adopting a non-standard entry point to dodge the check.

**The checkers land with the projects**, and the CI guards that let the workflow run on an empty tree come off in the same change: from now on, a missing solution, checker or test suite fails the build.

## What this forecloses

- A second build system or platform: the build is MSBuild over one `.slnx`, x64 only. CMake, Win32 and ARM64 are each a new ADR.
- Windows or Direct3D code in `VoxelCore`: code that needs either belongs in `VoxelRender`.
- A header outside `WindowsSdk.h` that defines a Windows macro, or a project file that defines one.
- A new project without its row in `AGENTS.md` §2, its `HeaderFilterRegex` entry (test suites match by convention), and an ADR of its own.
- Build output inside a project folder.
