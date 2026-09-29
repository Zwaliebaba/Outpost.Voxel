# AGENTS.md — Engineering Rules for *Outpost.Voxel*

Operating instructions for every agent (and human) writing code in this repository. **Read this before generating a single line.**

*Outpost.Voxel* is a C++23 project built on Windows with MSVC. This file is about **how code is written here** — naming, layout, build settings and the standing rules of the codebase. It is not the design: what the project *is* belongs in a design document.

**The tree started empty.** Every file in it was written to these rules from its first line. Nothing below is a target to migrate towards; there is no legacy here and nothing is grandfathered, so a whole-tree run of any checker comes back clean, and a finding is a defect rather than archaeology.

**What is authoritative, in order:**

1. **This file** — conformance: naming, style, build settings, and how to work here.
2. **`Design/ADR/`** — engineering decisions taken while building, one file per decision (§6). Numbering starts at `ADR-001`.
3. **The surrounding code** — for anything neither of the above covers, match the file you are editing.

The design documents sit alongside rather than above: each says what is built and this file says how. A design lives in `Design/` while its plan runs, as [`Design/GameConcept.md`](Design/GameConcept.md) does. It moves to `Design/Archive/` when the plan is done, as [`Design/Archive/SampleRenderer.md`](Design/Archive/SampleRenderer.md) has, or when a running design takes over what its plan left open, as the game concept did for [`Design/Archive/SpaceScene.md`](Design/Archive/SpaceScene.md) and [`Design/Archive/NeuronVoxelFormat.md`](Design/Archive/NeuronVoxelFormat.md). There it stays the record of what it built, and the accepted design of what was taken over, and the code keeps citing it. A task that needs a design answer the documents do not give asks the owner, and gets the answer written down in a design before the code is.

If a rule here conflicts with a habit from another codebase, this file wins. If you think a rule is wrong or your task cannot be done without deviating, **say so in your report — never deviate silently.**

---

## 1. Naming convention (normative — no exceptions)

| Kind | Convention | Example |
|---|---|---|
| Type (class, struct, enum, concept, alias) | `PascalCase` | `FileReader` |
| Function, method | `PascalCase` | `ReadBlock()` |
| Member variable | `m_camelCase` | `m_isOpen` |
| Static member (mutable) | `sm_camelCase` | `sm_openCount` |
| Global | `g_camelCase` | `g_instance`, `g_frameCount` |
| Parameter | `_camelCase` | `_fileName`, `_blockIndex` |
| Local | `camelCase` | `bytesRead` |
| Compile-time constant | `UPPER_CASE` | `MAX_PATH_CHARS`, `BLOCK_BYTES` |
| Enumerator | `PascalCase` | `NotFound`, `AccessDenied` |
| Macro | `UPPER_CASE` | `OUTPOST_ASSERT` |
| Namespace | `PascalCase` | `Outpost` |
| File | `PascalCase.cpp` / `.h` | `FileReader.cpp` |

**Note the split that catches people out: a `constexpr` is `UPPER_CASE`, an enumerator is `PascalCase`.** They are both compile-time and they are spelled differently on purpose — an enumerator is a *value of a type* and reads as one at the use site (`ReadFault::AccessDenied`), while a constant is a number with a name and is meant to look like one. [`.clang-tidy`](.clang-tidy) enforces both, and it is the single source of truth for the option values; this document states the rules in prose and does not repeat the settings, so there is nothing to drift.

### The rules behind the table

**R1 — The leading underscore on parameters is deliberate.** It is legal C++: the reserved forms are `_Uppercase`, anything containing `__`, and `_lowercase` **at global scope**. A parameter is never at global scope, so `_fileName` is safe. Never introduce a reserved form — no `_Impl`, no `__helper`, no file-scope `_cache` (use `g_cache` in an anonymous namespace).

**R2 — A type name carries no prefix or affix, and that includes abstract ones.** An interface is `Transport`, not `ITransport`. A base class is not `BaseTransport` or `AbstractTransport`. PascalCase means the name and nothing else. This bans `CFoo`, `SFoo`, `EFoo`, `IFoo`, `FooBase`, `FooAbstract`, `FooImpl` and `_t` suffixes. Name the concept and let the concrete types say what they are:

```
Transport             ← the concept
├── UdpTransport      ← a socket-backed one
└── LoopbackTransport ← in-process, for tests
```

That tree is an illustration of the rule, not a description of anything. A base class for one derived class is ceremony: name the concept, and add the layer when a second thing needs it.

clang-tidy can require an *absent* prefix but cannot see a *present* suffix, so the repository checker carries the other half (§6).

**R3 — Compile-time constants are `UPPER_CASE`.** `constexpr`, `inline constexpr` and `static constexpr` members: `MAX_PATH_CHARS`, `BLOCK_BYTES`, `DEFAULT_TIMEOUT_MILLISECONDS`. `sm_` is reserved for *mutable* statics, which are rare and must document their thread-safety.

**R4 — Acronyms capitalize as words**: `HttpClient`, `XmlReader`, `UdpTransport` — never `HTTPClient`. Identifiers from an external SDK keep that SDK's spelling (`HRESULT`, `HANDLE`, `CreateFileW`, `WSADATA`) and are never renamed to fit.

**R5 — Template parameters are PascalCase**: `T`, `Fn`, `BlockBytes`, `Ts...`.

**R6 — Units belong in names; types do not.** `timeoutMilliseconds`, `sizeBytes`, `angleRadians`, `widthPixels` are encouraged — unit ambiguity is a real defect class, and it is one the compiler cannot catch for you. Never encode the type: no `iCount`, `pBuffer`, `strName`.

**R7 — A file is named for its primary type**, PascalCase, `.h` / `.cpp` only. `.hpp`, `.cc` and `.inl` are not used; template implementations live in the header. Exceptions, because MSBuild and the Visual Studio wizards spell them this way: `pch.h`, `pch.cpp`, `framework.h`, `targetver.h`, `Resource.h`.

**R8 — `m_` marks encapsulated state, not every field.** A `class` with invariants prefixes private members `m_`. A public aggregate — a `Desc` config struct, a wire record, a plain data struct — uses plain `camelCase` fields so brace initialization reads naturally.

**R9 — One namespace per layer, and a lower layer does not know a higher one.** Reusable library code gets its own namespace; application code gets another. The split is a rule rather than a filing preference: if a library type has to know an application concept by name in order to do its job, it is in the wrong layer. Test suites use `namespace <Project>Tests`.

**R10 — No `using namespace` at file scope in a header.** It leaks into every translation unit that includes it, and the failure it causes appears somewhere else. In a `.cpp` it is allowed for the unit-test framework and nothing else; otherwise qualify the name or write a local alias.

**R11 — One spelling per family, and it is the SDK's.** `color`, `initialize`, `serialize`, `normalize`, `quantize`, `synchronize`, `behavior`, `neighbor`, `center`, `gray`, `canceled`. Neither spelling is wrong English; the defect is a tree where a reader has to know which half they are in and a grep for one finds half the uses. The Windows SDK spells `COLORREF` and `InitializeCriticalSection`, and that settles which half wins. Prose is not checked — a design document may spell `colour`; an identifier spells `color`.

### Worked example — this is the target style

```cpp
// <Project>/FileReader.h
#pragma once

#include <cstdint>

namespace Outpost
{

// R3: constant → UPPER_CASE. R6: the unit is in the name.
inline constexpr std::uint32_t BLOCK_BYTES = 4096;
inline constexpr std::uint32_t MAX_PATH_CHARS = 260;

// Enumerator → PascalCase, unlike the constants above.
enum class ReadFault : std::uint8_t
{
  NotFound,
  AccessDenied,
  UnexpectedEnd
};

/// Reads a file in fixed-size blocks.
/// R2: no prefix on the type. R8: private state carries m_.
class FileReader
{
public:
  struct Desc                                            // R8: aggregate → plain fields
  {
    std::uint32_t blockBytes;                            // R6: unit in the name
    std::uint32_t timeoutMilliseconds;
    bool shareRead;
  };

  [[nodiscard]] static bool Open(const wchar_t* _fileName,       // R1: _ on parameters
                                 const Desc& _desc,
                                 FileReader& _outReader) noexcept;

  [[nodiscard]] std::uint64_t SizeBytes() const noexcept { return m_sizeBytes; }

private:
  HANDLE m_file = nullptr;                               // R4: SDK spelling kept as-is
  std::uint64_t m_sizeBytes = 0;
  bool m_isOpen = false;
};

} // namespace Outpost
```

### Enforcement

| Rule | Enforced by |
|---|---|
| The naming table, R1, R3, R5, R8 | [`.clang-tidy`](.clang-tidy), gated in CI over the whole tree |
| R2 affixes, R7 file names and project registration, R11 spellings, R12's ban on WRL, R17 HLSL files, §4's HLSL semantics, §2 directory shape, §3 build settings and the alignment of Debug with Release and of ARM64 with x64 | `Build/CheckProjectFiles.py`, gated in CI |
| R4, R6, R9, R10 | Review. Check your own diff against the table before handing it back. |

**Both checkers run in CI on every change** (§6): `Build/RunClangTidy.py` drives clang-tidy over every hand-written translation unit with the switches its project sets, and `Build/CheckProjectFiles.py` carries what clang-tidy cannot express. Run them yourself before you push (§3). A rule that one of them could carry and does not is a gap in the checker; close it in the change that finds it.

---

## 2. Repository shape

The first layout was settled when the first project landed ([ADR-001](Design/ADR/ADR-001-repository-layout.md)); its present shape, an engine and a game split along the client/server line the game will grow into, is [ADR-003](Design/ADR/ADR-003-engine-and-game-layout.md), [ADR-016](Design/ADR/ADR-016-server-suites.md) gave the server side its suites, [ADR-020](Design/ADR/ADR-020-nvf-import.md) added the model importer and `Tools/`, and [ADR-023](Design/ADR/ADR-023-tests-and-tools-folders.md) grouped the test suites in `Tests/` and the tools in `Tools/`. One solution, `Outpost.Voxel.slnx`, sits at the root. A library and the game's executable live at `<Name>/<Name>.vcxproj`, a test suite at `Tests/<Name>/<Name>.vcxproj` and a tool at `Tools/<Name>/<Name>.vcxproj`, and a project's namespace is its name:

| Project | Kind | References | Holds |
|---|---|---|---|
| `NeuronCore` | static library | — | The engine core that client and server share: maths, the voxel model, the `.vox` reader, NVF's reader, writer and importer ([ADR-019](Design/ADR/ADR-019-nvf-format.md), [ADR-020](Design/ADR/ADR-020-nvf-import.md)), the C++ twins of the GPU algorithms (R15), the reference tracer. No Windows or Direct3D header. |
| `NeuronClient` | static library | `NeuronCore` | The client engine: Direct3D 12, passes and their shaders, the canvas and its twin (R15), window, input, clock, and the client's session with a server and its snapshots ([ADR-018](Design/ADR/ADR-018-client.md)). Owns `WindowsSdk.h`, the one header that defines the Windows macro family (§4). |
| `NeuronServer` | static library | `NeuronCore` | The server engine: `ServerHost`, which runs the tick and the sessions, and the `World` it simulates through ([ADR-015](Design/ADR/ADR-015-client-server-boundary.md)). No Windows header. |
| `GameLogic` | static library | `NeuronServer`, `NeuronCore` | The game's rules, on the server side: the sector, its layout, routes, flight and detonations ([ADR-017](Design/ADR/ADR-017-sector.md)). |
| `GameLib` | static library | `NeuronClient`, `NeuronCore` | The game on the client side: the space scene, its camera and keys, and the bench. |
| `Outpost` | Win32 application | `GameLib`, `GameLogic`, `NeuronClient`, `NeuronServer`, `NeuronCore` | `Outpost.exe`: `wWinMain` and the command line. The client, and for now the server process as well. |
| `NvfImport` | console application | `NeuronCore` | `NvfImport.exe`: a `.vox` into an `.nvf`, merged with the hardpoints Blender authored, `--check` and `--dump` ([ADR-020](Design/ADR/ADR-020-nvf-import.md)). A tool of the asset pipeline, not of the game. |
| `NeuronCoreTests` | test DLL | `NeuronCore` | CPU tests. |
| `NeuronClientTests` | test DLL | `NeuronClient`, `NeuronCore` | GPU tests on WARP, and the client's session and snapshots on the CPU. |
| `NeuronServerTests` | test DLL | `NeuronServer`, `NeuronCore` | The server host, over a world of its own. |
| `GameLogicTests` | test DLL | `GameLogic`, `NeuronServer`, `NeuronCore` | The sector, alone and through a server host. |

**Engine below, game above, client and server apart.** The `Neuron*` libraries are the engine and know nothing of this game; `GameLib` and `GameLogic` are the game and build on them. The client side (`NeuronClient`, `GameLib`) and the server side (`NeuronServer`, `GameLogic`) never reference each other: what both need lives in `NeuronCore`, or, for the game, in a shared game library created when the first such type appears. `Outpost.exe` is the client and, until the two are separated, the server process too, so it links both sides; when the server moves into its own `Server.exe`, that executable takes `GameLogic` and `NeuronServer`, and the client keeps the rest. `NvfImport` is a tool: it takes `NeuronCore` alone, and nothing links it. A project lists every static library it links as a reference, and puts another project's folder on its include path only if it references it.

Everything builds to `<Platform>\<Configuration>\` at the root, where CI looks in `x64\Debug\` for the test DLLs, with intermediates under `<Platform>\<Configuration>\obj\<Project>\`. Adding a project changes this table and needs an ADR of its own, as the layout did. The constraints below hold for every project, present and future.

**C++ is flat; shaders live in `Shader`.** C++ source lives directly in its project's folder, wherever that folder is. This is not taste: `.clang-tidy`'s `HeaderFilterRegex` matches a header only when it sits directly in a folder named for its project, so **a header in a subdirectory is silently unchecked** — no findings, no warning, and nobody notices for months. A subdirectory that holds C++ is an exception, and an exception is an ADR plus a matching change to the filter. The one subdirectory there is holds HLSL: shaders belong to the library that uses them and live in its `Shader` folder, `<Project>/Shader/`, beside no C++ (R17, ADR-003).

**`Tools/` holds the tools.** A C++ tool is a project in its own folder there, as `NvfImport` is at `Tools/NvfImport/` (ADR-023). Beside it, the Blender extension's Python lives in `Tools/Blender/`, and the golden file that holds NVF's two implementations to one another in `Tools/Golden/` (ADR-020). No project builds anything in those two, and `Build/CheckProjectFiles.py` fails C++ or HLSL outside a project's folder, in `Tools/` as anywhere.

**The edges run one way, and a layer never reaches sideways.** Library code is built on by application code and never the reverse (R9), and two libraries at the same level share what is below them rather than each other. An edge that only exists "for now" is an edge, and it is the one that will be impossible to remove later.

**The project files are part of the source.** Adding, removing or moving a file means editing the owning `.vcxproj` **and** its `.filters`. A file that compiles locally but is missing from the project fails only in CI — or worse, links a stale object nobody notices.

**A new project is registered in `.clang-tidy`'s `HeaderFilterRegex`** in the same commit that creates it. A project missing from that list has headers that nothing checks.

**Build and IDE output is never committed** — `x64/`, `ARM64/`, `.vs/`, `*.user`, the restored NuGet packages in `packages/`, and anything a build step generates.

---

## 3. Build and verify

**x64 and ARM64 are the platforms** ([ADR-012](Design/ADR/ADR-012-arm64-platform.md)). Every project and the solution have Debug and Release on each, and nothing else: no Win32/x86 configuration, and no code that only works at 32 bits or on one of the two. CI builds and tests x64 only (§6).

**The compiler settings are the settings.** Toolset `v145` (Visual Studio 2026), `/std:c++latest`, `/permissive-`, `/W4` with **warnings as errors**, `/fp:precise`, `/arch:AVX2` on x64 and `/arch:armv8.7` on ARM64. There is no CMake. If a build error tempts you to change the toolset, lower the language standard, turn off `/permissive-` or silence a warning — **stop and report instead.**

**`/fp:precise` and the instruction set are stated explicitly in every configuration**, not inherited from an MSVC default — a default is not a decision. `/arch:AVX2` sets a CPU floor (Intel Haswell, AMD Excavator); an older CPU meets an illegal instruction, not a message. It also lets MSVC contract `a*b+c` into an FMA even under `/fp:precise`, so float results can differ from a build without it. If that matters for a piece of code, it is an ADR, not a local workaround. `/arch:armv8.7` is ARM64's floor in the same way: it lets the compiler use any Armv8.7-A instruction, so an older ARM64 CPU can meet one it does not have.

**Debug and Release are aligned by rule, not by luck.** Every setting that is not *about* optimisation reads identically in both configurations: language standard, conformance, warning level, include directories, precompiled header, floating-point model, instruction set. The two differ in exactly four things — `Optimization`, `_DEBUG` vs `NDEBUG`, `FunctionLevelLinking`/`IntrinsicFunctions`, and the linker's folding and LTCG switches. (MSBuild spells those four through a few more properties — `UseDebugLibraries`, `RuntimeLibrary` as the debug or release CRT, `LinkIncremental`, `WholeProgramOptimization`, `EnableCOMDATFolding`, `OptimizeReferences` — and that list is the whole of what may differ.)

**The two platforms are aligned the same way.** A configuration reads identically on x64 and ARM64 except for its instruction set, `EnableEnhancedInstructionSet`, and that is the whole of what may differ between them.

That alignment matters more than it looks, because **CI builds Debug|x64 only** (§6). Release is compiled by whoever ships and ARM64 by whoever runs on it, and a Release or an ARM64 build that quietly lost an include directory or sat on an older language standard would not be discovered until then. A static check of the four configurations is what stands in for the builds CI does not run.

**Build through the solution, never a `.vcxproj` directly.** Output paths and cross-project include directories are anchored on `$(SolutionDir)`, and MSBuild defines `SolutionDir` only for a solution build. Building a project file directly resolves every one of those paths against the *project* folder instead of the repository root. **It does not fail — that is the problem.** Output lands in the wrong folder, so the next solution build links against whichever copy is staler, and every cross-project include path becomes a directory that does not exist. The breakage is latent: it bites the first time a file reaches across projects, which may be weeks after someone got into the habit. To build one project, use `/t:<ProjectName>` on the solution.

```powershell
# NuGet packages, after a fresh clone or a package change. Visual Studio restores them when it builds.
msbuild Outpost.Voxel.slnx /t:Restore /p:RestorePackagesConfig=true /nologo /v:minimal

# Everything, from the repository root, naming the solution.
msbuild Outpost.Voxel.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo

# One project, still through the solution.
msbuild Outpost.Voxel.slnx /t:<ProjectName> /p:Configuration=Debug /p:Platform=x64 /m /nologo

# Release, before you claim anything about it.
msbuild Outpost.Voxel.slnx /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo

# ARM64: any of the above with /p:Platform=ARM64, on an ARM64 machine or with Visual Studio's ARM64 build tools.
msbuild Outpost.Voxel.slnx /p:Configuration=Release /p:Platform=ARM64 /m /v:minimal /nologo
```

**A project does not put its own directory on the include path.** `cl.exe` already searches the directory of the including file first for a quoted include, so `#include "FileReader.h"` from a `.cpp` in the same folder resolves without help. Only the directories of *other* projects are listed, as `$(SolutionDir)<Project>`.

**Run the tests**, through `vstest.console.exe`, over every suite the build produced. The Blender extension's suites are Python's and need no build (R20). Those that drive Blender skip without its `bpy` module, which `pip install bpy==4.2.*` provides on Python 3.11:

```powershell
python -m unittest discover -s Tools\Blender\NeuronVoxelFormat\Tests -p "*Tests.py"
```

**vstest reports "no tests found" as a pass.** An empty suite is therefore worse than no suite: it is a green check mark over a library nobody exercised. Every test project ships a placeholder `SuiteSmoke` for exactly this reason; delete it when the first real test lands, never before.

**Run the checkers before you push.** They are seconds of Python and they are what CI runs:

```powershell
python Build\CheckFormat.py           # clang-format, whole tree. --fix rewrites the offenders
python Build\CheckProjectFiles.py     # build shape, project registration, R2/R7/R11
python Build\RunClangTidy.py          # needs a Developer PowerShell (INCLUDE must be set)
```

**A green build says nothing about whether the program works.** For anything a user can see, hear or touch, launch the executable and try it.

**Report what you actually did.** "Builds clean, not run" and "builds and runs" are different claims. Never imply the second when you only did the first, and say which configurations you built.

---

## 4. Layout and formatting

[`.clang-format`](.clang-format) is the authority for C++ layout: 2-space indent, 140 columns, Allman braces, pointer and reference bound left, includes never reordered. HLSL is laid out by the same file, as C++; the two constructs clang-format misreads are a semantic after a function's parameter list and a semantic on a parameter of a function that carries an attribute, so an entry point returns a struct whose members carry the output semantics, and one with an attribute such as `[numthreads]` takes a struct whose members carry the input semantics ([ADR-005](Design/ADR/ADR-005-shader-toolchain.md)). [`.editorconfig`](.editorconfig) covers everything clang-format does not — CRLF, UTF-8, final newline, trailing whitespace, and the non-C++ formats — and repeats the two numbers an editor needs before the first save.

**This tree is formatted, and CI keeps it that way.** A whole-tree format check here is a no-op. Format what you write; if the check fires, run `--fix` and commit the result rather than arguing with it.

- **Do not reformat what your task did not touch.** The check being green tree-wide means a drive-by reformat produces pure churn and buries your actual change.
- **Include order is load-bearing and grouped by hand**, which is why `SortIncludes` is `Never`: `pch.h`, then `<windows.h>` before any other Windows SDK header, then the rest of the SDK, then project headers, then the standard library. A formatter reordering these behind a change's back is a correctness risk, not a style preference.
- **One header owns the Windows macro family, and nothing else defines any of it.** `NOMINMAX`, `WIN32_LEAN_AND_MEAN` and any other `NO*` switch are set in that one header, before `<windows.h>`, and the project files deliberately define none of them. Two owners of one macro is C4005, and `/WX` makes that fatal — `/D` spells a bare macro as `1` where a `#define` spells it as nothing, so the collision is guaranteed rather than possible. If you need `<windows.h>`, include that header; do not add the macros yourself.
- Do not silence a diagnostic with `#pragma warning(disable: ...)` to make a build pass. Fix the cause, or report it.

---

## 5. Rules for this codebase

**R12 — Memory and resource lifetimes are plain C++.** `new`/`delete` where it must be, RAII everywhere, standard containers by default. COM objects are held in `winrt::com_ptr` and Windows handles in `winrt::handle`, both from C++/WinRT's `<winrt/base.h>` in the Windows SDK. `Microsoft::WRL::ComPtr` is not used, and a raw `AddRef`/`Release` pair in new code is a defect, not a style. An `HRESULT` that means failure is checked with `winrt::check_hresult`, which throws `winrt::hresult_error` and hands C++/WinRT the file and line it was checked at; one the code expects and handles on the spot, such as the end of an enumeration or an optional feature that is absent, is tested with `FAILED` and handled there. `WindowsSdk.h` includes `<unknwn.h>` before `<winrt/base.h>`, which is what makes `com_ptr` work with classic COM interfaces such as `ID3D12Device`, and every program links `WindowsApp.lib`, which C++/WinRT needs. No pool, slab or free-list allocator without a decision recorded in `Design/ADR/`.

**R13 — A string you do not write is `const`.** `/permissive-` turns on `/Zc:strictStrings`: a literal is `const char[N]` and will not bind to `char*`. The fix is `const` on the signature, never a cast at the call site — a `const_cast` here is a lie about a literal that lives in a read-only section, and writing through it is a real crash rather than a theoretical one.

**R14 onward are project-specific rules with a design source.** A design document does not only say what to build; some of what it says constrains how the code is *shaped*. Those rules live here, each citing the design it comes from, [`Design/Archive/SampleRenderer.md`](Design/Archive/SampleRenderer.md), [`Design/Archive/SpaceScene.md`](Design/Archive/SpaceScene.md), [`Design/Archive/NeuronVoxelFormat.md`](Design/Archive/NeuronVoxelFormat.md) or [`Design/GameConcept.md`](Design/GameConcept.md), and new ones are added at the end without renumbering anything above. Do not invent one without a design decision behind it, and do not import one from another tree: a rule with no source behind it is a rule nobody can settle an argument with.

**R14 — The voxel record is 32 bits and the palette has 16 entries.** Eight bits per model coordinate and four for the colour, which is the palette entry minus one (design D5, §7.1). The owner fixed the palette at sixteen entries. Widening any field is a format change, and a format change is an ADR.

**R15 — No algorithm exists only on the GPU.** Every algorithm a shader runs — the ray-box intersection, the screen-space bounds, the explosion pose, the placed voxel's box and the search for its placement, the packing, the sky's galaxy and sun, the star's point-spread function, bloom's filters — has a C++ twin in `NeuronCore` under the same name, and a test compares the two (design D10, §14). The twin is the reference; a shader that disagrees with it is the defect until shown otherwise. One exception, the owner's: the canvas's twin lives in `NeuronClient` beside the canvas, because only the client draws one ([ADR-010](Design/ADR/ADR-010-canvas-text-overlay.md)). It includes no Windows or Direct3D header all the same, and it is the only exception; another needs its own ADR.

**R16 — A layout shared with HLSL has one source.** The C++ struct is the truth, with `static_assert`s on its size and on every member's offset; its HLSL mirror is written once, in a `.hlsli`; and the echo test in `NeuronClientTests` proves the two agree (design §7.4). Nothing else redeclares the layout.

**R17 — HLSL follows §1.** A `.hlsl` file is one entry point: nothing but `#define` switches and exactly one `#include`. Algorithms live in `.hlsli` files. Both are PascalCase and live in the `Shader` folder of the library that uses them (§2); `.hlsl` is built as `FxCompile` and `.hlsli` is listed as `None`. A `.hlsl` file is named for its shader and its stage, the stage spelled as the profile spells it: `<Shader>VS.hlsl` for a vertex shader, `<Shader>PS.hlsl` for a pixel shader, `<Shader>CS.hlsl` for a compute shader, and its header array is the same name in `UPPER_CASE` (`ViewSplatAlignedPS.hlsl`, `VIEW_SPLAT_ALIGNED_PS`). Every shader is compiled for Shader Model 6.7 ([ADR-007](Design/ADR/ADR-007-shader-model-and-names.md)). The naming table of §1 applies to HLSL identifiers, and semantics and intrinsics keep the SDK's spelling (`SV_Position`, `SampleCmpLevelZero`) (design §6.2). `Build/CheckProjectFiles.py` enforces the files; review enforces the names.

**R18 — Client and server share bytes, never objects.** What the server knows reaches the client only as messages through a `Transport`, in one process as in two (SpaceScene S3 and S20, [ADR-015](Design/ADR/ADR-015-client-server-boundary.md)). Project references already keep the two sides apart (§2). `Outpost`, the one project that links both, creates the transport and hands each side its own end, and nothing else crosses: no pointer, reference or object of one side reaches the other. Review enforces it.

**R19 — The opponent plays through a session.** The computer opponent sees the world only as its side's snapshots and acts on it only by commands, through a `Transport`, as a client does ([`Design/GameConcept.md`](Design/GameConcept.md) G16 and G27). Nothing hands it more than its side is sent, fog of war included (G21), so every rule the player meets, it meets. `Outpost`, which creates it beside the server, hands it its own end of a transport and nothing else: no pointer, reference or object of the server's world reaches it. The project that holds it references `NeuronCore` and the shared game library, never `NeuronServer` or `GameLogic` (GameConcept §10), so the compiler keeps what it can of this rule. Review enforces the rest.

**R20 — NVF has one specification and two implementations.** [`Design/Archive/NeuronVoxelFormat.md`](Design/Archive/NeuronVoxelFormat.md) §4 is the specification. The design is archived, but §4 is no record: it is the one section of an archived design that still changes. `NeuronCore/NvfModel.cpp` implements it for the engine and `NvfImport`, and the Blender extension's `Tools/Blender/NeuronVoxelFormat/NvfFormat.py` for Blender (design N7, §8). The golden file, `Tools/Golden/Golden.nvf`, keeps the two in agreement: each one's tests write it byte for byte and read it back, and corrupt it alike into the same refusals, checked in the order [ADR-019](Design/ADR/ADR-019-nvf-format.md) fixes. A change to the format changes §4, both implementations and the golden file in one commit, and is an ADR. CI runs both suites, the C++ on Windows and the Python on Linux.

**R21 — The world and the opponent run on the tick and the seed.** Neither reads a clock: the world's time is its tick number times the period ([`Design/Archive/SpaceScene.md`](Design/Archive/SpaceScene.md) S11, §5.4), and the opponent budgets its thinking in work, never in time. Neither draws randomness from anything but `PcgHash` of the seed. So a sector runs the same from the same arguments, tick for tick, within one build, and a skirmish replays from its seed, its build, its libraries and the server's command log ([`Design/GameConcept.md`](Design/GameConcept.md) G41 and G53). The sector's tests already run a seed twice and compare the bytes (ADR-017); the opponent's will too, and a logged skirmish will replay. The host's thread, which only decides when a tick runs and never what is in it (ADR-015), is outside the rule. Review enforces the rest.

---

## 6. Working rules

**Stay in scope.** Do what the task asks. Adjacent code that offends you is not part of the task — note it in your report and move on. Unrequested "while I was in there" changes are the main way a young tree acquires regressions it cannot bisect.

**Record decisions as ADRs.** An engineering decision — a file format, a wire protocol, a subsystem's shape, a third-party dependency, an exception to a rule here — goes in `Design/ADR/` as one file per decision, numbered in order from `ADR-001-<slug>.md`, stating the context, the decision and what it forecloses, in the same commit as the change that implements it. Figures in an ADR are measured, not estimated — if you quote one, say how you measured it. A decision nobody wrote down gets re-litigated every few months by whoever forgot it.

**The checkers are part of the build.** `Build/CheckFormat.py`, `Build/CheckProjectFiles.py` and `Build/RunClangTidy.py` are what §1, §2 and §3 lean on. Each prints what it checked and exits non-zero on a finding; run them before you push, and extend one rather than working around it.

**What CI runs.** [`.github/workflows/build.yml`](.github/workflows/build.yml) has two jobs: a Windows job that checks the build shape, restores the NuGet packages, builds **Debug|x64**, runs the test suites, checks the models in `GameData` against their sources and then runs clang-tidy; and a Linux job that checks formatting on a pinned clang-format and runs the Blender extension's Python tests with the system Python, skipping those that need Blender. **Every step blocks.** Nothing is `continue-on-error`, and a checker that fails fails the build. While the tree was empty, each gate was guarded on the file it needed; those guards came off when the solution and the checkers landed, so a missing solution, checker or test suite is now a failure rather than a skip. Never add a guard back to get past a red build.

**CI does not build Release or ARM64.** The Windows build is the slow half of the pipeline, and each further configuration roughly doubles it for a tree whose configurations differ only in optimisation and instruction set. What stands in for them is the static alignment check on the four configurations (§3) — and an actual build by whoever needs one: Release by whoever is shipping, before a release, and ARM64 by whoever runs on it (ADR-012). If you change something that could plausibly break only under optimisation, build Release yourself and say so. If it could break on one platform only — an intrinsic, or code that leans on x64's stronger memory ordering — build the other one and say so.

**Commits and PRs.** Branch off `main`; small, focused commits with an imperative subject describing the change, not the process. One change per PR. CI must be green. Never commit build output, `.vs/` or `.user` files.

---

## 7. Before you hand work back

- [ ] Naming conforms to §1 — `_` on parameters, `m_` on class state, `UPPER_CASE` constants, `PascalCase` enumerators, no `I`/`C`/`Base` affixes.
- [ ] Only the lines the task required were changed; no reformatting, no drive-by fixes.
- [ ] New, removed or moved files are in the `.vcxproj` **and** the `.filters` of every project involved.
- [ ] A new project is registered in `.clang-tidy`'s `HeaderFilterRegex`.
- [ ] No project's `ConformanceMode`, `LanguageStandard`, `WarningLevel` or `TreatWarningAsError` was changed, and no warning was silenced with a pragma.
- [ ] Debug and Release, and x64 and ARM64, still agree on everything §3 says they must.
- [ ] The checkers pass: `Build/CheckFormat.py`, `Build/CheckProjectFiles.py`, `Build/RunClangTidy.py`.
- [ ] It builds Debug|x64, and every test suite runs and passes.
- [ ] If it changes anything a user can see, hear or touch: it was **run**, not just built.
- [ ] `Design/ADR/` has a new file if the change *was* a decision.
- [ ] Your report states plainly what you verified, what you assumed, and any rule here you had to bend.
