# ADR-023 — Test suites in `Tests/`, tools in `Tools/`

**Status:** accepted, 2026-09-29 · **Lands with:** the checkers' update for the owner's restructure of the tree, commit c916c1e · **Amends:** [ADR-003](ADR-003-engine-and-game-layout.md), where every project lives at `<Name>/<Name>.vcxproj`, for [ADR-016](ADR-016-server-suites.md)'s suites as for the rest; and [ADR-020](ADR-020-nvf-import.md)'s `NvfImport/` and its ban on C++ in `Tools/` · **Amended by:** [ADR-027](ADR-027-design-generator.md), which adds a second tool with no C++, the design generator

## Context

On 2026-09-29 the owner moved the four test suites into `Tests/` and `NvfImport` into `Tools/`, beside the Blender extension and the golden file, and grouped them the same way in the solution. The libraries and `Outpost` stayed at the root. The move updated the solution and each moved project's references, which now read `..\..\<Library>\<Library>.vcxproj`. It also removed the two sky textures that ADR-021 had left unread.

The build did not need to change, because none of it depends on how deep a project sits:
- every output and include path is anchored on `$(SolutionDir)` (AGENTS.md §3);
- CI finds the suites by a recursive search for `*Tests.vcxproj`, and their DLLs and `NvfImport.exe` in `x64\Debug\`;
- the suites find `GameData` and the repository's files by walking up from their working directory and their sources, and so do the Blender extension's tests;
- `.clang-tidy`'s `HeaderFilterRegex` is not anchored, so it matches a header by the name of the folder it sits in, at any depth.

The checkers had not caught up, and CI runs them before the build. CI run 36525884039 on c916c1e stopped at the first of them.
- **`Build/CheckProjectFiles.py`** still required `<Name>/<Name>.vcxproj`, and took a file's project from the first folder of its path. It reported 74 findings over the 11 projects:
  - 5 solution entries in the wrong place;
  - 62 of the suites' C++ files outside every project folder;
  - 5 of `NvfImport`'s C++ files in `Tools/`;
  - the 2 HLSL files of `NeuronClientTests`' layout echo outside a project's `Shader` folder.
- **`Build/RunClangTidy.py`** looked for a source's project at `<folder>/<folder>.vcxproj`, spelling `<folder>` as the whole path, so it looked for `Tests/GameLogicTests/Tests/GameLogicTests.vcxproj`. It builds every command before it runs one, so it stopped before linting anything.

AGENTS.md §2 makes the layout a decision (ADR-001, ADR-003), and a move of five projects changes it. This ADR records the owner's layout, and what the checkers now hold it to.

## Decision

**A project has one of three homes.**

| Home | Holds |
|---|---|
| `<Name>/<Name>.vcxproj` | The libraries and the game's executable: `NeuronCore`, `NeuronClient`, `NeuronServer`, `GameLogic`, `GameLib` and `Outpost`. |
| `Tests/<Name>/<Name>.vcxproj` | The test suites, each named `<Library>Tests`, and nothing else. |
| `Tools/<Name>/<Name>.vcxproj` | The tools: applications that nothing links. Today `NvfImport`. |

**The groups are one level deep, and there are two.** `Tests/` and `Tools/` are the only folders a project may sit one level below, and nothing sits deeper. A third group is a change to this ADR.

**`Tools/` holds the tools, C++ and not.** A C++ tool lives in its own project folder there, as `NvfImport` does. The Blender extension's Python stays in `Tools/Blender/`, and the golden file in `Tools/Golden/` (ADR-020). No project builds anything in either of those, and C++ or HLSL anywhere in `Tools/` outside a tool's folder fails the check, as it does anywhere outside a project's folder. The rest of ADR-020 stands.

**The rules inside a project do not change.** C++ lives directly in its project's folder and HLSL in its `Shader` folder, wherever the project folder is, and `.clang-tidy`'s filter still sees every header that does.

**`Build/CheckProjectFiles.py` holds the tree to this.**
- **Placement.** A project listed anywhere but the three homes is a finding. So is a suite outside `Tests/`, anything in `Tests/` that is not a suite, and anything in `Tools/` that is not an application.
- **Files.** A file's project is the project folder it lies in, at any depth, rather than the first folder of its path.
- **Include directories.** These are matched against project folders rather than project names, so `$(SolutionDir)Tests\NeuronCoreTests` is recognized as that suite's folder.
- **References.** A reference's path must now lead to the project it names. Until now the checker read only the file name, so a reference left at `..\NeuronCore\NeuronCore.vcxproj` in a project moved one level down, which leads nowhere, passed it.
- **The header filter.** It is probed with a header in each project's own folder.

**`Build/RunClangTidy.py` finds a source's project by its folder's name,** `<folder>/<name of folder>.vcxproj`.

**Nothing else changes.** `.clang-tidy`, the CI workflow, the project files' settings and the namespaces are as they were. A project's namespace is still its name, not its path.

## Measured

- **CheckProjectFiles.** On c916c1e it reports the 74 findings above, locally with the old checker as in CI. With this change it is clean: 11 projects and 377 files, this ADR among them.
- **RunClangTidy's dry run.** On c916c1e it stops at the first suite's source. With this change it prints 128 commands for the 128 translation units `git ls-files` lists.
- **Planted violations.** In a scratch copy of the tree, each of these is reported with the finding for it:
  - a project in an unknown group, `Stuff/`;
  - HLSL loose in a project's folder;
  - a header one folder below its project;
  - a reference whose path leads to the wrong folder;
  - C++ in `Tools/Blender/`;
  - a static library in `Tools/`.

  The two remaining placement rules, a suite at the root and a project in `Tests/` that is not a suite, were checked on the placement function directly.
- **Not measured here:** a build. This was done on Linux. CI builds Debug|x64 and runs the suites, clang-tidy and `NvfImport --check`.

## What this forecloses

- **A project anywhere but the three homes,** and a group nested inside a group.
- **A test suite outside `Tests/`,** and anything in `Tests/` that is not one.
- **A library in `Tools/`.** A tool that grows code another project needs moves that code into a library at the root.
- **C++ or HLSL in `Tools/` outside a tool's project folder.**
- **A reference whose path does not lead to the project it names.**
