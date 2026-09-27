# ADR-004 — PIX event runtime

**Status:** accepted, 2026-09-27 · **Decided by:** the owner, who added the package on `main` in 20fcb6b · **Lands with:** M2 of [`Design/SampleRenderer.md`](../SampleRenderer.md) (§15), which carries it into `NeuronClient`

## Context

PIX on Windows captures a Direct3D 12 frame and replays it call by call. WinPixEventRuntime lets the program name what those calls are for: it marks regions and moments on a command list or on the CPU timeline, and a capture shows the frame by those names instead of as one flat list. The owner added the package to `VoxelRender` through Visual Studio's package manager while M2 was being written; `VoxelRender` is `NeuronClient` now (ADR-003), and the package moves with it.

`AGENTS.md` §6 asks for a record of every third-party dependency, and the commit that added this one has none, so it is written here, with the change that carries the package into its new home. That commit also turned `main` red: `Build/CheckProjectFiles.py` refused the condition on the package's import, and CI had no step that restores packages, so the build would have failed next.

## Decision

**The package.** `WinPixEventRuntime` 1.0.240308001, Microsoft's, under the MIT licence, from nuget.org, listed in `NeuronClient/packages.config`: the client engine is the only library that talks to Direct3D. Visual Studio wrote the reference in its own form, an import of the package's `build/WinPixEventRuntime.targets` guarded by `Exists()` and an `EnsureNuGetPackageBuildImports` target that fails the build when the package is missing. It stays in that form, so that the package manager can update it.

**Restored, never committed.** NuGet restores the package into `packages/` at the repository root, which `.gitignore` excludes. Visual Studio restores when it builds. A command-line build restores first, with `msbuild Outpost.Voxel.slnx /t:Restore /p:RestorePackagesConfig=true` (`AGENTS.md` §3), and CI runs the same command in a step of its own before the build (`AGENTS.md` §6). `RestorePackagesConfig` is needed because MSBuild restores a `packages.config` project only when asked.

**The checker accepts the guard.** `Build/CheckProjectFiles.py` accepts only conditions that name one configuration, so that it can compare Debug with Release (`AGENTS.md` §3). It now also accepts `Exists('X')` on an import of `X`, which comes out the same in both configurations. Any other `Exists()`, on an import of another file or on anything that is not an import, is still a finding.

**What the package does to a build**, read from its targets file and headers. On x64 it adds its include folder to every compile, adds its library folder and `WinPixEventRuntime.lib` to the link, and marks `WinPixEventRuntime.dll` to be copied to the output folder. `pix3.h` switches events on when `_DEBUG` is defined, and otherwise compiles every call to nothing, so a Release build has no events and no use for the DLL unless `USE_PIX` is defined. Events on a command list exist only if `d3d12.h` was included before `pix3.h`.

**No code calls it yet.** M2 includes no PIX header. The change that first does settles three things and records them:

1. Whether events run in Release too. Defining `USE_PIX` in both configurations would do it, and `AGENTS.md` §3 allows that, because the setting reads the same in both.
2. How the programs get the library and the DLL. `NeuronClient` is a static library, which does not link, so the link settings the package adds reach nothing. `Outpost` and `NeuronClientTests` link `NeuronClient`'s calls into PIX, so they need `WinPixEventRuntime.lib` themselves and the DLL beside them at run time. NuGet's own answer is the package in each program's `packages.config` as well.
3. How `pix3.h` is included. With `USE_PIX` it includes `<shlobj.h>`, `<strsafe.h>`, `<knownfolders.h>` and `<shellapi.h>`. Without it, it disables warnings C4548 and C4555 for the rest of the translation unit, which `AGENTS.md` §4 does not allow our own code to do.

## What this forecloses

- Committing restored packages, or copying the runtime's headers and binaries into the tree.
- A PIX dependency anywhere but the client side: `NeuronCore`, `NeuronServer` and `GameLogic` do not take it.
- Changing the version by hand. The package manager writes `packages.config`, the import and the error target together, and the three must agree.
