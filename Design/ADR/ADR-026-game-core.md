# ADR-026 — `GameCore`, the shared game library

**Status:** accepted, 2026-09-29; the owner confirms the name at phase 1's checkpoint, while a rename is cheap · **Lands with:** phase 1 of [`Design/MvpPlan.md`](../MvpPlan.md) · **Renumbered:** from ADR-024, which phase 1's commits cite, when `main` gave ADR-024 and ADR-025 to the fragmented detonation and its light · **Amends:** [ADR-003](ADR-003-engine-and-game-layout.md), which creates the shared game library when the first type both sides need appears

## Context

ADR-003 splits the game along the client/server line and keeps the two sides from referencing each other. What both sides need lives in `NeuronCore`, or, for the game, in a shared game library that ADR-003 creates "when the first such type appears, and not before".

Phase 1 of the MVP plan brings the first such types (the concept's §5.4 and §5.5, G29, G37, G38, G40):
- the catalogue: materials, modules, size classes and designs;
- a design's validation;
- its profile.

The server validates every design that enters a match, flies ships on their profiles and fights with them. The client draws the design card and the HUD from the same profile. The opponent reads it, and R19 lets the opponent's project reference only `NeuronCore` and the shared game library. None of it belongs in `NeuronCore`, which knows nothing of this game.

## Decision

**The project.** `GameCore` is a static library at `GameCore/GameCore.vcxproj`, in namespace `GameCore`.
- It references `NeuronCore` alone, and includes no Windows or Direct3D header.
- Its suite is `GameCoreTests`, at `Tests/GameCoreTests/`.
- `AGENTS.md` §2's table, `.clang-tidy`'s `HeaderFilterRegex` and the solution name both projects.
- A project references `GameCore` from the phase that first links it:
  - `GameLogic`, `GameLib` and `Outpost` from phase 2;
  - the opponent's project from phase 8.

  In phase 1 only its suite links it.

**The catalogue is tables in code** (`Catalogue.h`), until a data format is decided. The concept's §5.2 gives each design a small file of game data, but the MVP's library is fixed: four designs and the core, the same for both sides (the plan's §2.2).
- **Materials:** the light and heavy classes (G13). Each has a density, a toughness and a price per voxel.
- **Modules:** one per kind and size that a design fits. Each has its mass, power supplied or drawn, thrust, sensor range, hold and price.
- **Size classes:** frigate, capital and station (G40). Each has a box, a voxel budget, a speed cap, a turn-rate cap and its command points (G56).
- **Designs:** each names its hull's model, whether it is a ship or a structure, the armor class of each palette entry, and its one fit, the module at each mount by name (G12).

**A design takes the least class of its kind that holds it.** The spec says whether a design is a ship or a structure, never its class. A class holds a design when:
- the design's hull and its mounts' boxes fit its box, axis by axis and unturned;
- its hull voxels fit the budget.

So a frigate-sized hull can never be entered as a capital ship and cost four command points.

**`ValidateDesign` refuses by name** (the concept's §5.5), and checks in this order:
- `HullUnreadable`: the file, or a part tree or voxel range a model built in memory gets wrong.
- The mounts, one by one, in the hull's hardpoint order:
  - `BadMountName`: not ADR-027's `<type>.<s|l>.<label>`, or a type no module has;
  - `MountOffCenter`: not at a voxel's center;
  - `MountOffAxis`: not turned by one of the cube's 24 rotations.
- The command mount: `NoCommandMount`, `ExtraCommandMount`.
- `NoSizeClass`. It comes before anything is allocated over the design's box, which the largest class bounds at 64 × 40 × 96 cells.
- The hull's voxels: `PartsOverlap` (G37), then `NotOnePiece`, face to face.
- Each mount's box: `MountHoldsHull`, `MountsOverlap` and `MountDetached` (G37).
- The fit, entry by entry: `UnknownMount`, `MountFittedTwice`, `UnknownModule`, `WrongModule`.
- Then `UnfittedMount`, since a fit is the module at every mount (G12).
- Then `PowerShort`.

**A blocked line is not a refusal** (G38). Validation walks each engine's, weapon's, sensor's and mining laser's line:
- The line is the column of cells from its box's front face along its facing, to the edge of the design's box.
- It is exact, because a mount turns by quarter turns and stands on a voxel's center.
- A module whose line meets a hull voxel does nothing, and the profile counts it as blocked.
- `GridWalk` waits for arcs, which turn off the axes.

**The profile** (G29) is computed over the plan's subset (§2.2), in double precision and in one order, then rounded to float:
- **Mass** sums each voxel's density and each module's mass. A module's mass counts as a point at its mount's center.
- **Center of mass,** and the **inertia** about it along the model's axes. Each voxel counts as a unit cube: its point moment plus a sixth of its mass.
- **Thrust along each axis,** from the clear engines. Each pushes against the way it faces.
- **Acceleration** is forward thrust over mass. The **speed cap** is the class's.
- **Turning** is the one turn the MVP's plane needs: about the vertical, from vectored thrust.
  - Each clear engine can turn a share of its thrust, `VECTORED_THRUST_SHARE` = 0.25, across its line.
  - Its lever is its distance from the center of mass: fore or aft for an engine along the ship's length, port or starboard for one across it, and in the plane for one firing up or down.
  - Torque over the vertical inertia gives the **turn acceleration**, so a long hull, which has more inertia, turns more slowly (the concept's §5.4).
  - The **turn rate** is the class's cap.
  - The **half turn** is the time to turn half a turn from rest to rest: spin up at the turn acceleration, hold the cap, spin down.
- **Sensor range** is the farthest-seeing clear sensor's.
- **Power supplied and drawn.**
- **Price** is each voxel's class price plus the modules'. The **build time** is the price over `BUILD_CREDITS_PER_SECOND` = 150.
- **Command points** are the class's.

The silhouette joins the profile in phase 5, and the armor over vital modules after the MVP.

**Game data stays out of the files** (NVF's N8). A hull's `.nvf` holds its voxels and mounts. What a mount holds, what a palette entry is made of, and what a module does are here.

## Figures

Measured by `GameCoreTests`, built natively by GCC 13.3 for x86-64 against a throwaway stand-in for the test framework, as ADR-017's figures were:
- at `-O1` without contraction;
- at `-O2 -mfma -ffp-contract=fast`, which forces the contraction MSVC may do under `/arch:AVX2`.

The two agree to the printed digits, and `ProfilesArePinned` holds every build to a thousandth of each figure.

| Design | Class | Voxels | Heavy | Mass | Acceleration, units/s² | Turn acceleration, °/s² | Turn, °/s | Half turn, s | Speed, units/s | Sensor, units | Power | Price, credits | Build, s | Command points |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Miner | Frigate | 1210 | 130 | 1720 | 34.9 | 143.8 | 45 | 4.3 | 60 | 700 | 14 of 25 | 2480 | 16.5 | 1 |
| Gunship | Frigate | 1155 | 498 | 2391 | 25.1 | 93.5 | 45 | 4.5 | 60 | 700 | 19 of 25 | 3819 | 25.5 | 1 |
| Lancer | Frigate | 1119 | 395 | 2149 | 27.9 | 74.6 | 45 | 4.6 | 60 | 700 | 23 of 25 | 3674 | 24.5 | 1 |
| Cruiser | Capital | 9017 | 5707 | 21001 | 5.7 | 8.7 | 8 | 23.4 | 20 | 700 | 39 of 80 | 28848 | 192.3 | 4 |
| StationCore | Station | 28955 | 5264 | 40943 | 0.0 | 0.0 | 0 | never | 0 | 1200 | 72 of 80 | 47097 | 314.0 | 0 |

**Against the plan's targets** (§8.1), which allow a quarter either way:
- **Acceleration.** The frigates accelerate at 25.1 to 34.9 units/s² against 30, and the cruiser at 5.7 against 5.
- **Half turns.** The frigates turn half a turn in 4.3 to 4.6 s, against the 4.0 s their 45°/s cap alone would take. The cruiser takes 23.4 s against 22.5 s.
- **Where the rate comes from.** Vectored thrust holds every ship near its class's rate. The lancer, the longest frigate, reaches it most slowly.

**How the values were set.**
- **The thruster's thrust** (30,000) and the cruiser's heavy armor (63 % of its voxels) were tuned to put four thrusters on the capital ship, as the plan's fit has it, and two on each frigate, within those bands.
- **The shipyard's rate** was set so that the core's one yard builds a budget of 20 command points in the opponent's mix (§8.5), in about 10.6 minutes:
  - the mix is six miners, four gunships, two lancers and two cruisers;
  - six miners take the plan's 12 to 15 minutes to pay for it (§8.3), so income paces production, not the yard.

## What this forecloses

- Game data or rules in a `Neuron*` library, and a `GameCore` that references anything but `NeuronCore` or includes a Windows header.
- A second computation of a profile or a validation. The server, the client and the opponent read `GameCore`'s.
- A design refused without a name, or refused in an order other than the one above.
- A design that states its own class.
- A mount turned off the cube's axes. That rules out arcs for the MVP, and a mount that turns freely is a change to this ADR.
