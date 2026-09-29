# ADR-018 — The client: its session, its time and the space scene

**Status:** accepted, 2026-09-28 · **Lands with:** S-M4 of [`Design/Archive/SpaceScene.md`](../Archive/SpaceScene.md) (§6.4, §13, §14, §15) · **Amends:** [ADR-015](ADR-015-client-server-boundary.md), with the client's time it left to this ADR; [ADR-003](ADR-003-engine-and-game-layout.md), whose `--vox` is retired; and SpaceScene §13 and §14, for as long as the question of §17 is open and S-M8 has not come · **Amended by:** [ADR-021](ADR-021-sky.md), which lights the space scene from the welcome rather than with the station's settings; and [ADR-026](ADR-026-game-reads-nvf.md), whose session and sector read each model from `<name>.nvf`

## Context

S-M3 built the server's side of the boundary: the messages, the loopback, `ServerHost`, and the sector ([ADR-015](ADR-015-client-server-boundary.md), [ADR-017](ADR-017-sector.md)). S-M4 builds the client's side, and with it the space scene replaces the station sample (§16):
- a session that says Hello, loads the models the welcome names and takes the snapshots;
- a buffer that relates the client's clock to the server's and interpolates between snapshots (§6.4);
- placements made from the entities and their detonations;
- the camera, the keys, the figures and the command line (§13).

The owner answered two questions for it on 2026-09-28:
- **`--bench`** runs §14's one-station preset in lockstep until S-M8's space bench, and the server detonates the station. The station sample's phases, CSV and summary carry on.
- **A restored leader rejoins as a wingman**, as ADR-017 had it on its own account. ADR-017 now records the owner's confirmation.

The figures below were measured in two ways, on Linux:
- **The suites**, built natively by GCC 13.3 against a throwaway stand-in for the test framework, at `-O1` without contraction, as ADR-015's were.
- **A probe** that drives the real `ServerHost`, `Sector`, `ClientSession`, `SnapshotBuffer` and `Scene` without a window or a GPU:
  - the bench's lockstep loop;
  - the game's loop, against the host on its own thread.

  It was built at `-O2`. Its times are this container's, not a Windows machine's.

## Decision

**Where it lives.**
- `NeuronCore`:
  - `OccupiedBounds`, the box around a model's voxels, whose middle is where an entity stands (§5.1);
  - `ReadVoxFile`, a model file's bytes, which the manifest hashes;
  - `EnclosingSphere`.

  The sector now measures and reads its models through the first two, so server and client find an entity's box by one function and hash one file's bytes.
- `NeuronClient`:
  - `ClientSession`;
  - `SnapshotBuffer`;
  - `SceneModels`, which turns an entity into placements;
  - the renderer's `SetShadowView`, and the voxels each view drew.
- `GameLib`:
  - the space scene;
  - the camera, the chase camera and the keys;
  - the figures;
  - the bench's timeline.
- `Outpost`: the command line, the sector on its host, and the loopback between them.

**The session** (§6.1, §6.2). `ClientSession` sends a Hello as it is made.
- **The welcome.** When the welcome comes, the session loads every model it names from its own directory, as `<name>.vox`. It hashes each file with FNV-1a and parses it, before a snapshot is taken.
- **The snapshots** go into its `SnapshotBuffer`, made at the welcome's tick rate.
- **Commands.** `Send` encodes a command.
- **Refusals.** A refusal closes the transport, and every later `Poll` gives the same refusal:
  - `Closed`: the server closed the session. What it sent before is taken first.
  - `BadMessage`: bytes that do not decode, named by the decoder's refusal, or a message a client never receives: a Hello, a command, a second welcome, or a snapshot before the welcome.
  - `WrongProtocol`: a welcome of another protocol.
  - `ModelNotLoaded`: a model file the client cannot read, or that its reader refuses, named with the reader's refusal.
  - `ModelMismatch`: a file whose hash is not the welcome's.

**The client's time** (§6.4, amending ADR-015). Times are ticks of the server's clock, tick *n* falling *n* / rate seconds after tick 0.
- **The offset.** Each snapshot's offset is its arrival on the client's clock less its tick's time. The buffer keeps the smallest over the arrivals of the last second.
- **The render tick** is the server's time that offset gives, in ticks, less a delay of three ticks: 100 ms at 30 ticks a second.
- **It never runs backward.** When the smallest offset leaves the last second and the next is larger, the render tick holds still until the server's time catches up. It does not jump back and replay motion.
- **What it keeps.** The buffer keeps its snapshots in tick order and ignores a tick it holds already. It drops a snapshot more than a second older than the newest, but always keeps two.

**Between two snapshots** (§6.4). The render tick is bracketed by the first snapshot at or after it and the one before.
- **Whole entities.** An entity present in both moves along the cubic Hermite curve through its two positions and velocities. The curve is summed in double and rounded once, so it gives each snapshot's position exactly at its tick, and an entity that holds still exactly where it is.
- **Rotations** interpolate by nlerp along the shorter arc. A rotation that did not change is kept to the bit, since normalizing again can move it by an ulp. So a station stays a symmetry of the cube and draws aligned (§7.2), and a paused world holds exactly still.
- **The later snapshot decides.** Its entities are present: one only in it has appeared, and one only in the earlier has gone.
- **Debris.** An entity with an event in the later snapshot stands at that snapshot's transform, where it froze, not on a curve through the earlier one. Its debris is posed from the event at the render tick's world tick, interpolated between the two, so it freezes with a paused world. Before the event, it is the whole model at rest.
- **Outside the snapshots**, the nearest is held, before the oldest and past the newest.

**Placements from entities** (§7.1, §7.2, §7.7). `SceneModels` measures each model once:
- the middle of its occupied box;
- the sphere about that middle;
- the mean of its voxels' centers;
- each part's whole placement.

Part *j* of an entity at *P*, turned by *R*, is placed at *P* + *R*(*O*ⱼ − middle). For a station, whose position is whole and whose *R* is a cube symmetry, every term is exact, and so is every voxel's center. A detonated entity's parts take ADR-013's defaults, blasted from the mean of the model's voxels less the part's origin, with the event's velocity turned into the part's axes and the event's seed. The frame's placements run in the order of their entities' ids and then of their parts, and take their ids from `AssignVoxelIds` (§7.3).

**The sun's view** (§10, until S-M7). One square, fitted to the whole layout as far as the client has seen it:
- **What it holds.** It holds every entity's reach: its whole sphere, and the sphere its debris stays in at every time. That is the event's debris once an entity has detonated, and while it is whole, the debris it would leave if it detonated now at its velocity.
- **It never shrinks.** It moves only when a reach pokes out, and then grows by a quarter beyond both, so shadows hold still.
- **Its narrowest** is the station sample's 1,024 units. So the one-station preset draws its shadows at the density M5 measured (§3.3), and the view does not move when the station detonates.

**The lighting** stays the station sample's, `DefaultRenderSettings`, the station file's values (ADR-008), until S-M5 takes the welcome's (§12.1).

**The camera and the keys** (§13).
- **The orbit camera** follows its target entity: it keeps its place relative to the target as the target moves.
- **N and B** choose the next and the previous target, and frame it. The design has them cycle "the stations and the flights' leaders", but no flight crosses the wire (§5.1), and the client cannot tell a leader. Until the owner answers §17's question, they cycle every entity in the order of their ids.
- **F** frames the target: its sphere, or its debris's at its time.
- **C** chases the target: behind and above it, 2.6 of its radii behind and 0.7 above, in its own frame. The camera's frame follows the entity's with a first-order lag, a time constant of 0.35 s, so a ship's banking reads and nothing small shakes the view.
- **Tab** flies free, with +Y up.
- **E and R** send the server a detonate and a restore of the target, and **Space** a pause or a resume, as the newest snapshot says.
- The rest are the station sample's: 1 to 6, the brackets, V, F1 and F2.

**The figures** (§13) add:
- the server's newest tick and rate, and how far behind it the frame is drawn;
- the entities and how many have detonated;
- the target, and whether it is chased;
- "paused";
- the voxels each view drew, beside the placements drawn and culled.

**The command line** (§13):
- **The world:** `--seed`, `--stations` (at most 1,024), `--frigates` and `--capitals` (at most 100,000 each), and `--debris-lifetime` (seconds, 0 for ever, at most a day). These set the sector's parameters. A layout the sector refuses is refused by name before a window opens.
- **Retired:** `--vox`. The client and the server read the same `GameData` beside the executable, where the build copies all three models.

**One process** (AGENTS.md R18, ADR-003).
- `Outpost` makes the sector, its host and a loopback. It gives the host the server's end and the client the client's, and nothing else crosses.
- **The game** runs the host's thread until the client's window closes. If the client throws, the host stops as it is destroyed.
- **The bench** owns time, so `Outpost` steps the host itself. The bench says the tick its next frame needs, and `Outpost` steps the host until it has sent that tick, then has the bench draw. No reference to the server reaches the client: the bench learns of every step only through the snapshots it brings.

**`--bench`** (§14, the owner's answer).
- **The preset.** It runs one station and no ships, from `--seed`, and refuses the other world options.
- **The timeline.** Frame *i* is at render tick 1 + *i* × rate / 60: the first snapshot's tick, and *i* / 60 s after it. It needs tick 1 + ⌈*i* × rate / 60⌉.
- **The detonation.** At the first frame a quarter of the way through, the bench sends the detonate that E sends. The server applies it at its next tick, and the debris is posed from its event.
- **The phases:**
  - intact until the event;
  - in flight until the station's envelope stops, 11.61 s after it (ADR-013);
  - drifted to a stop after that.

  Time is now the world's own, not the station sample's timeline stretched over the run. So a run shorter than a quarter of itself plus 11.61 s, about 15.5 s, never reaches the last phase, and the summary says "no frames" for it.
- **The rest** is the station sample's:
  - the camera once round the station at the default framing;
  - the warm-up;
  - both depth variants and the coverage count;
  - the CSV, which gains the voxels each view drew;
  - the summary, which names the preset and its seed and when the station detonated.
- **The frame interval** now includes the server's steps between frames, about one every other frame. The summary's row says so.

**Tests.** In `NeuronClientTests`, on the CPU, as §15 has them:
- **`SnapshotBufferTests`, 10:**
  - positions along the curve through both snapshots, and exact at each;
  - the shorter arc;
  - a station and a paused world held to the bit;
  - appearing and disappearing;
  - holding before the oldest snapshot and past the newest;
  - debris posed from its event, frozen with a paused world;
  - the delay behind the fastest arrival, and a render tick that never runs backward;
  - a stall of two ticks, less than the delay, drawn to the bit as the client that had no stall draws it;
  - the order and the second the buffer keeps.
- **`ClientSessionTests`, 8,** over a loopback whose server end the test speaks for:
  - the Hello;
  - GameData's three models loaded by name and hash;
  - snapshots into the buffer;
  - commands, in order;
  - every refusal by name, each closing the transport and repeated by every later poll.
- **`SceneModelsTests`, 4:**
  - a station exact and aligned, whole and quarter-turned;
  - every part of a two-part model where a double-precision reference puts it;
  - debris at time 0 the whole model to the bit, blasted from one point with the entity's velocity and the event's seed;
  - a reach that holds the station's debris at every time.

`NeuronCoreTests` gains two tests: `OccupiedBounds` over a three-part model, and over the station, where its grid finds it.

**Measured.**
- **The suites.** Built natively, all 22 new tests pass, and all 145 of `NeuronCoreTests`, 8 of `NeuronServerTests` and 16 of `GameLogicTests`.
- **The tests were tested.** Each of 19 deliberate mutations of `SnapshotBuffer`, `ClientSession` and `SceneModels` fails at least one test. Three that survived a first round, a pause flag, a duplicate tick and a repeated refusal, now fail too. A fourth survivor showed that holding a still position exactly needed no rule of its own, and the rule went.
- **The bench's loop,** probed over a 20 s run:
  - the station intact for 301 frames, in flight for 696 and drifted to a stop for 203;
  - the server detonates it 5.000 s into the timeline;
  - the sun's view stays 1,024 units across and never moves;
  - the server's last tick is 601.

  Over 10 s the last phase has no frames.
- **The game's loop,** probed against the host's thread at 30 ticks a second, polled about 60 times a second for 5 s and for 120 s:
  - the welcome comes 40 ms after the Hello;
  - the default sector draws 52 placements a frame;
  - frames are drawn 71 to 100 ms behind the newest snapshot;
  - sampling, placing and fitting take 0.02 ms a frame;
  - the sun's view is fitted 5,400 units across, grows once to 6,750 within 0.15 s, and then holds for two minutes.

  A detonation sent from the client reaches the server and comes back as an event.
- **In CI,** MSVC's Debug|x64 build of the merge on `main`, on 2026-09-28: all 216 tests of the five suites pass, `NeuronClientTests` on WARP among them, and clang-tidy is clean over the tree's 106 translation units.
- **On the owner's machine,** on 2026-09-28, the owner ran the scene and reported everything working: the ships fly and face the right way, and a station detonates on command. That is S-M4's "done when" (§16).

## What this forecloses

- **A client that draws before its models are in.** The session loads every model the welcome names before a snapshot is taken, and refuses one whose file differs.
- **Extrapolation past the newest snapshot.** The buffer holds it. A render tick that runs backward is foreclosed with it.
- **A sun's view that follows the camera** before S-M7's cascades.
- **The bench stepping the server itself.** Whatever runs the server in the bench's process steps it, and the client side never holds the server.
- **N and B cycling only the flights' leaders**, until the owner says how the client is to know them: by a flag on the wire, which is a new protocol version, or otherwise (§17).
