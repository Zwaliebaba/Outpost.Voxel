# ADR-003 — Engine and game layout

**Status:** accepted, 2026-09-27 · **Lands with:** M2 of [`Design/Archive/SampleRenderer.md`](../Archive/SampleRenderer.md) (§15) · **Supersedes:** the project layout of [ADR-001](ADR-001-repository-layout.md) · **Amended by:** [ADR-016](ADR-016-server-suites.md), which gives `NeuronServer` and `GameLogic` their suites, and [ADR-018](ADR-018-client.md), which retires `--vox`: the client and the server read every model from the `GameData` beside the executable

## Context

ADR-001 laid out a sample renderer: a core library, a renderer, an application and two test suites. The owner has since set the direction the code grows in. It is a game, Outpost, that will run as a client and a server, on an engine of its own, and the game must be able to split into a client executable and a server executable without references having to be untangled first. M2 is the first milestone that adds client code of any size — window, device, passes — so the layout changes before that code lands rather than after it.

The owner answered three questions on 2026-09-27: the window, input and clock are engine code and the camera and scene are game code; the client-side and server-side game libraries do not reference each other; and the change lands in M2's pull request.

## Decision

**Eight projects.** Each lives at `<Name>/<Name>.vcxproj` and its namespace is its name.

| Project | Kind | References | Holds |
|---|---|---|---|
| `NeuronCore` | static library | — | The engine core that client and server share: maths, the voxel model and the `.vox` reader, the CPU twins of the GPU algorithms, the reference tracer. No Windows or Direct3D header. |
| `NeuronClient` | static library | `NeuronCore` | The client engine: Direct3D 12, passes and their shaders, window, input, clock. Owns `WindowsSdk.h`. |
| `NeuronServer` | static library | `NeuronCore` | The server engine. Empty until the server has code of its own. |
| `GameLogic` | static library | `NeuronServer`, `NeuronCore` | The game's rules on the server side. Empty until the server has code of its own. |
| `GameLib` | static library | `NeuronClient`, `NeuronCore` | The game on the client side: camera controls, scene setup. |
| `Outpost` | Win32 application | `GameLib`, `GameLogic`, `NeuronClient`, `NeuronServer`, `NeuronCore` | `Outpost.exe`: `wWinMain` and the command line. |
| `NeuronCoreTests` | test DLL | `NeuronCore` | CPU tests. |
| `NeuronClientTests` | test DLL | `NeuronClient`, `NeuronCore` | GPU tests on WARP. |

**Engine below, game above.** The `Neuron*` libraries are the engine and know nothing of this game. `GameLib` and `GameLogic` are the game and build on the engine.

**Client and server apart.** `NeuronClient` and `GameLib` are the client side; `NeuronServer` and `GameLogic` are the server side. Neither side references the other. Engine code both need lives in `NeuronCore`. Game code both need gets a shared game library when the first such type appears, and not before: a library created for a need nobody has yet is an edge that exists "for now" (`AGENTS.md` §2).

**One executable for now.** `Outpost.exe` is the client and, until the two are separated, the server process too, so it links both sides. When the server moves into its own `Server.exe`, that executable takes `GameLogic` and `NeuronServer`, and the client keeps `GameLib` and `NeuronClient`; the new project is an ADR of its own.

**What moved.** `VoxelCore` became `NeuronCore`, unchanged but for its namespace. `VoxelRender` became `NeuronClient`, which also takes the window, input and clock that the design had given the application. The camera controls and scene setup go to `GameLib`, and `Outpost`, formerly `VoxelSample`, keeps the entry point and the command line. The suites became `NeuronCoreTests` and `NeuronClientTests`. Every file moved with `git mv`, so its history follows it. `NeuronServer` and `GameLogic` start with a precompiled header and nothing else.

**References name what the linker gets.** A project references every static library it links, directly, as ADR-001's projects did: `Outpost` names all five libraries. A reference list that says what reaches the linker can be checked without knowing how MSBuild propagates references between static libraries.

**Shaders live in `<Project>/Shader/`.** HLSL belongs to the library that uses it and sits in that library's `Shader` folder. It is the one exception to flat project folders, and it costs the header filter nothing, because no C++ lives there: `Build/CheckProjectFiles.py` fails an `.hlsl` or `.hlsli` outside a `Shader` folder, and a `.h` or `.cpp` inside one.

**`WindowsSdk.h` stays in `NeuronClient`**, the only library that needs Windows today. The server side cannot reach it there. Where it lives once `NeuronServer` needs Windows, for sockets or timers, is decided then: moving it into `NeuronCore` would end the rule that `NeuronCore` sees no Windows header, which is what keeps the CPU twins independent of the SDK (ADR-001).

**Tests follow the libraries.** A library gets a suite, `<Library>Tests`, when it gets code worth testing; CI finds suites by that name. `GameLib`, `GameLogic` and `NeuronServer` have none yet.

**The asset.** `Outpost`'s build copies `GameData/MilitaryStation.vox` beside `Outpost.exe`, where the application's default `--vox` looks (design §13). The suites find the file by looking upward from their working directory, which reaches that copy under Test Explorer and the repository's own under CI.

## What this forecloses

- A reference between the client side and the server side, in either direction.
- Game-specific code in a `Neuron*` library, and engine code in `GameLib` or `GameLogic`.
- C++ in a subdirectory, and HLSL outside a `Shader` folder.
- Windows code in `NeuronCore`, and, until a decision says where `WindowsSdk.h` goes, Windows code in `NeuronServer` or `GameLogic`.
- A second executable before the server is separated from the client.
