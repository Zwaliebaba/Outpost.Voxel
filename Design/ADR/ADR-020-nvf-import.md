# ADR-020 — NvfImport: the importer, its project, and `Tools/`

**Status:** accepted, 2026-09-28 · **Lands with:** N-M2 of [`Design/NeuronVoxelFormat.md`](../NeuronVoxelFormat.md) (§10) · **Amends:** [ADR-002](ADR-002-voxel-record-and-palette.md)'s refusal of every rotation; [ADR-003](ADR-003-engine-and-game-layout.md)'s table of projects, which gains a tool beside the game

## Context

N-M2 turns a MagicaVoxel scene into an `.nvf`. The `.vox` reader learns the two things markers need (§6.1): the names of nodes, and rotations. `ImportVoxModel` in `NeuronCore` converts a scene, merged with the file it replaces (§6.2), and `NvfImport.exe` is the command line around it (§6.3). The three assets are converted, and CI checks every `.nvf` against its `.vox` (§9).

A new project needs an ADR of its own (`AGENTS.md` §2). So does `Tools/`, the folder N-M1 created for the golden file ([ADR-019](ADR-019-nvf-format.md)), which the Blender extension joins in N-M3 and N-M4. The owner's answers of 2026-09-28 shape two points (§11): a part's default pivot is its geometric centre, and a hardpoint refined in Blender that shares a marker's name stays a refusal.

## Decision

**The reader (§6.1).**
- **Names.** `ModelInstance` gains `name`, the `_name` of the transform directly above the model. A name on a transform above a group names no model and is not read.
- **Rotations.** `ModelInstance` gains `rotation`, one of the cube's 24 rotations in the engine's axes: MagicaVoxel's matrix *R* as *P R P*, where *P* swaps y and z (§4.1). `_r` is decoded as MagicaVoxel's extension document describes: a column for each of rows 0 and 1, the third row taking the one left over, and a sign bit for each row. Everything else stays `UnsupportedRotation`:
  - a reflection, a column named twice or out of range, or text that is not a decimal integer below 128;
  - a turn on a transform above a group, which would turn its models about the group's own pivot;
  - a turned model with an even dimension, whose centre voxel is not its middle.
- **Where a turned model lies.** It turns about its centre voxel, ⌊size / 2⌋, which stays in its cell. Voxel *v* lies in the cell origin + *c* + *R*(*v* − *c*), where *c* is ⌊size / 2⌋.
  - `OccupiedBounds` places a turned model's voxels there, so the sector measures what a marker covers.
  - `VoxelBox` and the voxel grid stay for unturned models, the only ones the renderer draws.
- **The renderer refuses a turned model.** `VoxelScene` throws `std::invalid_argument` naming it (§6.1): a turned model is a marker, and a marker belongs in the `.nvf`. `IsIdentityRotation` joins `RigidTransform.h` for the reader, the importer and the renderer alike.

**The importer (§6.2).**
- **Names (§5).** A node is a part path, `<part>@<hardpoint>` or `<part>@pivot`. A file's only part may be unnamed, and is then `main`.
- **Parts in path order.** A parent's path begins its children's, and `/` sorts before every character a segment may hold, so path order puts every parent first. The file then does not depend on the order of MagicaVoxel's outliner. Each part keeps its records in the `.vox`'s order, and is translated in its parent's space.
- **The default pivot** is the part's geometric centre, size / 2 (the owner, §11 question 5).
- **A marker's hardpoint.**
  - Its position is the centre of its centre voxel in its part's space: an integer and a half, exact in single precision.
  - Its rotation is looked up in a fixed table of the cube's 24 rotations, `CubeRotationQuaternion`. The table spells each one w > 0, or for a half turn w = 0 and the first nonzero of x, y and z positive, and a test holds `RotationOf` to giving each rotation back exactly.
  - It carries `FromVox`.
- **A pivot marker** sets its part's pivot. Its turn is not read: a pivot has no orientation.
- **The merge.** It is keyed by part path:
  - every hardpoint Blender authored is carried to its part; hardpoints from markers are dropped for the markers there are now;
  - a pivot Blender set survives unless a marker now sets one;
  - an authored pivot whose part is gone goes with the part, since a pivot is not apart from its part;
  - an authored hardpoint whose part is gone is refused, and so is one that shares a marker's name. The clash names the marker, which the artist deletes now that Blender owns the hardpoint (the owner, §11 question 6);
  - a previous file with chunks this version does not know is refused, since the rewrite would lose them (§4.6).
- **Every refusal at once.** §6.2 gave the function one error. It returns them all, each `NvfImportError` naming its refusal and its node, in the order of the `.vox`'s models, so that an artist fixes a file in one pass. There are 13 names, among them the format's limits of 1,024 parts and 4,096 hardpoints.

**The tool (§6.3).**
- **`NvfImport.exe`** is a console application with `wmain`, the CRT's wide entry point, because a path may hold what the ANSI code page cannot. `.clang-tidy` exempts `wmain` from the naming rule as it exempts `wWinMain`.
- **Exit codes.** 0 when it wrote the file, or `--check` finds it up to date; 1 when `--check` finds it stale; 2 for a refusal, a mistake on the command line, or a file it cannot read or write.
- **`--check`** imports as an import would, merge and all, and compares the bytes with the file; a missing `.nvf` is stale. It writes nothing.
- **An import** writes through `SaveNvfModel`, a temporary file renamed over the output. It leaves an existing `.nvf` it cannot read as it is, unless `--replace` discards it.
- **`--dump`** prints `DumpNvfModel`'s text, which lives in `NeuronCore` so that a test pins it.

**The project.** `NvfImport/` holds `Main.cpp`, `CommandLine.h` and `.cpp`, and its precompiled header. It references `NeuronCore` alone, and nothing links it. Its project file was made from `NeuronCoreTests`', so its settings match in every configuration and on both platforms, as an Application with the Console subsystem. It is in the solution, in `AGENTS.md` §2's table, and in `.clang-tidy`'s `HeaderFilterRegex`.

**`Tools/`** holds what is not C++: the golden file in `Tools/Golden/`, and from N-M3 the Blender extension in `Tools/Blender/`. No project builds anything there, and `Build/CheckProjectFiles.py` fails C++ or HLSL under it.

**The assets.** `NvfImport` converted each of `GameData`'s `.vox` into an `.nvf` beside it, one part named `main` each. CI's new step runs `--check` over every `.vox` in `GameData` after the tests, and fails on a stale, missing or refused `.nvf`. `NvfImportTests::ImportsTheAssetsAsCommitted` does the same in-process.

## Figures

**The assets, measured from the files `NvfImport` wrote:**

| Asset | `.nvf` bytes | `VOXL` bytes | The rest |
|---|---|---|---|
| `MilitaryStation` | 900,608 | 900,192 | 416 |
| `CapitalShip` | 43,408 | 42,988 | 420 |
| `Frigate` | 5,152 | 4,724 | 428 |

**The suites.** `NeuronCoreTests` gains 15 tests:
- `VoxModelTests` gains 4. All 128 values of `_r` are tried: the 24 rotations on a model odd in every dimension and, but for the identity, refused on models even in one dimension, each in turn; the 24 reflections and 80 malformed values refused. Also a turned group, the names of placing nodes, and §9's asymmetric model with a marker pointing along +X.
- `NvfImportTests` holds 11: one unnamed part, the part tree, markers and pivots, all 24 rotations through the table, the merge, orphans and clashes, every refusal, the limits, the dump pinned, every refusal's name, and the assets.

`NeuronClientTests` gains `VoxelSceneTests::RefusesTurnedModels`, which runs on WARP in CI only.

**Measured, natively.** GCC 13.3 at `-O1` against a throwaway stand-in for the test framework: all 186 tests of `NeuronCoreTests` pass, with `NeuronServerTests`' 8 and `GameLogicTests`' 16.

**Mutations,** one at a time, each failing at least one test before it was taken out:
- in the reader: a reflection accepted; the swap left out; a turned group accepted; the parity of each axis in turn left unchecked. The first test missed the z axis, and was strengthened to try a model even in each dimension alone;
- in the importer: a marker's position without its half; the authored pivot's flag not carried; a clash not refused; hardpoints from markers carried over; Blender's pivot preferred to a marker's; translations not taken in the parent's space; a table entry spelled with the other sign, which the test of the table's spelling was added to catch.

**End to end.** `NvfImport` was built on Linux with a throwaway `main` that calls `wmain`, and run on a scene of two parts, a turned marker and a pivot marker:
- import, dump and `--check`, clean;
- a refinement in Blender, simulated by moving the hardpoint and clearing `FromVox`: `--check` and the import both refuse with `NameClash`, naming `hull@weapon.front`;
- the marker deleted: the import keeps Blender's hardpoint where it was moved, and `--check` is clean;
- `--replace` discards the hardpoint.

## What this forecloses

- **A turned part, group, or model with an even dimension.** A turned marker is the one turned model a `.vox` may hold, and the renderer refuses it.
- **A second path from `.vox` to `.nvf`.** `ImportVoxModel` is the one conversion, and the tool, the tests and CI share it.
- **A merge that drops or overwrites Blender's work silently.** An orphan or a clash stops the import, and `--replace` is the explicit way to discard it.
- **C++ in `Tools/`, and a tool outside a project.**
