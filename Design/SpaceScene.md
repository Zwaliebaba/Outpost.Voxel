# Outpost.Voxel — Space Scene Design

**Status:** draft for the owner's acceptance. The owner answered six questions on 2026-09-28 (§17, 1–6); the rest of §17 is open · **Date:** 2026-09-28
**Builds on:** [`SampleRenderer.md`](SampleRenderer.md), the renderer; [`NeuronVoxelFormat.md`](NeuronVoxelFormat.md) §12, the move to Direct3D's axes (N-M0); [ADR-003](ADR/ADR-003-engine-and-game-layout.md), the client/server layout · **Assets:** `GameData/MilitaryStation.vox`, `CapitalShip.vox`, `Frigate.vox`

This document says what the space scene is and in what order it is built; `AGENTS.md` says how the code is written. The engineering decisions below land as ADRs in the commits that implement them (§18). §3 lists what this retires from `SampleRenderer.md` and what it amends there.

## 1. Summary

The single station on a floor gives way to a scene in space: several stations, ships flying routes around and between them, and a sky of stars across a galactic band. The server owns that world. `GameLogic` simulates it at a fixed tick on a thread of its own, and nothing it knows reaches the client except as bytes: messages that `NeuronCore` encodes and a `Transport` carries, which inside one process is a `LoopbackTransport`. The client decodes snapshots, interpolates them a fixed delay behind the newest, and draws what it gets.

The world is built from a seed and a small parameter block, so the same arguments build the same world and simulate it the same way, tick for tick. `--bench` steps the simulation in lockstep with its own frames, so every run draws the same frames. That is what makes the scene a test bed for performance: the workload is set on the command line, and whatever it is, it repeats.

The renderer keeps its technique, its depth conventions and its twins. What it gains is placement: one model drawn many times, each time under a rigid transform and with its own palette. Stations draw through the aligned splat and ships through the oriented one. Shadows come from cascades fitted to the camera, and the sky is drawn at the far plane of the reversed-Z view.

| # | Decision | Source |
|---|---|---|
| S1 | The space scene replaces the station sample. The ground plane, the explosion with its clock and keys, and the explosion's timeline in `--bench` are retired, and D2 and D3 with them. | Owner, 2026-09-28 |
| S2 | N-M0, the move to Direct3D's axes, lands before any code of the space scene. The retirement lands before N-M0, so that N-M0 converts nothing it would then delete. | Owner, 2026-09-28; §3.2 |
| S3 | The server is authoritative and runs on a thread of its own. Only encoded messages cross between server and client, through a `Transport`; within one process it is a `LoopbackTransport`. | Owner, 2026-09-28 |
| S4 | The sky is the view from inside a galaxy: a band across the whole sky with a brighter core, star clouds and dust lanes, and a star field whose density follows the band. | Owner, 2026-09-28 |
| S5 | The view keeps reversed-Z with an infinite far plane (D8, ADR-006), and every new pass uses it through ADR-006's helpers. The sky is drawn at the far plane and tested against it. | Owner, 2026-09-28; §9 |
| S6 | Models are drawn through placements: a rigid transform, the model's records and the model's palette. A placement whose rotation is one of the cube's 24 symmetries draws with the aligned splat; any other rotation draws with the oriented one. | §7.2 |
| S7 | The first word of the visibility buffer becomes a scene-wide voxel id: the placement's base plus the record's index within its part. | §7.3 |
| S8 | Each model keeps its own 16-entry palette. | §4, measured; §7.1 |
| S9 | Shadows come from cascaded orthographic maps, fitted to the camera every frame and moved in whole texels. | §10 |
| S10 | Stars are point sources: sprites whose Gaussian point-spread function is integrated over each pixel, in HDR. The galaxy and the sun are functions of direction, evaluated per background pixel. There is no cube map. | §11 |
| S11 | The world comes from a seed and a parameter block. The same arguments give the same world and the same simulation, tick for tick, within one build. | §5.2 |
| S12 | The world lies within ±16,384 units of the origin, where a coordinate's spacing is at most 2⁻¹⁰ of a voxel. Camera-relative rendering waits until the world outgrows that. | §7.5 |
| S13 | Models reach the client by name and content hash, in the server's welcome. No game-specific type crosses the wire, so no shared game library is created yet (ADR-003). | §6.2 |

## 2. Scope

**In scope:** the retirement (§3); placements in the renderer (§7); the world, its layout and its flight (§5); the messages, the transport, the server's host and the client's session (§6); reversed-Z carried through the new passes (§9); cascaded shadows (§10); the sky (§11); lighting in space (§12); the camera, keys, figures and command line (§13); `--bench` on the space scene (§14); and the tests for all of it (§15).

**Out of scope, and why:**

- **Gameplay:** weapons, damage, docking, collisions between ships, a ship under a player's control. The flight model flies routes. Ships pass through one another, as the explosion's pieces did (D3); collision is a design of its own.
- **A network transport and `Server.exe`.** The boundary built here is the one they need, and §6.1 writes down what a UDP transport must add. Sockets need Windows headers in `NeuronServer`, and ADR-003 left that decision to the day it is needed.
- **Loading `.nvf`.** Models load from `.vox` by name (§6.2). The loader changes when NVF's follow-up lands, and nothing else does.
- **Less work for distant voxels:** the paper's stochastic pruning (Listing 3, which SampleRenderer §4.2 item 9 left out) or a level of detail. This scene is what will show whether either pays (§14), and each is an ADR on measured numbers.
- **Stations that turn.** A station stays where it is placed, turned through one of the cube's symmetries, which keeps its 225,048 voxels on the aligned splat (§7.2). A spinning station moves to the oriented splat; that is a parameter away once the bench has said what it costs.
- **Anti-aliasing, bloom and HDR output**, as in SampleRenderer §2 and D4; §17 asks how D4 reads against the stars' halos.

## 3. What the owner's answers change

### 3.1 The retirement (S1)

**The explosion goes whole:** its twin (`NeuronCore/Explosion.h` and `.cpp`) and `ExplosionTests`; its HLSL (`Explosion.hlsli`, `ExplosionConstants.hlsli`); `NeuronClient/ExplosionConstants.h` and `.cpp` and their layout echo; the oriented splat's entry points and pipelines; `ExplosionSplatTests`; the oriented cases of `MeasurementTests`; the six `Explosion…` tests of `MilitaryStationTests`; the clock; and the keys E, R, Space, + and −. The oriented ray-box twin and its CPU tests stay. The oriented permutation returns in S-M2 with a rigid pose and tests of its own (§7.2). Between the two milestones nothing draws an oriented box, so nothing untested runs.

**The ground goes from the lighting:** `LightPixel`'s ground branch in both twins, `groundVisible`, the G key, and the reading of `_setting _ground`. The hemisphere ambient keeps its lower color, which the ground's color also supplied, until S-M5 gives it a new source (§12). The shadow view is fitted to the station's box, no longer grown down to a floor.

**`--bench` keeps its machinery:** the warm-up, both depth variants of every frame, the coverage count, the CSV and the summary. It loses the explosion's timeline, and until S-M7 it runs the intact station under a circling camera.

**The documents.** In `SampleRenderer.md`, D2 and D3 are marked retired, §12 gives way to a pointer, §11 loses the ground, §13 loses the explosion's keys, the G key and the bench's phases, §14 loses the pose and explosion tests, and §15 notes that M4's deliverables are retired. ADR-009 is superseded rather than edited, and ADR-008's ground paragraph is amended.

### 3.2 Why the retirement comes before N-M0

N-M0's table (NeuronVoxelFormat.md §12.2) converts the explosion (gravity, contacts, lift, envelope and their tests) and the ground (the plane, its normal, the shadow box grown down to it). Under S1 both would be converted and then deleted. Retiring them first makes N-M0 smaller, and the commit that retires them strikes those rows from §12.2. N-M0's own checks, the pinned tracer images and the mirror test, depend on neither.

The alternative is N-M0 as accepted, with the explosion retired only when the rigid placements take over the oriented splat in S-M2. That keeps the oriented permutation drawn and tested without a gap, at the price of converting code that is deleted one milestone later. This design takes the first order; §17 asks.

### 3.3 A baseline first

M5 is done when a measured performance note exists, and none is in `Design/`. The station bench is the only measurement of the renderer as it stands, and S-M1 takes its explosion phases away. So S-M0 asks for one run of `--bench` on the owner's hardware, committed as M5's note, before anything is retired. S-M7's preset of one station and no ships (§14) then has something to be compared with.

## 4. The assets, measured

Measured on 2026-09-28 by walking the three files' chunks with a throwaway script, as SampleRenderer §3 was measured. Axes are MagicaVoxel's, as the files store them.

| Asset | `SIZE` | Voxels | Occupied box | Radius of the sphere around it |
|---|---|---|---|---|
| `MilitaryStation.vox` | 207 × 228 × 255 | 225,048 | 205 × 227 × 255 | 199.1 |
| `CapitalShip.vox` | 45 × 77 × 21 | 10,747 | 45 × 77 × 21 | 45.8 |
| `Frigate.vox` | 33 × 27 × 12 | 1,181 | 33 × 27 × 12 | 22.1 |

The frigate is 27 deep, not the 26 of NeuronVoxelFormat.md §4.5. MagicaVoxel stored 21 of its voxels at y = 255 in a model 26 deep, and the repair merged in 786afeb grew the model by one; the NVF table predates that merge.

The three files carry the same sixteen colors, the CGA/EGA palette in its usual order, but not the same materials. Every emissive entry is `_emit` 0.6, `_flux` 2.

| Entry | Color | Station | Capital ship | Frigate |
|---|---|---|---|---|
| 10 | light blue | emissive, 8,025 voxels | unused | unused |
| 12 | light cyan | unused | emissive, 12 | emissive, 12 |
| 15 | yellow | emissive, 773 | emissive, 120 | emissive, 3 |
| 16 | white | emissive, 3,785 | **diffuse**, 148 | emissive, 23 |

One palette for all three would light the capital ship's 148 white voxels, or put out the station's 3,785 (S8).

**Facing.** Both ships face MagicaVoxel's +Y. All 120 of the capital ship's yellow emissive voxels are exposed on its −Y face, its engines, and its hull narrows to a point at +Y. The frigate's front is the end with the two pins on its sides (the owner, 2026-09-28). Read here, those are the two one-voxel prongs that flank its centerline at y = 25–26, the +Y end, where its two side pylons also point; the pylons end in light-cyan lights at y = 22. After N-M0, MagicaVoxel's +Y is NVF's forward, +Z, and its +Z is NVF's up, +Y (N9). So the flight model takes every model's forward as +Z in model space and needs no per-model facing. The owner confirms both ships by eye in S-M4.

## 5. The world

### 5.1 Entities

An entity has four things: an id, never reused within a session; a model, by its index in the welcome's manifest (§6.2); a position; and a rotation, a unit quaternion from model space to world space. The position is where the center of the model's occupied box lies, so a ship turns about its middle and not about the corner where the `.vox` translation leaves its origin. Stations and ships are both entities. The world knows which is which; the protocol does not.

A station stands still. Its position is whole, and its rotation is one of the cube's 24 proper symmetries, so every one of its voxel centers is exact, as the intact station's are today (§7.2).

A ship carries what its flight needs (§5.3): its speed, its route and how far along it is, its bank, and a wingman's leader. None of that is sent. The client sees transforms and velocities.

### 5.2 The layout, from a seed

The parameters, with their defaults: the seed (1); stations (4); frigates (40); capital ships (8); and the least distance between two stations' centers (1,000 units). All randomness is `PcgHash` of the seed and a stream number, the scheme ADR-009 used per voxel, used here per world.

**Stations** are placed by seeded rejection sampling in a flattened region that grows with their number, each at least the spacing from every other. Each is turned through one of the 24 symmetries (§17 asks whether all 24 or only the four about the vertical).

**Ships fly in flights:** frigates in flights of two to four, a leader and its wingmen; capital ships alone. Each flight gets a closed route through two to four stations. At each station the route orbits it: a circle about the station's center, in a plane tilted up to 45° from the horizontal, one or two laps, at between 1.3 and 2 times the keep-out radius. That radius is the station's sphere (199.1), plus the ship's, plus a margin of 50. Between stations the route is a straight transit from one orbit's exit to the next orbit's entry. A transit that would cross another station's keep-out sphere gets a waypoint pushed outside it. At the default spacing no orbit comes near another station: the widest one, 590 units about its center, stays 410 units from the next station's center, outside its keep-out radius.

**The defaults' workload, by arithmetic:** 4 × 225,048 + 8 × 10,747 + 40 × 1,181 = 1,033,408 voxels in 52 placements. A view that sees all of them runs 4,133,632 vertex-shader invocations, 4.6 times the station's. The ships are 13 % of the voxels. So the default scene is a station workload with ships moving through it; a test of per-object cost (draws, culling, snapshot size) raises the ship counts.

A parameter block whose layout would not fit inside the world's bound (S12) is refused by name before anything runs.

Because placements share their model's records (§7.1), the paper's 53 million voxels (its Figure 1) would be 236 stations: 236 placements, not 212 MB of records. At the default spacing they fit inside the world's bound on a 16 × 16 grid. The scene can therefore reach the regime SampleRenderer §2 said this sample could not speak to. Whether that is worth measuring is the owner's call; it is not a goal here.

### 5.3 Flight

Flight is kinematic, one step per tick. A ship has a forward, an up and a speed. Each tick it aims at the point of its route that lies a look-ahead ahead of it (pure pursuit, looking 1.5 s of travel ahead). It turns its forward toward that point by at most its turn rate, changes speed toward its cruise speed by at most its acceleration, and moves. It banks into the turn: the bank the turn's lateral acceleration calls for, limited in angle and in rate, about its forward, from a reference up the route supplies. The reference is the orbit's normal while orbiting and the world's up in transit, so a ship leans into an orbit and levels out between stations. A wingman steers for its slot in its leader's frame and matches its leader's speed.

| Class | Cruise | Acceleration | Turn rate | Bank limit |
|---|---|---|---|---|
| Frigate | 60 units/s | 30 units/s² | 45°/s | 45° |
| Capital ship | 20 units/s | 5 units/s² | 8°/s | 15° |

These are defaults, tuned by eye in S-M4 and recorded in the world's ADR. No ship enters a keep-out sphere, and a test holds every ship to that over ten simulated minutes (§15).

### 5.4 The tick

The world runs 30 ticks a second by default. Its time is the tick number times the period, exactly; wall-clock time never enters the simulation (§6.3). Every tick produces one snapshot.

## 6. Client and server

### 6.1 Where it lives, and what crosses

| Project | Gains |
|---|---|
| `NeuronCore` | The messages, their encoding and their validation (§6.2). `Transport`, an abstract pipe of whole messages, and `LoopbackTransport`, two queues between two ends. Quaternions and rigid transforms. The standard library only, as before. |
| `NeuronServer` | `ServerHost`: sessions over transports, the handshake, one snapshot per tick to every session, and commands. It runs the tick on a thread of its own, or one step at a time for a caller (§6.3). It simulates nothing: it asks a `World`, an abstract class it defines, to advance a tick and to list its entities. |
| `GameLogic` | `SpaceWorld`, the `World` of §5: the layout, the flight, and the manifest of the models it places. It loads those models itself to measure their boxes. |
| `NeuronClient` | `ClientSession`: the handshake, then snapshots into a `SnapshotBuffer`, which says where every entity is at a given time (§6.4). The renderer's placements (§7), cascades (§10) and sky (§11). |
| `GameLib` | The game's client: the camera and its targets, the keys, the figures, the placements made from the buffer's transforms, and the bench's timeline (§13, §14). |
| `Outpost` | The command line. It creates the `LoopbackTransport` pair, starts the `ServerHost` and its `SpaceWorld` on their thread, and runs the client. It is the one project that sees both sides, and it hands each side only its own end of the transport. |

`Transport` is message-oriented: `Send` takes one message's bytes, and `Receive` returns the next whole message or nothing. The loopback is reliable and ordered, and it copies bytes, so no object is ever shared across the boundary. A UDP transport will be neither reliable nor ordered. Snapshots are whole states and tolerate loss, but the handshake and commands need a reliable channel, and the ADR that adds UDP adds that channel.

Splitting off `Server.exe` then touches `Outpost` and the transport alone. The server's executable builds a `ServerHost` and a `SpaceWorld` over a listening transport, the client builds its `ClientSession` over a connecting one, and nothing between them changes.

### 6.2 Messages

| Message | Direction | Content |
|---|---|---|
| `Hello` | client → server | the protocol version |
| `Welcome` | server → client | the protocol version; the tick rate and the current tick; the world's settings (the sun's direction, radiance and angular radius, the ambient, the sky's seed, and the galactic plane's orientation); and the manifest: for each model, its name and a 64-bit hash of its file |
| `Snapshot` | server → client | the tick and whether the world is paused; for each entity, its id, model index, flags, position, rotation and velocity |
| `Command` | client → server | pause or resume |

Every message is little-endian and starts with a header giving its type, its version and its size. An entity's record is a fixed 48 bytes, like NVF's records, and its rotation is stored as x, y, z, w with w ≥ 0, as NVF's hardpoints store theirs. A snapshot of the defaults' 52 entities is about 2.5 KB, about 76 KB a second at 30 ticks.

A model's name is its file's stem, letters and digits; the client adds its loader's extension, which is `.vox` today and `.nvf` after NVF's follow-up. The client loads every model the manifest names before it draws anything. It refuses, by name, a model it cannot load or whose hash differs from its own file's. Server and client read the same files today; once they no longer do, a mismatch is exactly the failure this catches.

`DecodeMessage` returns `std::expected<Message, ProtocolError>` and refuses by name, as the readers do: `Truncated`, `UnknownMessage`, `UnsupportedVersion`, `MalformedMessage` (a size or count that disagrees with the content), `BadName`, `NotFinite`, `NotUnitRotation` (NVF's tolerance of 10⁻⁴, and w ≥ 0), `DuplicateEntity` and `BadModelIndex`.

### 6.3 Threads and time

The server's thread runs `ServerHost` on a `std::jthread` at the tick rate. It wakes, runs every tick that is due (at most a few, so that a stall never turns into a spiral), sends each tick's snapshot, and sleeps until the next tick is due. A tick's time is its number times the period, so a late wake-up delays a snapshot's arrival but never changes anything in it. `NeuronServer` has no Windows header (ADR-003), so the thread sleeps with `std::this_thread::sleep_until`. On Windows that can wake some milliseconds late, which the interpolation delay absorbs (§6.4).

`--bench` and the tests run no thread. They call the host's `Step` themselves, one tick at a time, which is what makes a bench run repeatable (§14).

The pause command freezes the world, not the clock. Ticks and snapshots go on, each state the same as the last, so the client's time never jumps and a resume needs no resynchronization.

### 6.4 The client's time

The client relates its clock to the server's by an offset estimated from arrivals. For each snapshot the offset is its arrival time minus its tick's time. The client keeps the smallest over the last second, which filters out delivery delay. It renders at the server time that offset gives, less an interpolation delay of three ticks, 100 ms by default.

`SnapshotBuffer` brackets the render time with two snapshots and interpolates every entity present in both. Positions follow a cubic Hermite curve through the two positions and velocities, so orbits stay round between ticks. Rotations are normalized linear interpolation along the shorter arc. An entity present only in the later snapshot has appeared; one present only in the earlier has gone. Past the newest snapshot, the buffer holds the newest.

## 7. Placements in the renderer

### 7.1 Models and palettes

When the welcome arrives, the client loads each named model. The renderer puts all their records in one static buffer, model after model, and their palettes in an array, one `PaletteConstants` each, as today. A model's records are stored once however many placements draw them: the defaults draw 1,033,408 voxels from 236,976 records, 947,904 bytes.

A `.vox` model can hold several placed models of its own (`ModelInstance`), and each is a part with its own origin. The three assets have one part each. A placement draws every part of its model, with the part's origin folded into its transform, which is the shape NVF's tree of parts will need (N2).

### 7.2 Rigid transforms, aligned and oriented

A placement is a model part under a rigid transform, a rotation *R* and a translation *t*. The voxel with record *r* has its center at *t* + *R*(*r*.xyz + ½). The CPU folds the entity's position, the model's center and the part's origin into *t*, and *R*'s columns are the box's axes. The vertex shader reads the placement from the frame's buffer, at an index the draw passes as a root constant.

When *R* is one of the cube's 24 proper symmetries (every entry 0 or ±1, determinant +1), every product in that sum is exact, the box it places is axis-aligned, and the placement draws with the aligned splat. Stations always do. Any other rotation draws with the oriented splat, whose pixel shader intersects an oriented box (Listing 5 with `ORIENTED`) exactly as the explosion's did; only the pose changes, from the explosion's to the placement's. A ship that happens to fly at an axis-aligned rotation draws aligned for that frame, and a test holds the two permutations to the same image there (§15), as SampleRenderer §12's test at *t* = 0 did.

The twin is `PlacedVoxelBox`, beside `VoxelBox` in `NeuronCore`. The scene tracer (§15) uses it to build the same boxes the GPU draws.

### 7.3 Voxel ids (amends SampleRenderer §7.3)

Today the visibility buffer's first word is the voxel's record index, which is unique because each record is drawn once. Shared records make it ambiguous, so it becomes a scene-wide id: the placement's base plus the record's index within its part. The bases are a running sum over the frame's placements in a stable order (by entity id, then part), not in draw order, so a voxel's id, and the voxel-index view's colors, hold still while the camera moves. `NO_VOXEL` stays 0xFFFFFFFF, and the host refuses a frame whose voxels would reach it.

The lighting pass and the debug views find a pixel's placement by binary search over the bases: six steps for the defaults' 52 placements, ten for a thousand. The placement then gives the record (its part's first record plus the offset) and the palette (its model's). The twin is a function beside `LightPixel`. Packing a placement index into the id's high bits would save the search, at the price of a cap on both the placements and the voxels per model. Widening the visibility buffer to four words would double its 16.6 MB at 1080p.

ADR-006's tie rule holds within a placement: its records draw in order, and the lower one keeps a tie. Across placements the first one drawn keeps it (§7.4). The world makes that moot by never letting two placements overlap.

### 7.4 Draws, culling and order (amends SampleRenderer §4.2, item 10, and §9.1)

Each view, the camera's and each cascade's, culls placements on the CPU: the sphere around each part, tested against the view's frustum or the cascade's box. The survivors draw one `DrawIndexedInstanced` each, as each instance does today. The camera's view draws front to back, nearest sphere first, so that conservative depth lets the most pixels be rejected early (SampleRenderer §9.3). None of this runs on the GPU, so none of it needs a twin; its tests are CPU tests (§15). Every view counts what it drew and what it culled, and both the figures and the bench report the counts.

The frame's placements go through its upload ring, which grows with them at 64 bytes a placement. A view's draw list is just indices into them.

### 7.5 Precision (S12)

A voxel's box is intersected in world coordinates. Within ±16,384 units, a coordinate's spacing is at most 2⁻¹⁰ of a voxel, a quarter of the 1/256-voxel edge tolerance the WARP tests allow. The layout refuses a world that does not fit (§5.2). Outgrowing the bound means camera-relative placement, which is an ADR of its own.

## 8. The frame

| Pass | Kind | Reads | Writes | Depth |
|---|---|---|---|---|
| Shadow splat, once per cascade | Graphics | Records, placements | That cascade's slice | Standard Z, `LESS` |
| View splat | Graphics | Records, placements | Depth, visibility | Reversed-Z, `GREATER` |
| Lighting | Compute | Depth, visibility, cascades, records, placements, palettes | HDR color | — |
| Sky | Graphics: one full-screen triangle, then one quad per star | View depth (read-only), star records | HDR color | Reversed-Z far plane, `EQUAL` |
| Tone map | Graphics, as today | HDR color | Back buffer | — |
| Canvas | Graphics, as today | Quads, glyph atlas | Back buffer | — |

## 9. Depth: reversed-Z, kept and carried (S5)

The view has used reversed-Z with an infinite far plane since M2 (D8, ADR-006). Depth is *n* / view depth with *n* = 0.1, the buffer is `D32_FLOAT` cleared to 0, the test is `GREATER`, and the splat writes conservative depth through `SV_DepthLessEqual`. Nothing here replaces it. The space scene is where it earns its keep.

**Why it matters more here.** By arithmetic: a float's spacing is at most 2⁻²³ of its value, and reversed-Z's depth is inversely proportional to distance. Two surfaces are therefore told apart once their distances differ by about one part in eight million, about a thousandth of a voxel at 10,000 units. A conventional depth buffer with the same near plane and a far plane at 20,000 spends its precision near the camera: at 10,000 units, adjacent depth values lie about 60 units apart, twice a frigate's length, and a ship passing in front of a distant station would flicker through it. The infinite far plane also means nothing is ever clipped for being far away, which a scene with a sky at infinity needs.

**What the new passes do with it:**

- **Rigid placements keep conservative depth's promise.** The rectangle sits at the nearest point of the voxel's sphere or corners, as the aligned one does (SampleRenderer §9.2, step 4), and the pixel shader writes min(*n* / *t*, `SV_Position.z`). Only the box changes.
- **The sky is drawn at the far plane.** A direction at infinity has depth exactly `PERSPECTIVE_FAR_DEPTH`, the value the view is cleared to, so "no voxel here" is exactly "depth equals the far plane": no epsilon, and no z = w trick. The sky's full-screen triangle and its stars sit at that depth, tested `EQUAL` against the depth buffer bound read-only. The comparison lives beside ADR-006's helpers in `PerspectiveView.h`, and a `static_assert` ties the pipeline's test to it, as the splat's is tied. The triangle is the one `FullScreen.hlsli` already places at the far plane.
- **The lighting keeps rebuilding position from depth**, as the ray at *n* / depth (SampleRenderer §9.3). At 10,000 units that position is good to about a thousandth of a voxel, well inside the smallest cascade's normal offset.
- **A WARP test pins it.** It draws placements 10,000 units from the camera, less than a voxel apart, and compares every pixel with the tracer (§15).

**The cascades stay on standard Z**, as ADR-006 has the shadow map. Orthographic depth is linear in distance, so reversing it only moves where a float's precision is finest. At the far end of an 8,000-unit range, standard Z still spaces depths 0.0005 units apart, two thousand to a voxel, and shadow acne comes from texel size and normal offset, not from depth precision. Reversing the maps would buy one convention for both kinds of depth. ADR-006 chose to name the two instead, each with its helpers; §17 asks whether to keep that choice.

## 10. Shadows: cascades (amends SampleRenderer §10)

SampleRenderer §10's map holds 1,024 units, four texels to a voxel, fitted once around the station and never moved. The default layout is about three times as wide. One 4096² map over it gives about one texel per voxel, and over the world's bound, 32,768 units across, an eighth of one.

**Cascades.** There are three by default, each a square 2048² map, held in a `Texture2DArray`: 50.3 MB, against today's 67.1 MB. The camera's depth range, out to a shadow distance of 3,000 units, is split between them by the usual blend of uniform and logarithmic splits (Zhang et al. 2006). Beyond the shadow distance everything is lit, as everything beyond §10's square is lit today.

**Stability.** §10 kept shadows from swimming by never moving the map; cascades must move, so they move in whole texels. Each cascade covers the sphere around its slice of the frustum, a size that does not change as the camera turns, and its center moves in whole texels of the sun's view. Its depth runs from the sun-side edge of the world's bounds to beyond its slice, so every caster between the sun and the slice lands in the map. An extent that does not follow the camera's turning, and movement in texel-sized increments, are the usual remedies for a moving camera, as Microsoft's two articles on shadow maps describe them.

**Lighting.** The lighting picks, for each point, the first cascade whose map holds the point and the reach of its 3 × 3 filter. It offsets the point along its normal by 1.5 of that cascade's texels (§10). Each cascade is culled and splatted on its own (§7.4). `ShadowFactor`'s twin gains the choice of cascade. The fit runs only on the CPU and is tested for stability and containment (§15).

**Views.** The shadow-map debug view shows one cascade at a time. A new debug view tints the lit image by the cascade each pixel used, for tuning the splits.

**Cost.** The shadow splat runs once per cascade, over whatever that cascade's culling leaves, and the farthest cascade sees most of the world. The bench times each cascade (§14); nothing here assumes the sum is small.

Until S-M6 the space scene keeps one map, fitted to the whole layout: coarse, but correct.

## 11. The sky (S4, S10)

### 11.1 Why not a cube map

At 1920 × 1080 and a 45° field of view, a pixel spans about 0.04°, and a texel of a 2048² cube face spans from 0.03° at the face's edges to 0.06° at its middle: about one texel per pixel. A star baked into such a texel spreads its energy over one to four pixels, depending on where the texel falls between them, so it pulses in brightness as the camera turns. The cube costs 201 MB at `R16G16B16A16_FLOAT`, and at 4K it needs four times that to keep up. A sprite keeps each star's energy exact and its position sub-pixel at any resolution. The galaxy is smooth enough for a map, but evaluated per pixel it needs no memory, and its twin agrees with it function for function, which a cube map's filtering across face edges would not.

### 11.2 Stars

**The catalog** is generated by `NeuronCore` from the world's sky seed: 20,000 stars by default.

- **Brightness.** Magnitudes are drawn so that the number of stars brighter than *m* grows tenfold every 2.2 magnitudes (log *N* ∝ 0.45 *m*, close to the real sky's), from a handful near magnitude −1 down to the faintest the count reaches. A star's flux is 10^(−0.4 *m*).
- **Direction.** Bright stars fall almost evenly over the sky. Fainter ones gather increasingly toward the galactic plane and its core, as distant stars do.
- **Color.** An effective temperature from 2,500 to 30,000 K, most between 4,000 and 7,000 K. The Planck spectrum is integrated against the CIE 1931 color-matching functions, using the analytic fit of Wyman, Sloan and Shirley so that no table is needed, into linear Rec. 709, and normalized to unit luminance. The result is then moved toward white by a saturation of 0.35, so that the differences stay slight.

`StarRecord` holds a star's direction, flux and color. It is the element of a structured buffer, so its layout is shared with HLSL under R16.

**Drawing.** One quad per star. The vertex shader projects the star's direction, a point at infinity, so the camera's position never enters. It sizes the quad to where the star's point-spread function falls below the darkest step the tone map shows. The pixel shader integrates a Gaussian point-spread function (σ = 0.7 pixels) over the pixel's square exactly, through the error function, so a star that moves by a fraction of a pixel neither flickers nor changes brightness. A second Gaussian, wider (σ = 2.5 pixels) and much fainter (2 % of the energy), makes a bright star look larger than a faint one, and that is where the natural look's different sizes come from. Both add into the HDR color before the tone map, and the depth test of §9 keeps every star behind every voxel.

**Twins.** `StarPixel` gives one star's radiance in one pixel, and the error function is one polynomial (Abramowitz and Stegun 7.1.26) in both languages. The catalog runs only on the CPU.

### 11.3 The galaxy

The galaxy is a function of direction in the galaxy's own frame, which the welcome's orientation places in the world:

- **the disk**, whose brightness falls off exponentially with galactic latitude, thicker and brighter toward the core;
- **the bulge**, about the core, warmer in color than the disk;
- **star clouds**, the disk modulated by a few octaves of value noise over the direction, a lattice hashed with `PcgHash`, whose twin already exists;
- **dust**, an absorbing layer thinner than the disk and a little off its plane, shaped into lanes by a ridged noise, which reddens what it dims.

A gain, tuned by eye, keeps it faint: a spread in the background, not a source of light.

### 11.4 The sun

The sun is a disc at the welcome's sun direction and angular radius (0.27° by default), at the sun's radiance, which the tone map clips to white. It has limb darkening and a faint corona that falls off over a few degrees, and its edge is antialiased over one pixel's angle. It is the same sun the lighting uses (§12).

### 11.5 The pass

The sky pass runs after the lighting and before the tone map. The HDR color gets a render-target view, and the view's depth is bound read-only. First a full-screen triangle at the far plane writes galaxy and sun into every pixel still at the far plane; then the stars add. The lighting still writes its background into those pixels first, and the sky overwrites them. Because the sky's pixel shader writes no depth, the early depth test keeps the galaxy's noise off every pixel a voxel covers. The sky is timed as a pass of its own.

## 12. Lighting in space (supersedes ADR-008)

SampleRenderer §11's formula stays. What changes is where its values come from: the welcome's world settings, not a `.vox` file's `rOBJ` chunks, which no longer drive anything. `ReadRenderSettings` and its tests go, and the reader still stores the chunks verbatim.

The defaults:

- **The sun** from the station's `_angle` of 50°, 50° as ADR-008 reads it after N-M0's conversion, at `_i` 0.7, white.
- **The hemisphere's two colors** equal and dim (0.05, white), so that a face turned away from the sun is dark but not black.
- **The background** black, under the sky.
- **The emissive mapping** of ADR-008 (`_emit` × 2^`_flux` × the gain) unchanged, read from each model's own palette.

The look is the owner's to judge by eye in S-M5, and the values accepted become the defaults.

## 13. Application

**Command line.** `--seed <n>`, `--stations <n>`, `--frigates <n>` and `--capitals <n>` set the world's parameters (§5.2) and are handed to the in-process server. `--size`, `--warp`, `--adapter`, `--d3d-debug` and `--gbv` are unchanged. `--bench <seconds>` is §14's. `--vox` is retired: models come from the welcome.

**Camera.** The orbit camera orbits a target entity and follows it as it moves. N and B cycle the target forward and back through the stations and the flights' leaders. C switches to a chase camera behind and above a ship, in the ship's frame, sprung so that the ship's banking reads without the view shaking. Tab still flies free, now with +Y as up, and F frames the target.

**Keys.** Space pauses the server, the first command (§6.2). 1 shows the lit image, 2–6 the debug views as today, and 7 the cascade view. [ and ] set the emissive gain, V vsync, F1 the key map and F2 the figures; Alt+F4 quits.

**Figures,** in the title and on the panel: as today, plus the server's tick and the client's delay behind it; the entities; the placements drawn and culled and the voxels drawn, per view and per cascade; the sky's GPU time and each cascade's; and "paused" when the server is.

## 14. `--bench`

**The bench owns time.** Frame *i* of the timeline is at *i* / 60 s. Before drawing it, the bench steps the server up to the last tick the frame's render time needs and delivers the snapshots through the loopback; the client then interpolates at exactly that time. No thread runs and no wall clock enters, so every run of one build draws the same frames.

**The camera's path** is a closed path through the layout, derived from the seed. It passes close to a station (large rectangles, near-plane crossings), takes a wide view of the whole cluster (most voxels under a pixel), and chases a capital ship for a stretch (rigid placements filling the screen). Each stretch is a phase in the summary, as intact, in flight and at rest were.

**What it measures:** everything it measures today (per-pass GPU times, pipeline statistics, covered pixels, both depth variants). To that it adds the sky and each cascade, the placements and voxels each view drew, and CPU times: the server's tick, a snapshot's encoding and decoding, the interpolation, and the client's culling and recording. The summary names the seed and the parameters, so any run can be repeated exactly.

**A preset** of one station, no ships and an orbiting camera at the default framing stands nearest to the M5 note's intact phase (§3.3), with the sky and without the ground. It is what shows the placement refactor's cost on the old workload.

## 15. Verification

**`NeuronCoreTests`:**

- quaternions and rigid transforms, against double-precision references; the cube-symmetry test accepts all 24 proper rotations and refuses the 24 reflections and every near miss;
- `PlacedVoxelBox`: exact centers for aligned placements, bit for bit, and rigid ones within rounding of a double reference;
- the scene tracer, against brute force over every placed box, on seeded random scenes of aligned and rigid placements; like `VoxelGrid`, it walks each placement's grid in model space and tests the candidates with the box the GPU draws;
- culling: a sphere that touches a frustum or a cascade's box is never culled;
- the protocol: every message round-trips; every truncation of a valid message, and every refusal, is refused by name; and golden bytes for one message of each type, written into the test, so that a change of encoding is a visible diff;
- `LoopbackTransport`: order, completeness, closing, and two threads;
- the star catalog: the same seed gives the same bytes; the counts per magnitude follow the target law within bounds; faint stars lie near the plane more often than bright ones do; and every color is in gamut at unit luminance;
- the sky's twins: finite everywhere, including at the galactic poles, along the axes and at the exact sun direction; no seam where longitude wraps; and a star's energy, summed over its pixels, equals its flux wherever in its pixel it falls;
- the cascade fit: turning the camera about its eye leaves every cascade's extent unchanged; moving it by less than a texel leaves every texel center where it was; and every point of every slice lies inside its cascade.

**`NeuronClientTests`, on WARP,** per pixel, with SampleRenderer §14's edge rule and bounds set from the first measured run:

- the layout echo, for every new shared struct;
- several placements of the three models, aligned and rigid, near and far, one across the near plane, against the scene tracer: id, normal and depth in every pixel; and the same at 10,000 units from the camera, where a standard-Z buffer could not separate the surfaces (§9);
- a rigid placement with a cube-symmetric rotation draws what the aligned one draws;
- the lighting through placements: in one image, the capital ship's white does not glow and the station's does (§4);
- the measurement variants and the overdraw count on rigid placements, as the oriented ones were tested;
- the cascades against their twin, and the lighting's choice among them;
- the sky: galaxy and sun per pixel, and stars per pixel, including a star a voxel hides, against the twins;
- `SnapshotBuffer`, on the CPU: bracketing, appearing and disappearing, holding past the newest snapshot, and a stall shorter than the interpolation delay passing unseen.

**`NeuronServerTests` (new):** the handshake, and a refused version; stepping *N* ticks sends *N* snapshots with consecutive ticks; two clients receive the same bytes; pause freezes the world while its ticks go on, and resume moves it again; and the thread starts and stops cleanly. No test times the thread, because a CI runner's timing is not a measurement.

**`GameLogicTests` (new):**

- the same seed and parameters give byte-identical welcomes, and byte-identical snapshot streams over a long run;
- stations are spaced, whole and cube-rotated;
- over ten simulated minutes, no ship enters a keep-out sphere, every tick respects its class's speed, acceleration and turn rate, every route closes, and wingmen hold their slots within a bound;
- a parameter block that does not fit is refused by name.

**By hand,** the owner runs the scene at S-M4 (the ships fly, and face the right way), at S-M5 (the sky's look and the lighting in space), at S-M6 (the shadows, and no swimming) and at S-M7 (the bench).

## 16. Milestones

| | Delivers | Done when |
|---|---|---|
| S-M0 | This design accepted; M5's performance note committed from the owner's run of `--bench` | The owner accepts; the note is in `Design/` |
| S-M1 | The retirement (§3.1); `SampleRenderer.md` amended; ADR-009 superseded and ADR-008 amended; NVF §12.2's rows for the explosion and the ground struck | Every suite green; the owner has seen the station lit and shadowed, with no floor and no explosion |
| N-M0 | NeuronVoxelFormat.md §12, as amended by S-M1 | As NeuronVoxelFormat.md §10 says |
| S-M2 | Placements: models and palettes, rigid transforms, aligned and oriented placements, ids, culling and order, the scene tracer; the placements ADR | §15's placement tests green; the station renders as before, through one placement |
| S-M3 | The messages, the transports, `ServerHost`, `SpaceWorld` and its flight; the two new suites; the ADR for the client/server boundary, and the layout ADR for the suites | `NeuronCoreTests`, `NeuronServerTests` and `GameLogicTests` green |
| S-M4 | `ClientSession`, `SnapshotBuffer`, placements made from snapshots, the camera, keys, figures and command line; the space scene replaces the station sample | The owner has flown among the ships and confirmed their facing |
| S-M5 | The sky and the lighting in space; the sky ADR; ADR-008 superseded | The sky tests green; the owner accepts the look |
| S-M6 | Cascades; the cascades ADR | The cascade tests green; no swimming on hardware |
| S-M7 | The space bench; a measured performance note; an ADR for every decision its numbers drive | The note is committed |

S-M5 and S-M6 depend only on S-M2 and N-M0, so they may run alongside S-M3 and S-M4.

## 17. Risks and open questions

**Answered by the owner on 2026-09-28:**

1. **N-M0 comes first (S2).**
2. **The space scene replaces the station sample (S1).**
3. **The full boundary, with the server on a thread of its own (S3).**
4. **The galaxy is a band across the sky, seen from inside (S4).**
5. **Reversed-Z throughout (S5).** The view has had it since M2; §9 says how the new passes keep it.
6. **The frigate's front is the end with the two pins on its sides (§4).**

**Open, each with this design's default:**

7. The order of §3.2: retire before N-M0 (the default), or keep the explosion through N-M0 until S-M2 takes the oriented splat over.
8. The cascades' depth: standard Z (the default, §9), or reversed-Z too, for one convention.
9. Bloom. A star's halo and the sun's corona are each object's own light, not a post-process, so this design reads D4's "no bloom" as untouched. If the owner reads them as bloom, they go, and bright stars differ from faint ones by the core alone.
10. The defaults: 4 stations, 40 frigates and 8 capital ships; 30 ticks a second and 100 ms of interpolation delay; 20,000 stars; three cascades of 2048² out to 3,000 units.
11. Stations turned through all 24 symmetries (the default, which stands some of them on their sides), or only through the four about the vertical.
12. The names: this document, the two suites (`NeuronServerTests`, `GameLogicTests`), and whether S3 becomes a conformance rule (§18).

**Risks:**

- **The workload is vertex work on small voxels.** A view of the whole cluster puts most of a million voxels under a pixel, and the splat pays four vertex invocations for each whatever its size (SampleRenderer §2). That is the regime the paper's Listing 3 addresses and §4.2 left out. The bench will show what it costs before anything is done about it.
- **Shadows multiply that work.** Each cascade splats what its culling leaves, and the farthest cascade sees most of the world. The bench times each cascade.
- **Determinism holds within one build.** The simulation repeats itself exactly in one build. MSVC's FMA contraction under `/arch:AVX2` (`AGENTS.md` §3), or another compiler, changes its rounding, so runs are compared within a build. The server, being authoritative, never needs another machine to agree with it.
- **The server's sleep on Windows** can wake milliseconds late. Ticks are stamped exactly and the interpolation delay absorbs it. If the bench's CPU times say otherwise, a high-resolution timer needs Windows headers in `NeuronServer`, the decision ADR-003 deferred.
- **Snapshots are whole states.** 2.5 KB at 30 ticks is nothing within one process. Over a network, delta encoding belongs to the UDP ADR.
- **The GPU's transcendental functions** (`exp`, `sqrt`) in the sky differ from the CPU's by a few ulps. The sky's tests carry a relative bound set from their first run, as SampleRenderer §14 allows.
- **The station bench's numbers** leave with its timeline unless S-M0's note records them first (§3.3).

## 18. Expected ADRs and changes to `AGENTS.md`

ADRs are numbered in order as they land. The next free number is ADR-011, and N-M0's axes ADR takes whatever number its turn gives it.

- **S-M1, the retirement:** supersedes ADR-009, and amends ADR-008's ground and NVF §12.2.
- **S-M2, placements:** rigid transforms and the choice between aligned and oriented; scene-wide ids, amending SampleRenderer §7.3 and ADR-006's tie rule across placements; per-model palettes; culling and order on the host; and the world's bound.
- **S-M3, the client/server boundary:** the messages and their validation, `Transport`, the host's tick, threads and stepping, and the client's time. Also the two new suites, amending ADR-003's table.
- **S-M5, the sky:** the catalog, the point-spread function, the galaxy, the sun, the pass, and their tuned defaults. Also the lighting from the world, superseding ADR-008.
- **S-M6, cascades:** amending SampleRenderer §10 and ADR-006's shadow section.
- **S-M7:** whatever the numbers decide.

`AGENTS.md`:

- **§2's table:** `NeuronServer` and `GameLogic` say what they hold, and `NeuronServerTests` and `GameLogicTests` join the table. `.clang-tidy`'s `HeaderFilterRegex` already matches every `*Tests` folder, and CI finds suites by name, so neither changes.
- **R15's list of twins:** the placement's pose replaces the explosion's, and the sky's functions, the star's point-spread function and the choice of cascade join it.
- **A new rule,** under the next free number (R18 unless NVF's has taken it): *Client and server share bytes, never objects: what the server knows reaches the client only as messages through a `Transport`, in one process as in two.* Its source is S3. Project references already keep `GameLib` away from `GameLogic`; the rule covers `Outpost`, the one project that links both.
- **The paragraph that names the design document** names this one beside `SampleRenderer.md`.

## 19. References

- A. Majercik, C. Crassin, P. Shirley, M. McGuire. *A Ray-Box Intersection Algorithm and Efficient Dynamic Voxel Rendering.* JCGT 7(3), 2018: `Majercik2018Voxel.pdf`.
- N. Reed. *Depth Precision Visualized.* NVIDIA, 2015: https://developer.nvidia.com/content/depth-precision-visualized
- Microsoft. *Cascaded Shadow Maps*: https://learn.microsoft.com/windows/win32/dxtecharts/cascaded-shadow-maps, and *Common Techniques to Improve Shadow Depth Maps*: https://learn.microsoft.com/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps
- F. Zhang, H. Sun, L. Xu, L. K. Lun. *Parallel-Split Shadow Maps for Large-Scale Virtual Environments.* VRCIA 2006.
- C. Wyman, P.-P. Sloan, P. Shirley. *Simple Analytic Approximations to the CIE XYZ Color Matching Functions.* JCGT 2(2), 2013.
- M. Abramowitz, I. A. Stegun. *Handbook of Mathematical Functions*, formula 7.1.26.
- M. Jarzynski, M. Olano. *Hash Functions for GPU Rendering.* JCGT 9(3), 2020.
- G. Fiedler. *Snapshot Interpolation*, 2014: https://gafferongames.com/post/snapshot_interpolation/
- Y. Bernier. *Latency Compensating Methods in Client/Server In-game Protocol Design and Optimization.* Valve, 2001.
