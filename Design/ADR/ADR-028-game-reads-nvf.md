# ADR-028 — The game reads `.nvf`

**Status:** accepted, 2026-09-29 · **Lands with:** phase 1 of [`Design/MvpPlan.md`](../MvpPlan.md), its task 6 · **Renumbered:** from ADR-026, which phase 1's commits cite, when `main` gave ADR-024 and ADR-025 to the fragmented detonation and its light · **Amends:** [ADR-018](ADR-018-client.md), whose session read each model the welcome names as `<name>.vox`; [`SampleRenderer.md`](../Archive/SampleRenderer.md) §7.1, whose loader the game no longer uses; and [`NeuronVoxelFormat.md`](../Archive/NeuronVoxelFormat.md) §10, whose follow-up this is

## Context

NVF's N1 keeps MagicaVoxel's files as sources: `NvfImport` makes the `.nvf` the game reads, and the game never reads a `.vox` (NVF §3, §6). Until now `Outpost.exe` still drew `.vox`, because the renderer kept its loader until a follow-up moved it (NVF §10). The game concept's G-M1 and phase 1 of the MVP plan ask for that follow-up now.

Phase 1's hulls make it necessary rather than tidy. Their mounts are markers, which the `.vox` reader keeps as turned models, and the renderer refuses to draw a turned model. Their `.nvf` files keep the markers as hardpoints and leave them out of the voxels.

NVF §2 expected the follow-up to need per-part transforms, "which is renderer work". It no longer does:
- the renderer already draws a model as placements, one per part, each at its part's origin ([ADR-014](ADR-014-placements.md));
- a part at rest is never turned (N2).

So a part needs only its origin in model space, which is the sum of the translations from part 0 down to it.

## Decision

**`NeuronCore` flattens an `.nvf` into what the renderer and the server's measures take.** `FlattenNvfModel` turns an `NvfModel` into a `VoxModel`:
- each part becomes an unturned instance, named by its path and placed at its origin in model space;
- the records stay in the parts' order, and the palette is kept as it is;
- `version` is 0 and there are no render objects, since the model came from no `.vox`;
- hardpoints stay behind: they are the game's, and `GameCore` reads them from the `NvfModel` ([ADR-026](ADR-026-game-core.md)).

**`ReadNvfFile`** returns a file's bytes, which a manifest hashes, as `ReadVoxFile` did. `LoadNvfModel` now reads through it.

**The session reads `<name>.nvf`.** For each model the welcome names, it hashes the file's bytes with FNV-1a, parses them with NVF's reader and flattens them, before a snapshot is taken. Its refusals keep their names, and `ModelNotLoaded` now carries NVF's refusal: a file that is not an `.nvf` is `Garbage.nvf: NotAnNvfFile`.

**The sector measures and hashes `<name>.nvf`** the same way. Server and client therefore still hash one file's bytes and find an entity's box through one function, `OccupiedBounds` (ADR-018). The protocol does not change: a manifest entry is still a name and a hash, now the hash of an `.nvf`.

**`Outpost`'s build copies every `.nvf` in `GameData` beside the executable, and no `.vox`.** That is the three assets and phase 1's hulls, modules and asteroids. The suites find `GameData` by its `.nvf` files.

**The `.vox` reader stays** for `NvfImport`, and for the tests that read the assets' sources.

## Figures

Measured by the suites built natively by GCC 13.3 for x86-64 against a throwaway stand-in for the test framework, as ADR-026's were. `ClientSessionTests` ran with `ClientSession` and `SnapshotBuffer` compiled apart from `NeuronClient`, whose precompiled header needs the Windows SDK.

- **`FlattensAsTheVoxReaderReads`** covers each of the three assets. Its `.nvf`, flattened, gives the model `ParseVoxModel` reads from its `.vox`:
  - the same records in the same order;
  - the same instance origin, size and range, unturned;
  - the same palette and the same occupied bounds.

  The golden file's three-part tree flattens to origins (−2, 0, −3), (−1, 3, −1) and (0, 4, 2): each part's translation summed down the tree.
- **The sector.** `BuildsTheDefaultSector` measures the same radii from the `.nvf` files as from the `.vox`: 199.11, 45.81 and 22.15 units. `GameLogicTests` passes 16 of 16, `ClientSessionTests` 8 of 8 and `NeuronCoreTests` 206 of 206.
- **The frames.** The renderer receives the same records, palette and placements as before. The one difference is an instance's name, `main` where the `.vox` gave none, and the renderer reads a name only to report a turned model it refuses. So the space scene draws the same frames by construction. Nobody has seen them yet: the owner runs `Outpost.exe` at phase 1's checkpoint.

## What this forecloses

- The game reading a `.vox`, anywhere (N1).
- A second loader for the game's models. The client and the server read through `ReadNvfFile`, `ParseNvfModel` and `FlattenNvfModel`.
- Drawing a part turned at rest. A part that turns in play is a transform the game sets on its placement, never one baked into the model.
