# ADR-014 — Placements, scene-wide ids and culling

**Status:** accepted, 2026-09-28 · **Lands with:** S-M2 of [`Design/SpaceScene.md`](../SpaceScene.md) (§7, §15, §16) · **Amends:** [ADR-002](ADR-002-voxel-record-and-palette.md)'s record index per drawn voxel, [ADR-006](ADR-006-depth-conventions.md)'s tie rule across placements, and [ADR-013](ADR-013-zero-gravity-detonation.md)'s parameter block, randomness and envelope

## Context

Until S-M2 the renderer drew one model. Each part was an instance, drawn with its origin as a constant. The visibility buffer's first word was a record index, and there was one palette. The explosion posed voxels in the world, hashing each by its record index. Every instance was drawn in every view.

The space scene draws three models many times each, turned any way, each with its own palette (SpaceScene S6–S8, §7). A station or a ship detonates wherever it stands and however it is turned, and its debris carries on the way it was going (§7.7). Two detonations of one model must not throw the same debris, so the event brings a seed (§5.5). With dozens of placements, each view culls what it cannot see, and the camera draws the nearest first (§7.4).

S-M2 builds all of that in the renderer and its twins. The station sample draws through it: one placement for each part of the station, which renders, detonates and is restored as before. The entities that will supply placements are S-M3's and S-M4's.

The design left one question open, and on 2026-09-28 the owner answered it: the inherited velocity slows under the drag with the rest of the motion (SpaceScene §17, question 20).

The figures below were measured in three ways, and each says which:
- **The suite.** `NeuronCoreTests` built natively by GCC 13.3 for x86-64, against a throwaway stand-in for the test framework.
  - At `-O1` without contraction, all 124 tests pass.
  - `-O2 -mfma -ffp-contract=fast` forces the contraction MSVC may do under `/arch:AVX2` (`AGENTS.md` §3). There 123 pass. The one failure is `TracesThePinnedVoxels`, 9 pixels of the three-quarter image, which fails the same way on `main` before this change.
- **The probes.** Throwaway native programs, built the same way, over the station and random models.
- **WARP in CI**, MSVC's Debug|x64 build: the GPU tests.

## Decision

**Words.**
- A *model* is a loaded `.vox`.
- A *part* is one of its `ModelInstance`s.
- A *placement* is one part under a rigid transform, whole or detonated: what one draw draws.
- The *scene* is every model's records and palettes on the GPU. A frame brings its placements.

An entity whose model has several parts is drawn by one placement per part, each with its part's origin folded into its translation (§7.1).

**The transform** (`RigidTransform.h`).
- **Its columns.** A `Rotation` holds the images of the three unit axes. They are the columns of its matrix, and a placed voxel's box takes them as its axes.
- **A voxel's centre.** In its part's space, the voxel of record *r* is the unit cell whose least corner is *r*.xyz. Its centre in the world is *t* + *R*(*r*.xyz + ½).
- **One order of sums.** `TransformPoint` sums *t* + (`axisX` *x* + `axisY` *y* + `axisZ` *z*) in that order, in the twin and in `Placement.hlsli` alike.
- **One call site.** Each twin takes a voxel's centre into the world through one call of `TransformPoint`, whole or detonated. A compiler that fuses a multiply and an add may round two inlined copies differently, and then a detonation at time 0 would not draw the whole placement's boxes.

**Aligned or oriented** (§7.2). A whole placement draws aligned when its rotation is one of the cube's 24 proper symmetries, and oriented otherwise. A detonated placement always draws oriented.
- **The test.** `IsCubeSymmetry` asks for every entry exactly 0 or ±1, one ±1 in each column, and `axisX` × `axisY` = `axisZ` exactly.
- **Nothing snaps.** A rotation a rounding away from a symmetry draws oriented. The suite accepts the 24 rotations, refuses the 24 reflections, and refuses every rotation with one entry a float's last bit away from one.
- **Quaternions give the symmetries exactly.** `RotationOf` scales its products by 2 / |*q*|², not by 2. The float quaternion of each of the 24 rotations then gives the rotation exactly: the suite checks all 24, with contraction and without. Against a double-precision reference, the worst entry of random quaternions' rotations lies 3.63 × 10⁻⁷ away. So a station turned by quarter turns (S18) draws aligned when its rotation arrives as a quaternion, and no snap is needed.
- **Rounding.** An aligned placement's centre is the exact sum, rounded once. It is exact when the translation is whole, as a station's is (§5.1). The suite checks every centre under each of the 24 rotations, at random translations within the world's bound. A rigid placement's centres are within rounding of a double-precision reference, and its boxes turn with it.

**Ids** (§7.3; amends SampleRenderer §7.3 and ADR-002). The visibility buffer's first word is a scene-wide id: the placement's `firstVoxel` plus the record's index within its part.
- **A running sum.** `AssignVoxelIds` gives each placement the number of records the placements before it hold, in the order the caller gives. S-M4's session orders them by entity id and then part, so that ids hold still while the camera moves (§7.3). The station sample gives its parts in file order.
- **Refusals.** `AssignVoxelIds` refuses a frame whose ids would reach `NO_VOXEL`. The renderer refuses, by name, placements:
  - whose records lie beyond the scene's;
  - whose palette the scene lacks;
  - whose ids fall back, overlap or reach `NO_VOXEL`.

  The shaders index the scene's buffers with what a placement names, without a bound, and the binary search relies on rising ids.
- **ADR-002 amended.** Its "every drawn voxel has its own record index, which the visibility buffer reports" now reads: every drawn voxel has its own id. A model that one `.vox` places twice is still stored twice. A model that a scene draws twice is stored once.
- **The station keeps its ids.** The station sample's placement starts at id 0, and its part at record 0, so its ids are its record indices. N-M0's pins hold unchanged. `TracesThroughOnePlacementAsItsGridDoes` finds the same voxel in all 3,941 of its pixels, through the placement and through the grid.

**Finding a voxel.**
- **The search.** `FindPlacement` searches for the last placement whose first id is at most the id. That placement holds the id if it falls within its records; otherwise the result is `NO_PLACEMENT`.
- **The lookup.** `FindVoxel` then gives the record, the part's first record plus the offset, and the palette, the model's.
- **Its cost.** The search takes ⌈log₂ *n*⌉ steps: 6 for the defaults' 52 placements, 10 for a thousand.
- **Where the twins live.** They sit in `Placement.h` and `Placement.hlsli`, beside `PlacedVoxelBox`, not beside `LightPixel` as §7.3 had them. The debug views need them as much as the lighting does, and `LightPixel` takes a palette entry, not an id.

**Detonated placements** (§7.7). A detonated placement carries the detonation's parameters in its part's space, and the time since the event.
- **Into the part's space.** The world's blast origin comes into the part's space through the inverse transform, and the inherited velocity through the inverse rotation. The pose is computed there. It is taken into the world by the same `TransformPoint`, and its axes by the rotation.
- **The hash.** A voxel's hash counts from its part's first record within its model, so every voxel hashes by its record index in its model. That holds whichever placement draws it, and wherever the model's records lie in the scene's buffer. `DebrisDoesNotDependOnTheSceneOrder` holds a model's debris to that.
- **Time 0.** Both twins skip the pose at time 0 and before. Time 0 is exactly the rest (ADR-013), so the skip saves the work and changes nothing.

**The inherited velocity and the seed** (amends ADR-013's parameter block and randomness).

| Parameter | Default | Meaning |
|---|---|---|
| `inheritedVelocity` | (0, 0, 0) | Added to every voxel's launch: the entity's velocity when it detonated |
| `seed` | 0 | Mixed into every voxel's hash, so that two detonations of one model differ |

- **The drag slows it,** as the owner decided on 2026-09-28. A voxel's launch is the jittered one away from the blast origin, plus the inherited velocity, and the drag slows the sum.
  - So the debris carries on the way the entity was going, and comes to rest `inheritedVelocity` / drag further on. At the default drag, that is 60 voxels further for a frigate at its cruise of 60 units a second, and 20 for a capital ship.
  - The spins are unchanged.
  - `InheritedVelocityCarriesEveryVoxel` holds every voxel to that offset, within rounding, and its spins to what they were, at five times from 0 to the end.
- **The seed.** Voxel *i*'s *k*th random number is `PcgHash`(8*i* + *k* + seed × 0x9E3779B9), modulo 2³². The constant is ⌊2³² / φ⌋, Knuth's multiplicative hashing constant, so successive seeds move the hash's inputs far apart.
  - Seed 0 gives ADR-013's numbers exactly.
  - `SeedsGiveDifferentDebris` finds every one of 64 voxels posed differently under seed 1 than under seed 0, and the same under seed 1 twice.
- **The envelope** (amends ADR-013's). The inherited velocity moves every voxel alike, so it moves the envelope.
  - **Its centre** drifts from the blast origin by `inheritedVelocity` / drag in all, as far along as the motion has gone. Its radius is unchanged.
  - **The stop time** adds the drift's length to what is left to travel.
  - **`EnvelopeSphereAt`** gives the sphere at a time, which culling uses.
  - **`EnvelopeSphere`** is the sphere around the whole drift. The shadow view is fitted around it, and F frames it.
  - **The test.** `RandomExplosionsStayInsideTheirEnvelope` now draws parameter blocks with inherited velocities of up to 300 voxels a second, any way, and random seeds. Its 1,152 voxels under 24 blocks stay inside the sphere at their time and inside the sphere around the drift, and they stop by the stop time.
- **ADR-013's figures stand.** With no inherited velocity and seed 0, the suite measures the station's envelope as before: 420.87 about the blast origin, a stop at 11.61 s, and 456.16 of the shadow map's 512.

**On the GPU.**
- **Records** are one `StructuredBuffer<uint>`, model after model. A model's records are stored once, however many placements draw them.
- **Palettes** are a `StructuredBuffer<PaletteConstants>`, one 256-byte palette per model, in place of the one constant buffer.
- **`PlacementConstants`**, 64 bytes (R16): `axisX` at 0, `firstRecord` 12, `axisY` 16, `recordCount` 28, `axisZ` 32, `firstVoxel` 44, `translation` 48, `paletteIndex` 60. The frame's placements are a structured buffer in its upload ring.
- **The ring grows.** It starts at 64 KB: room for the three aligned pieces of constants and about a thousand placements. A frame that needs more replaces its slot's ring, once the GPU has finished with it. The new ring is twice the size, or the size the frame needs if that is larger.
- **`ExplosionConstants`**, 60 bytes where it was 40. `inheritedVelocity` joins at 16, `seed` at 52 and `hashBase` at 56, and the rest keep their order.
  - Every oriented draw binds one as a root constant buffer.
  - A detonated placement binds its own.
  - Every whole oriented placement binds one shared, zeroed block, whose time 0 skips the pose.
- **The splat's root signature** is shared by both permutations:
  - the view's constants;
  - the draw's placement index, one root constant;
  - the explosion's constants, for oriented draws;
  - the records and the placements, as root shader resource views;
  - the overdraw table, in that variant.

  `SplatPass::Record` changes pipeline state only where the permutation changes from one draw to the next.
- **The vertex shader** reads its placement at the root constant's index, and builds the box through `PlacedVoxelBox`, the twin of the C++. It writes `firstVoxel` plus the voxel's index as the id.
- **The lighting** finds each pixel's record and palette through `FindVoxel`. The word of `LightingConstants` that ADR-013 named `padding` becomes `placementCount`, so the layout keeps its size and offsets. The debug views take the view and the count as two root constants.
- **The layout echo** reads the new and changed layouts:
  - `PlacementConstants`, two of them, so that the stride is checked;
  - the new `ExplosionConstants`;
  - `LightingConstants`;
  - two palettes of the structured buffer.

**Culling and order** (§7.4; amends SampleRenderer §4.2, item 10, and §9.1). The host culls. Nothing new runs on the GPU, so nothing needs a twin.
- **The sphere.** A whole placement's is the sphere through the corners of its part's box, taken into the world. A detonated one's is the envelope's sphere at its time.
- **The test.** The camera's view keeps a sphere unless it lies wholly behind the near plane or beyond one of the four sides. Its far plane is at infinity. A shadow view tests the six faces of its box.
  - Each plane is tested on its own, so a sphere just outside a corner is kept.
  - A sphere is culled only when it clears a plane by more than `CULL_MARGIN`, one voxel, so that rounding never culls one that touches.
  - `CullingTests` keeps spheres that touch each plane of 256 random frustums and boxes, and culls them at twice the margin.
- **The order.** The camera draws what it keeps nearest first, by the distance from the eye to the sphere's nearest point. Two as near keep their order. The shadow views draw in placement order, since depth alone needs no other.
- **The counts.** Each frame counts the placements the camera and the sun drew and culled.
  - The figures add a line: "placements: view *N* drawn, *M* culled; sun …".
  - The bench's CSV gains four columns, `viewDrawn`, `viewCulled`, `shadowDrawn` and `shadowCulled`, with a row each in its summary.

**Ties** (amends ADR-006).
- **Within a placement,** the lower record keeps a tie, since a draw covers its records in order. That is ADR-006's rule, and the scene tracer's.
- **Across placements,** the placement drawn first keeps it: for the camera the nearer sphere, and for the sun the earlier placement. The scene tracer gives every tie to the lower id, so the two agree wherever the draw order is the id order.
- **When it arises.** Stations never overlap (§5.2). Ships may fly through one another (§2), but a tie there needs two voxels at exactly one depth, and either answer is a voxel the ray meets at that depth. The tests keep whole placements apart, so no tie across placements arises in them.

**The world's bound** (S12, §7.5).
- **What it buys.** Below 16,384 = 2¹⁴ in magnitude, a float's spacing is at most 2⁻¹⁰, a quarter of the 1/256 of a voxel within which the GPU tests let rounding decide.
- **Who enforces it.** The renderer does not check the bound: it draws whatever placements it is given. The world's layout refuses a world that does not fit (§5.2), which is S-M3's.
- **How it is tested.** `PlacementTests` places parts at random translations up to 16,384 in each coordinate. On WARP, `PlacementSplatTests` draws its scene from 10,000 units back.

**The scene tracer** (`SceneTracer.h`, §15).
- **Its grids.** It holds one `VoxelGrid` for each distinct part the placements draw, shared by every placement of it.
- **A whole placement.** The tracer takes the ray into the part's space in double precision and walks the grid there. It tests the cells the ray passes near with the box the GPU draws, in the world, through the same permutation.
- **A detonated placement.** It tests the posed boxes one by one.
- **`VoxelGrid`'s walk** became `GridWalk`, which `VoxelGrid::Trace` now uses too. A probe traced 1,000,000 rays through the station and through 30 random models of up to three parts, before the change and after it. It found `Trace` unchanged, bit for bit: 299,596 hits.
- **Against brute force** over every placed box, on 12 seeded random scenes of aligned, rigid and detonated placements, the tracer agrees on all 6,000 rays, 3,216 of which hit.

**The station sample** (`GameLib`).
- **Its placements.** The scene holds one whole placement per part, unturned, at the part's origin, with ids in file order.
- **Its detonation.** From the detonation on, each placement carries the world's detonation, with the blast origin taken into the part's space. There is no inherited velocity, and the seed is 0.
- **Its envelope.** The shadow view is fitted around the sphere around every part's `EnvelopeSphere`, and F frames that sphere.
- **The renderer takes the frame's placements,** so `RendererDesc` loses the explosion, and `FrameSettings` its time.
- **The image and the ids** are what they were.
- **The debris** is ADR-013's to rounding. The pose is now computed about the part's origin, and the probe compared it with the world-space pose over all 225,048 of the station's voxels. From 0.25 s to 30 s, no centre moved by more than 6.1 × 10⁻⁵ of a voxel in any coordinate, two units in the last place of a coordinate of a few hundred. No axis changed at all.

**Tests.**
- **`NeuronCoreTests`:**
  - `RigidTransformTests`: the symmetries and their reflections, every near miss, the quaternions of the 24 rotations, and `RotationOf` and the transforms against double-precision references;
  - `PlacementTests`:
    - parts placed from their models;
    - aligned centres rounded once, and rigid ones within rounding;
    - detonated at time 0 drawing the whole boxes;
    - posed boxes composed with the placement against a double-precision reference;
    - debris that does not depend on where a model's records lie;
    - ids as a running sum, and the refusal at `NO_VOXEL`;
    - every id found, among empty placements too, and none beyond the last;
    - spheres that hold everything they place;
  - `CullingTests`;
  - `SceneTracerTests`;
  - `ExplosionTests`' seed, inherited velocity and drifting envelope;
  - `MilitaryStationTests`' trace through one placement.
- **`NeuronClientTests`, on WARP:**
  - **`PlacementSplatTests`,** new:
    - the three models, aligned and rigid, against the scene tracer from the origin, grazing a turned capital ship across the near plane, and from 10,000 units back, and in the sun's map;
    - symmetric placements drawing the same through either permutation;
    - a detonation at time 0 drawing the whole image;
    - the measurement variants on turned and detonated placements;
    - the lighting through each model's palette, in one image where the station's white glows and the capital ship's does not.
  - **The splat, lighting, debug view and measurement suites,** ported to placements. The explosion's tests now detonate placements, and add turned debris against the twin.
  - **The layout echo.**
- **The edge rule's limits.** The limits of mismatches on an edge are set from CI's first measured run (§15).

## What this forecloses

- An id other than the placement's first id plus the record's index within its part, and a frame whose ids reach `NO_VOXEL`.
- A placement that scales or shears. S-M9's coarse models need a uniform scale of two, and bring it with their ADR.
- Snapping a rotation near a symmetry to it: only an exact symmetry draws aligned.
- Hashing a detonated voxel by anything but its record index within its model and the seed. Seed 0 stays ADR-013's numbers.
- An inherited velocity the drag does not slow.
- Culling or ordering on the GPU, or any camera order but nearest first, without a change to this ADR.
