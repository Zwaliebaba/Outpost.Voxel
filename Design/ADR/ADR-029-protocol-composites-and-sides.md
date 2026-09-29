# ADR-029 — Protocol version 2: composites, sides and the game's payload

**Status:** accepted, 2026-09-29 · **Lands with:** phase 2 of [`Design/MvpPlan.md`](../MvpPlan.md), its tasks 1, 3 and 4 · **Amends:** [ADR-015](ADR-015-client-server-boundary.md)'s messages and `World`; [ADR-018](ADR-018-client.md)'s placements from entities; [ADR-014](ADR-014-placements.md)'s palettes, one per model; and [`SpaceScene.md`](../Archive/SpaceScene.md) §6.2's layout · **Amended by:** [ADR-032](ADR-032-sides-sessions-and-fog.md), whose welcome tells the session its side, at layout version 3, and whose scene adds a remembered variant of every palette

## Context

Phase 2 puts the skirmish on screen. Its ships are designs: a hull, and a module at each of its mounts (G12, [ADR-026](ADR-026-game-core.md)). Every entity belongs to a side and shows the side's color (G24).

Protocol version 1 has neither. An entity names one model by its index in the manifest, and there are no sides.

A module could be an entity of its own. But then a ship would be as many entities as it has modules, each moved by the server every tick and interpolated by the client apart from its hull. A module fixed at its mount never moves relative to its hull, so that would be work that shows nothing.

The plan also gives the game messages of its own (§4), which `GameCore` encodes, for what the engine has no reason to read: names, then the economy's figures. The engine needs an envelope to carry them.

## Decision

**A composite model** is what an entity is drawn as: one or more components, each a model placed in the composite's space.
- A component turns by one of the cube's 24 rotations and moves by whole voxels. A point *p* of its model lies at *t* + *R*(*p*) in the composite.
- The engine gives a component no meaning. The game's composite of a design is its hull and a module at each mount, and an asteroid's is one model.
- The welcome lists the composites, and an entity names its composite by its index there.

**A side** is a number on the entity: 0 for none, and *n* for the welcome's *n*th side. The welcome gives each side a color in sRGB. At most 255 sides.

**The game's payload.** The welcome and every snapshot end with bytes the engine carries without reading. `World::WelcomePayload` gives the welcome's, and `World::Describe` may write the snapshot's.

**The encoding** (amends ADR-015's). The header's layout version is 2, and `Hello` asks for protocol 2.
- **`Welcome`** is version 1's up to and including the manifest. Then:
  - a `u32` composite count;
  - each composite: a `u32` component count, at least 1, then its components, 32 bytes each:
    - a `u16` model index;
    - a `u16` reserved, zero;
    - the translation, three `i32`, each within ±2²⁰, the `.vox` reader's bound, which keeps every voxel center exact in single precision;
    - the rotation, four `f32` in the order x, y, z, w: unit within 10⁻⁴, w ≥ 0, and one of the cube's 24;
  - a `u32` side count, at most 255;
  - each side, 4 bytes: red, green, blue, and a reserved byte, zero;
  - a `u32` payload size, then the payload.
- **`Snapshot`**:
  - `u64` tick, `u64` world tick, `u32` flags;
  - `u32` entity count, `u32` detonation count, `u32` payload size;
  - the entities, 48 bytes each: `u32` id; `u16` composite; `u8` side; `u8` reserved, zero; then the position, rotation and velocity as before;
  - the detonations, 28 bytes each, as before;
  - the payload.
- **`Command`** is unchanged, but for its header's version.

**Its size**, measured by encoding each message with `EncodeMessage`:
- The default sector's welcome grows from 160 to 280 bytes: its three composites of one component each, and the empty sides and payload.
- Its snapshot of 52 entities grows from 2,532 to 2,536 bytes, 76,080 bytes a second at 30 ticks.

**Validation** (amends ADR-015's).
- **The decoder's second argument.** `DecodeMessage` takes `WelcomeCounts`, the composites and sides of the receiver's welcome, in place of its model count.
- **Three new refusals:**
  - `NotCubeRotation`: a component turned by other than one of the cube's 24 rotations;
  - `BadCompositeIndex`: an entity naming a composite the welcome does not have;
  - `BadSide`: an entity naming a side the welcome does not have.
- **`BadModelIndex`** now names a component's model.
- **A welcome is judged in this order:**
  1. its structure: `Truncated`, and `MalformedMessage` for a count beyond its bytes;
  2. `NotFinite`;
  3. `NotUnitRotation`;
  4. `NotCubeRotation`;
  5. `MalformedMessage`: a setting out of its range, a reserved field that is not zero, more than 255 sides, a composite of no component, or a translation beyond ±2²⁰;
  6. `BadName`;
  7. `BadModelIndex`.
- **A snapshot's entities** are judged each in turn:
  1. `NotFinite`;
  2. `NotUnitRotation`;
  3. `MalformedMessage`, for an id of 0 or a reserved byte that is not zero;
  4. `BadCompositeIndex`;
  5. `BadSide`.

  Then come `DuplicateEntity` and the detonations' refusals, as before.

**Where an entity stands** (amends ADR-018's). An entity stands at the middle of its composite's box: `CompositeBounds`, the box around every component's voxels in the composite's space. It is exact, since every component turns by a symmetry of the cube and moves by whole voxels.

Its detonation blasts from `CompositeCentroid`, the mean of every voxel's center. For a composite of one model left where it is, both are the model's own, `OccupiedBounds` and `VoxelCentroid`, to the bit.

Server and client measure a composite through these functions in `NeuronCore/Composite.h`, as they measured a model through `OccupiedBounds` before.

**Placing a composite** (amends ADR-018's placements from entities). Take part *j* of a component turned by *Q* and moved by *T*, of an entity at *P* turned by *R*:
- Its placement turns by *RQ* (`ComposeRotations`).
- It stands at *P* + *R*(*T* + *Q O*ⱼ − middle).

Since *Q* is one of the cube's rotations, every product is exact. So a core that stands aligned, turned by quarter turns at a whole position, stays on the aligned splat with every module it carries (the concept's §5.1).

A component that leaves its model where it is skips the composition. Its placement is then version 1's to the bit, so the space scene draws as before.

**A detonated part:**
- **Where the blast comes from.** The composite's centroid is brought into the part's space: *Q*ᵀ(centroid − *T*) − *O*ⱼ.
- **The inherited velocity** is turned into the part's axes by (*RQ*)ᵀ.
- **Its fragments** are its own model's ([ADR-024](ADR-024-fragmented-detonation.md)), broken about that model's own centroid. So a module shatters about its own middle, not about the composite's.
- **Its heat** and the blast's light scale with the composite's radius ([ADR-025](ADR-025-detonation-light.md)).

**The side's color** (amends ADR-014's palettes, one per model). Entry 16 of every palette is the side's, `SIDE_PALETTE_ENTRY`, as [ADR-027](ADR-027-design-generator.md) keeps it in every design's and module's palette.
- **What the scene holds.** Each model's own palette, then its variant for each side, model after model. Model *m* as side *s* of *S* draws with palette *m*(*S* + 1) + *s* (`SidePaletteIndex`).
- **A side's variant** is the model's palette with entry 16's sRGB replaced by the side's color. Its alpha and its material stay (`SidePalette`).
- **Side 0** draws with the model's own palette.
- **The cost.** A scene of *M* models and *S* sides holds *M*(*S* + 1) palettes of 256 bytes.

**The world** (amends ADR-015's `World`).
- `Composites()` is required.
- `Sides()` and `WelcomePayload()` default to none.
- **The space scene is unchanged.** `GameLogic::Sector` names each model alone, `SingleModelComposites`: composite *i* is model *i*. It has no sides and no payload.

**One snapshot for every session** stands until phase 3 (ADR-015).

**Tests.**
- **`NeuronCoreTests`:**
  - `MessageTests`, 13 tests, 2 of them new:
    - golden bytes for one message of each type, 12, 207, 118 and 16 bytes, from an encoder written in Python's `struct` from the layout above and nothing else;
    - every truncation refused as `Truncated`;
    - every new refusal by its name, in the order above;
    - all 24 of the cube's rotations accepted, as the NVF importer's table spells them.
  - `CompositeTests`, 6, new:
    - a single model's composite is the model, to the bit;
    - a composite's box and centroid against an exact reference over every voxel, for 40 random fits of the cube's rotations;
    - a component's transform;
    - components without voxels;
    - the side's entry and nothing else;
    - the palette index.
- **`NeuronClientTests`:**
  - `SceneModelsTests`, 3 more:
    - every voxel of a hull and a module fitted to it where the composite puts it, exactly for a station and to 10⁻³ for a tilted ship;
    - each placement's palette for each side;
    - a composite's detonation, from its centroid, with each part's own fragments.
  - `VoxelSceneTests`, 1 more: the side palettes, on WARP.
  - `ClientSessionTests` and `SnapshotBufferTests` carry the composites, the sides and the payload.
- **`NeuronServerTests`:** the welcome carries the world's composites, sides and payload.

**Measured.** The suites were built natively by GCC 13.3 for x86-64, against a throwaway stand-in for the test framework:
- `NeuronCoreTests` 228 of 228;
- `NeuronServerTests` 8 of 8;
- `GameLogicTests` 16 of 16;
- `GameCoreTests` 40 of 40;
- `ClientSessionTests`, `SnapshotBufferTests` and `SceneModelsTests` 25 of 25, compiled apart from `NeuronClient`'s Direct3D precompiled header.

All of them passed again under `-O2 -mfma -ffp-contract=fast`, which forces the contraction MSVC may do under `/arch:AVX2`. The GPU suites run in CI only.

## What this forecloses

- **A component that moves within its composite.** A module that turns, such as a turret that tracks, is not a component: it needs its own transform in every snapshot, which is a later protocol.
- **Composites that change during a session.** The welcome fixes them. A design made mid-game (G-M6's designer) needs a message that adds a composite.
- **A side's color anywhere but entry 16,** or more than one color for a side.
- **More than 255 sides.**
- **The engine reading the payload.** It carries the game's bytes and gives them no meaning.
