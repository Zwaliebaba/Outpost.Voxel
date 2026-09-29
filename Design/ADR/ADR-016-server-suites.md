# ADR-016 — The server side's test suites

**Status:** accepted, 2026-09-28 · **Lands with:** S-M3 of [`Design/SpaceScene.md`](../SpaceScene.md) (§15, §16, §18) · **Amends:** [ADR-003](ADR-003-engine-and-game-layout.md)'s table of projects, and its note that `NeuronServer` and `GameLogic` have no suite yet · **Amended by:** [ADR-023](ADR-023-tests-and-tools-folders.md), which moves every suite into `Tests/`

## Context

ADR-003 gave every library a suite, `<Library>Tests`, once it has code worth testing, and CI finds suites by that name. Until S-M3, `NeuronServer` and `GameLogic` held a precompiled header and nothing else. S-M3 gives them code:
- `NeuronServer` gains `ServerHost` and the `World` it simulates ([ADR-015](ADR-015-client-server-boundary.md));
- `GameLogic` gains the sector, its routes and its flight ([ADR-017](ADR-017-sector.md)).

`AGENTS.md` §2 says a new project changes its table and needs an ADR of its own, and the design names the two suites (§15; the owner kept the names, §17 question 16).

## Decision

**Two test DLLs join ADR-003's table:**

| Project | Kind | References | Holds |
|---|---|---|---|
| `NeuronServerTests` | test DLL | `NeuronServer`, `NeuronCore` | `ServerHost` over a world of the tests' own: the handshake, snapshots, pause, commands, sessions and the thread. |
| `GameLogicTests` | test DLL | `GameLogic`, `NeuronServer`, `NeuronCore` | The sector, its routes and its flight, alone and through a `ServerHost`. |

**`GameLogicTests` references `NeuronServer`.** Several of its tests drive a sector through a `ServerHost` and clients over a loopback:
- two runs of one seed send the same bytes;
- a command from one client detonates for every client;
- a client that joins late receives the same event.

That is the server side testing itself, and neither suite references the client side.

**One project file.** Both project files are made from `NeuronCoreTests`', so their settings match it in every configuration: Debug and Release on x64 and ARM64 (ADR-012). `Build/CheckProjectFiles.py` holds them to that, as it holds every project.

**Nothing else changes.**
- CI builds every `*Tests.vcxproj` it finds and requires its DLL, so it builds and runs both suites without a change to the workflow.
- `.clang-tidy`'s `HeaderFilterRegex` already matches every `*Tests` folder.
- The suites find `GameData` as the others do: above their working directory, then above their sources.

`GameLib` still has no suite.

## What this forecloses

- **A server-side suite that references a client-side library,** `NeuronClient` or `GameLib`, or the other way round.
- **Testing `GameLogic`'s flight through a client.** The client's side is tested in its own suites, as S-M4's `SnapshotBuffer` will be in `NeuronClientTests` (§15).
