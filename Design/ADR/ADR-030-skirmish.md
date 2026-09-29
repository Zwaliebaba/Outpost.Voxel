# ADR-030 — The skirmish: its layout, its designs as composites, and its names

**Status:** accepted, 2026-09-29 · **Lands with:** phase 2 of [`Design/MvpPlan.md`](../MvpPlan.md), its tasks 2 to 5: `GameCore`'s layout, composites and names, then `GameLogic::Skirmish`, then the command line and the client · **Amends:** [`AGENTS.md`](../../AGENTS.md) §2, where `GameLogic` and its suite, `GameLib` and `Outpost` now reference `GameCore`; and [`MvpPlan.md`](../MvpPlan.md) §8.4, whose fields it sizes · **Amended by:** [ADR-032](ADR-032-sides-sessions-and-fog.md), whose skirmish serves each side what its sensors reach, refuses a command on the other side's entity, and is played in the window as side 1; [ADR-033](ADR-033-orders-and-flight.md), whose ships fly to their orders; and [ADR-034](ADR-034-interface-layer.md), whose first view is the strategic camera's, on the side's core

## Context

Phase 2 puts the MVP's skirmish on screen: two cores, each side's starting ships, and six asteroid fields (§2.1, §8.4). [ADR-029](ADR-029-protocol-composites-and-sides.md) gave the protocol what that needs: composites, sides and a payload for the game.

The concept asks for three things of the layout (§3, §7.1, G66):
- **Symmetry.** The sector is its own half turn about its center, with the sides exchanged, so that neither start is favored.
- **Shared code.** The layout comes from the seed through a generator in the shared library, which both sides could run.
- **Alignment.** Asteroids stand at whole coordinates and turned by quarter turns, which no build rounds differently.

The plan gives the cores' places and the fields' centers. It leaves open how many asteroids a field holds and how they are placed, how a design becomes a composite, which colors the sides wear, and what the game first says through the payload.

## Decision

**The generator** is `GameCore::MakeSkirmishLayout(seed)`, a pure function of the seed. It draws from `PcgHash` of an index and a stream offset by the seed, as the sector's layout does ([ADR-017](ADR-017-sector.md)), and in integers throughout.

It lays out half of the sector:
- **Side 1's core** is anchored at (−1,800, 0, 0), turned a quarter turn about +y, so that a design's front, +Z, faces +x, toward side 2.
- **Side 1's ships** are two miners and two gunships, anchored in a line 120 units in front of the core, at z = ±90 and ±30, the miners outside. They face the same way.
- **Three fields:** the two near fields at (−1,400, 0, ±500), each of 6 asteroids within 110 units of its center, and the first middle field at (0, 0, 800), of 10 within 180.
- **Each asteroid:**
  - a place drawn uniformly within its field's radius on the plane, within 20 units of the plane, and at least 48 units from its field's other asteroids, more than the largest asteroid's diameter;
  - then one of the three asteroid models, and one of the cube's 24 rotations.

  The generator tries up to 256 places for each asteroid.

**The other half** is the first turned a half turn, (x, y, z) → (−x, y, −z):
- each anchor's position is turned;
- each turn is composed with the half turn after it;
- side 1 becomes side 2.

So every image is exact, and the layout is its own half turn by construction.

**Where a thing stands.** An *anchor* is where the lower corner of the cell holding the middle of the thing's box goes. The thing stands at the anchor plus its turn of the middle's offset from that corner, `AnchoredPosition`. Every term is a whole or a half voxel, so every voxel's cell is whole, and the thing draws aligned (SpaceScene §7.2). The half turn of an anchor stands exactly at the half turn of the position.

**Measured,** by running `MakeSkirmishLayout` natively:
- over seeds 0 to 99,999, it never left an asteroid out, so a skirmish holds 44;
- the closest two anchors stood 48.00 units apart, the floor.

By arithmetic from the centers and radii, every asteroid of a middle field is anchored more than 1,780 units from either core, beyond the array's 1,200: its center is 1,970 away, and its radius 180.

**The sides.** Side 1, the player's, is blue, (70, 130, 220). Side 2, the opponent's, is red, (220, 80, 60). Each is the color of palette entry 16 in its side's variants (ADR-029).

**A design as a composite** (`GameCore::DesignComposite`):
- The hull comes first, left where it is.
- Then comes the module at each mount, in the hull's order. A module is one part, odd on every axis, authored in its mount's frame with +Z the way it faces ([ADR-027](ADR-027-design-generator.md)). It turns by the mount's turn, stored as the NVF importer's table spells that rotation, so it is exact.
- It moves by the whole voxels that put its center cell *m* on the mount's *c*: *t* = *c* + ½ − *R*(*m* + ½). This is computed in doubled integers, where every component is even, so *t* is exact.
- The command module, 3 × 3 × 3, sits centered in its 3 × 3 × 5 mount's box.

**The names** are the game's first payload, in the welcome (`GameCore::WelcomeNames`): the name of each composite, a design's or an asteroid's, and of each side, which the client shows in its figures.
- **The layout,** little-endian:
  - a `u8` version, 1;
  - a `u32` count of composites, then each name;
  - a `u32` count of sides, then each name;
  - each name a `u8` length and 1 to 255 letters and digits.
- **Refusals:** `Truncated`, `UnsupportedVersion`, `BadName` and `TrailingBytes`. A count beyond the bytes is refused before anything is reserved for it.
- **An empty payload,** such as the sector's, names nothing, and is not refused.

**The world** is `GameLogic::Skirmish`, a `World` beside the sector, which `GameLogic` builds on `GameCore` to serve. It references `GameCore`, as ADR-026 foresaw for the change that first links it.
- **What it validates.** `Create` validates every design of the catalogue, as the server validates any design that enters a match. A hull it cannot read is `ModelNotLoaded`, and any other refusal `DesignRefused`, with the design's name and ValidateDesign's reason.
- **Its models.** It reads each model the composites name from `<name>.nvf`, and hashes and flattens it as the sector does ([ADR-028](ADR-028-game-reads-nvf.md)). The order is each design's hull and modules, then the asteroids.
- **Its welcome** names:
  - the composites: the catalogue's five designs, then the three asteroids;
  - the two sides' colors;
  - in its payload, their names.

  The space scene's lighting and sky are shared through `SpaceSettings`, the sector's tuned constants, so the two worlds look alike.
- **Its entities,** by id:
  - side 1's core and ships, then side 2's;
  - then the asteroids, of no side.

  Each stands at its anchor's `AnchoredPosition`, about the middle of its composite's `CompositeBounds`. Its rotation is the NVF importer's quaternion of its turn, so every voxel is exact and every entity draws aligned.
- **Nothing moves** until phase 4 gives ships orders. An entity detonates on command, once, with a seed from `PcgHash` of the skirmish's count of detonations offset by its seed (R21), and is restored whole.

**The command line and the client.**
- **`Outpost.exe --skirmish`** has the server simulate the skirmish in the sector's place, from `--seed`, at 30 ticks a second. The options that shape the sector, `--stations`, `--frigates`, `--capitals` and `--debris-lifetime`, do not go with it, and neither does `--bench`; each is refused by name. The space scene stays the default until the MVP gate (the plan's §5).
- **The first view** under `--skirmish` frames every entity from the camera's three-quarter view, and targets none. So the layout shows whole, as the checkpoint and a capture at the start need, and N and B then choose each entity in turn. The sector keeps its first view, its first station framed and targeted.
- **The figures name the target** from the welcome's names: its composite's name, then its side's, such as `target 2: Miner, Blue`.
  - The client refuses a payload it cannot decode, and one that names other counts of composites or sides than the welcome holds.
  - An empty payload, such as the sector's, names nothing.
- **References.** `GameLib` references `GameCore`, to decode the names. `Outpost` references it as well, because it links `GameLogic`, which links it.

**Tests.** `GameCoreTests` gains 10:
- `SkirmishLayoutTests`, 5:
  - the layout is its own half turn, with the sides exchanged, on 20 seeds, each image found among the whole layout;
  - each side starts at its core;
  - the fields are where §8.4 puts them, full, aligned and spaced, on 20 seeds;
  - one seed gives one layout;
  - the cube's 24 rotations and anchors are exact.
- `DesignCompositeTests`, 2: every module of every MVP design fills its mount's box, with its center on the mount's and facing the way the mount faces; and a missing model is refused by name.
- `WelcomeNamesTests`, 3: the round trip, each refusal by name, and a name too long to encode.

`GameLogicTests` gains `SkirmishTests`, 6:
- the world is its own half turn on 20 seeds, each entity's image found among all of them, with the side exchanged, standing exactly at the half turn and turned by it;
- it serves the layout and its names;
- everything stands aligned;
- two hosts of one seed send the same bytes over a detonation and a restore, and another seed differs;
- detonations come on command;
- refusals come by name.

GameCore's 50 pass natively, built by GCC 13.3 against the stand-in for the test framework: at `-O1`, again under `-O2 -mfma -ffp-contract=fast`, and with `<windows.h>`'s plain-word macros defined, as MSVC's builds see them.

The logic suite's 22 pass natively the same way.

## What this forecloses

- **A skirmish that is not its own half turn,** or an asteroid off the whole grid or turned other than by quarter turns.
- **A field whose count depends on the seed.** Its places depend on the seed, and its size does not, so the economy's tuning (phase 6) sets the counts and radii as data.
- **A module that is not one part, odd on every axis, in its mount's frame.**
- **Names beyond letters and digits,** until a later version of the payload says otherwise.
