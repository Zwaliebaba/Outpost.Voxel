# ADR-032 — Sides, sessions and fog of war

**Status:** accepted, 2026-09-29 · **Lands with:** phase 3 of [`Design/MvpPlan.md`](../MvpPlan.md) · **Amends:** [ADR-015](ADR-015-client-server-boundary.md)'s sessions, snapshots and commands, and the foreclosure of a snapshot that differs between sessions; [ADR-029](ADR-029-protocol-composites-and-sides.md)'s welcome and the scene's palettes; [ADR-018](ADR-018-client.md)'s client, which learns its side and remembers what it has seen; [ADR-030](ADR-030-skirmish.md)'s skirmish, which serves each side what it sees and which the window plays as side 1; and [`AGENTS.md`](../../AGENTS.md) §2, whose `NeuronServer` and `NeuronClient` rows gain the command log and the client's memory · **Amended by:** [ADR-033](ADR-033-orders-and-flight.md), whose log is version 2, a command record carrying the game's payload; and [ADR-035](ADR-035-combat.md), whose skirmish parameters are version 2 and whose sides receive the shots their sensors see

## Context

Phase 3 puts fog of war into the skirmish. Each side receives only what its sensors reach (G21, G30), and the server refuses a command that names another side's entity (G34). The server logs every command it applies, so that a skirmish replays from its seed, its build, its libraries and that log (G41, R21).

ADR-015 sends every session the same snapshot, and forecloses one that differs between sessions. ADR-029 kept that rule "until phase 3". A session has no side, and a command is applied whoever sends it.

## Decision

**A session plays a side.**
- `ServerHost::AddSession` takes the side the session plays: one of the world's sides, 1 to their count, or `OBSERVER_SIDE`, 0.
  - An observer receives the whole world, and every command it sends is judged as no side's. The space scene's session is one, and so are the bench's and `--observe`'s, which is for development only.
  - A side the world does not have throws `std::invalid_argument`.
- **The welcome tells the session its side.** After the tick come a `u8` side, 0 for an observer, and three reserved bytes, zero. A side beyond the welcome's own sides is refused as `BadSide`, judged last, after `BadModelIndex`.
- **The versions.** The layout version is 3, for the welcome's new field. The protocol stays 2: G21 and G30 define protocol version 2 as the protocol per side, and ADR-029 kept one snapshot for every session only until this phase.

**What a side sees** (G21, G30).
- `World::Describe` takes the side the snapshot is for, and describes the world as that side sees it. The observer's snapshot is the whole world.
- **In the skirmish,** an entity is in a side's sight while either holds:
  - it is one of the side's own;
  - the middle of its composite's box lies within the sensor range of an intact entity of that side, measured from that entity's middle.

  A design's range is its profile's (`ComputeProfile`'s `sensorRangeUnits`). An asteroid senses nothing, and a detonated entity's sensors went with it. Distances are compared squared, in double precision.
- **Brute force.** `Describe` works visibility out afresh each time it is called, from every entity against every intact entity of the side. So visibility is never stale, and a detonation or a restore changes it on the tick it lands.
- **Detonations.** A detonation reaches a side with its entity, while the entity's debris is in sight.
- **Baselines** (G30). An entity entering a side's sight arrives whole, since every snapshot holds every entity in sight in full: the MVP's entity record is its baseline. One that leaves sight is absent from the next snapshot. Deltas, the "changes that follow", are not built. Every snapshot stays whole until a baseline grows past what a tick can carry, with G31's design and variant and the module states.
- **The host** describes and encodes a snapshot once for each side it sends one to that tick, and every session of one side receives the same bytes.

**Commands** (G34).
- **`World::Refuses`** returns the name of the refusal, `CommandRefusal`, of a command from a side, or nothing. The sector refuses nothing.
- **The skirmish refuses `OtherSidesEntity`:** a detonation or a restore that names another side's entity.
  - An asteroid is no side's, and anyone may detonate it while detonation is a test's.
  - An observer's commands are all applied.
  - Pause and resume are applied from any session while the MVP has one player. A match of two players needs its own answer.
- **A refusal is counted, and the session stays open** (G34). `ServerHost::RefusedCommands` gives the count. Nothing goes back to the client yet: the HUD, when it comes, needs the reason.

**The command log** (G41, R21). `--log <file>`, with `--skirmish`, writes the log as the host runs. Everything is little-endian.
- **The header:**
  - four bytes, `OVCL`; a `u16` version, 1; a `u16` reserved, zero;
  - a `u32` size, then the world's description: the game's bytes, which the engine carries without reading. The skirmish's are a `u8` version, 1, then its `u32` seed and its `u32` tick rate.
  - the manifest: a `u32` count, then each model as the welcome spells it, a `u8` name length, the name in letters and digits, and the `u64` hash of its file;
  - the sessions: a `u32` count, then each session's `u8` side, in the order the host added them.
- **The records** follow, each opening with a `u8` kind:
  - **1, a command the host applied,** in the order it applied them: the `u64` tick and the `u64` world tick it applied it at, the `u32` session, and the command's `u32` kind and `u32` entity.
  - **2, a tick:** its `u64` number, a `u32` count, then the `u64` FNV-1a hash of the snapshot bytes for the observer and then for each side, in order. The host describes every side for the log, whether or not a session plays it.
- **When it is written.** The host writes each record as it happens, and flushes the stream at the end of every tick.
- **Reading it.** `DecodeCommandLog` refuses by name: `Truncated` for a file that ends inside a record, then `NotALog`, `UnsupportedVersion`, `MalformedLog` and `BadName`.

**The replay.** `--replay <file>`, which takes no other option, runs the logged skirmish without a window.
- **Before it runs,** `Outpost.exe` makes the skirmish from the log's description and GameData's models, and `NeuronServer::Replay` refuses a world whose models are not the log's, by name and hash, with `std::invalid_argument`.
- **How it runs.** A host with one loopback client for each logged session steps through the log's ticks. Each client says Hello, then sends its session's commands before the tick the host applied them at. The replay's own host records a log in memory.
- **What it compares.** The replay's log against the file's, command by command and tick by tick. A command the file holds after its last tick, of a step the run did not finish, is neither sent nor compared.
- **What it reports:** the first tick whose commands or snapshot hashes differ from the log's, or that every tick matched, in a message box and the debugger's output. The exit code is 0 when every tick matched, and 1 otherwise.
- **What it holds to.** The world replays from its seed, its libraries and the log (G41). The build is not recorded: a log replays with the build that wrote it, as R21 promises within one build.

**The client** (amends ADR-018).
- **Its side.** `ClientSession::Side` gives the welcome's side. In a world with sides, the figures name it, such as `side 1, Blue`, or say `observer`.
- **What the window plays.** Side 1 of the skirmish, or an observer's session with `--observe`, which goes with `--skirmish` and is for development only. The space scene and the bench have no sides, and the window observes them.
- **What it remembers.** `NeuronClient::LastSeen` remembers each entity of the composites the game marks, as the frame last drew it, from the frame it leaves the sample until the sample holds it again. `GameLib` marks each composite whose design is a structure, from the welcome's names and `GameCore`'s catalogue. In the MVP that is the core.
  - It watches the sample the frame draws, not the newest snapshot. The sample lags the snapshots by the interpolation delay, and a structure remembered from the snapshots would draw twice until the sample caught up.
  - Debris is remembered as it was last drawn, its time since the event stopped there.
- **How it draws them.** A remembered entity draws dimmed, after the entities in sight, and lights nothing: each palette has a remembered variant, whose albedo is a third of the palette's (`REMEMBERED_ALBEDO_SCALE`) and which emits nothing. The third is a first value, to be tuned by eye when a remembered structure can first be seen.
- **The scene's palettes** (amends ADR-029's). The scene holds ADR-029's *M*(*S* + 1) palettes, then the remembered variant of each, in the same order. `RememberedPaletteIndex` is *M*(*S* + 1) plus `SidePaletteIndex`.

## Tests

- **`NeuronCoreTests`.** The welcome's golden bytes at layout 3, its side read back, and `BadSide` judged after `BadModelIndex`. The remembered palettes' indices fill the scene's palettes without a gap.
- **`NeuronServerTests`.**
  - A two-sided world of its own: each session receives what its side sees, the observer all of it, and every session of a side the same bytes.
  - A command on the other side's entity is refused, counted and forgotten, while the observer's is applied.
  - A session of a side the world lacks throws, as does a session added, or a log begun, too late.
  - The log records every applied command and every tick, whose hashes are those of the bytes each side's session received, and is spelled as this ADR spells it.
  - Every refusal of the decoder, by name.
  - A run replays with no difference; a replay that parts from its log finds the tick, whether the world, a command or a hash differs; and a world of other models, or a session of a side the world lacks, is refused before the replay runs.
- **`GameLogicTests`,** on the skirmish (the plan's phase 3 tests).
  - On 20 seeds and every tick of a script of detonations and restores, each side's snapshot holds exactly what the rule, worked out in the test from the observer's snapshot of the same tick, puts in its sight, each entity as the observer receives it, and the detonations of those entities alone. The ranges are the profiles': 1,200 units for the core and 700 for a ship.
  - At the start of every one of those seeds, each side sees its own core and ships and its two near fields, and neither the other side's nor the middle fields (phase 3's checkpoint).
  - Side 2's detonation of side 1's core is refused, and every session receives the bytes it would have received had the command never been sent.
  - Two runs of one seed send every session the same bytes.
  - A logged run replays with no difference from its log and GameData alone, and parts at the first tick on another seed.
- **`NeuronClientTests`.** `LastSeen` remembers a structure as last drawn, and debris as last drawn, and never a ship. A remembered entity draws the same parts with its remembered palettes. The scene holds the remembered palettes, a third of the albedo and no light (WARP). The session learns its side from the welcome.

## Measured

Measured natively, with GCC 13.3 at `-O1` on a 2.1 GHz Xeon, against the repository's sources and GameData, in a program of the agent's that is not in the tree:

- **What a side sees at the start.** On each of seeds 1 to 20, each side's first snapshot holds 17 of the skirmish's 54 entities: its core, its four ships and the 12 asteroids of its near fields. It weighs 856 bytes, against the observer's 2,632.
- **What brute force costs.** `Skirmish::Describe` takes 1.5 µs a call for a side and 0.48 µs for the observer, over 200,000 calls of seed 7. A logged step of a host with a session of each side and an observer, which describes three snapshots, takes 12 µs, against a tick of 33 ms.
- **What the log weighs.** The skirmish's header is 395 bytes with three sessions. A tick record of two sides is 37 bytes and a command record 29, so a minute without commands is 66,995 bytes, and an hour about 4 MB.
- **What a replay costs.** That minute replays in 113 ms, and matches.

## What this forecloses

- **A side seeing past its sensors** by any change to its client: the server decides what each side receives.
- **Sensors that are blocked.** A sensor reaches its range through hulls and asteroids alike, until a design asks otherwise.
- **An entity that leaves the world while remembered.** It stays remembered, since nothing tells the client it is gone. The MVP removes no entity; the first world that does needs a notice for it.
- **Deltas,** until a baseline grows past what a tick can carry.
- **A log that replays across builds.**
