# ADR-015 — The client/server boundary

**Status:** accepted, 2026-09-28 · **Lands with:** S-M3 of [`Design/SpaceScene.md`](../SpaceScene.md) (§6, §15, §16) · **Amends:** SpaceScene §6.2's snapshot, which gains a world tick

## Context

The owner chose the full boundary, with the server on a thread of its own (SpaceScene S3, §6). Nothing the server knows reaches the client except as bytes. `NeuronCore` encodes those bytes as messages, and a `Transport` carries them. Inside one process the transport is a `LoopbackTransport`. The owner also asked that the loopback's queues sit behind a mutex, with no atomics, and that S-M3 count as done only once its suites pass on ARM64 too (§6.3, §17 question 18).

S-M3 builds the server's side of the boundary: the messages, the transports, `ServerHost`, and the `World` it simulates. The world itself is the sector, which [ADR-017](ADR-017-sector.md) records. The client's side, `ClientSession` and `SnapshotBuffer`, lands with S-M4, whose ADR will amend this one with the client's time (§6.4).

The figures below were measured in three ways:
- **The suites, built natively** by GCC 13.3 for x86-64 against a throwaway stand-in for the test framework:
  - at `-O1` without contraction;
  - with `-O2 -mfma -ffp-contract=fast`, which forces the contraction MSVC may do under `/arch:AVX2` (`AGENTS.md` §3).
- **ThreadSanitizer**, GCC's, over the suites that run two threads.
- **An independent encoder** in Python's `struct`, written from the layout below and nothing else, which produced the golden bytes.

## Decision

**Where it lives.**
- `NeuronCore`:
  - the messages, `EncodeMessage` and `DecodeMessage` (`Message.h`);
  - `Transport` and `LoopbackTransport`;
  - `QuaternionOf` and `IsUnitRotation`, which turn a rotation into what the wire stores and check it;
  - `Fnv1aHash64`, the manifest's hash.
- `NeuronServer`:
  - `ServerHost`;
  - `World`, the abstract class it simulates through.

**The encoding** (§6.2). Every message is little-endian. It starts with an 8-byte header:
- a `u16` type: `Hello` 1, `Welcome` 2, `Snapshot` 3 or `Command` 4;
- a `u16` layout version, which is 1;
- a `u32` whole size, in bytes.

The messages:
- **`Hello`**, 12 bytes: a `u32` protocol version, which is 1.
- **`Welcome`**:
  - `u32` protocol version, `u32` tick rate, `u64` tick;
  - the settings:
    - the direction to the sun, three `f32`, of unit length within 10⁻⁴;
    - the sun's radiance, three `f32`, not negative;
    - its angular radius, an `f32` between 0 and a quarter turn;
    - the ambient above and below, three `f32` each, not negative;
    - the sky's seed, a `u32`;
    - the galactic plane, four `f32`: a rotation as below;
  - a `u32` model count, then each model: a `u8` name length from 1 to 255, the name in letters and digits, and the `u64` hash of its file.
- **`Snapshot`**:
  - `u64` tick, `u64` world tick;
  - `u32` flags, bit 0 for paused and the rest zero;
  - `u32` entity count, `u32` detonation count;
  - the entities, 48 bytes each: `u32` id, never 0; `u16` model index; `u16` reserved, zero; the position, three `f32`; the rotation, four `f32` in the order x, y, z, w, of unit length within 10⁻⁴ and with w ≥ 0; the velocity, three `f32`;
  - the detonations, 28 bytes each: `u32` entity, `u32` seed, `u64` world tick, and the velocity, three `f32`.
- **`Command`**, 16 bytes: a `u32` kind (pause 1, resume 2, detonate 3, restore 4), and a `u32` entity, 0 for a pause or a resume.

A message is one whole message and nothing more. The encoding is canonical: decoding a message and encoding it again gives back its bytes. A snapshot of the default sector's 52 entities is 2,532 bytes, 75,960 bytes a second at 30 ticks, as §6.2 estimated. That is arithmetic from the layout.

**Validation** (§6.2). `DecodeMessage` takes the bytes and the count of models in the receiver's manifest. It returns the message, or refuses by name:
- `Truncated`: fewer bytes than a header, or than the header's size.
- `UnknownMessage`: a type the protocol does not have.
- `UnsupportedVersion`: another layout version.
- `MalformedMessage`:
  - a size or a count that disagrees with the bytes;
  - a reserved field or flag bit that is not zero;
  - a command kind the protocol does not have, a pause or resume that names an entity, or a detonate or restore that names none;
  - an entity id of 0, or two detonations of one entity;
  - a tick rate of 0, or a setting out of its range.
- `BadName`: a model name that is empty or holds anything but letters and digits.
- `NotFinite`: a NaN or an infinity.
- `NotUnitRotation`: a rotation off unit length by more than NVF's tolerance, or with w < 0.
- `DuplicateEntity`: two records of one entity.
- `BadModelIndex`: a model index the manifest does not have.
- `UnknownEntity`: a detonation of an entity the snapshot does not hold.

**The world tick** (amends §6.2). A snapshot carries two ticks:
- **The tick** is the clock's. It counts every snapshot and never stops.
- **The world tick** counts the ticks the world has advanced, and stands still while the world is paused.

A detonation's tick is a world tick. So debris, whose pose is a function of the time since its event (§5.5), freezes with the world while the clock runs on. A client that joins late poses the debris from the same event, and it lands where everyone else's does.

**While paused** (§6.3), every entity's velocity is sent as zero. The client's Hermite curve through two equal positions then holds still, rather than swinging out along velocities the world is not moving at (§6.4).

**The transport** (§6.1). `Transport` carries whole messages.
- `Send` copies one message in. It returns false once the transport is closed.
- `Receive` returns the next whole message, or nothing when none is waiting.
- `Close` closes both directions, and the other end sees it too. What was sent before a close can still be received.
- Each end belongs to one thread at a time.

`LoopbackTransport` is a pair of ends over two queues, one for each direction. Each queue is a mutex, a deque of messages and a closed flag, and there are no atomics. It is reliable and ordered, and it copies bytes in and out. Destroying an end closes the link.

**The host** (§6.1). `ServerHost` holds a `World` and one session per transport. A session joins only while the host is stopped: `AddSession` throws `std::logic_error` while the thread runs. `Step` is one tick:
1. **It serves every session.** It reads each session's messages in order:
   - A `Hello` of this protocol is answered with a `Welcome`. The welcome's tick is that of the last snapshot sent, which the next snapshot follows.
   - A welcomed session's commands are applied in the order they arrive, at the current world tick. A detonation is thus stamped with the world tick of the last snapshot its client could have seen.
   - A session closes on anything else: a `Hello` of another protocol, a second `Hello`, a command before the `Hello`, a message that is not a command, or bytes that do not decode. That is how a client learns it was refused.
2. **The clock advances.** Unless the world is paused, the world tick advances too, and the world with it: `World::Advance`.
3. **The world describes itself.** The host encodes the snapshot once and sends the same bytes to every welcomed session.
4. **Closed sessions are dropped.**

`World` is an abstract class in `NeuronServer`, so the host simulates nothing itself. Its tests bring a world of their own, and the game's is `GameLogic::Sector`.

**The thread** (§6.3). `Start` runs `Step` on a `std::jthread` at the world's tick rate.
- **Exact tick times.** Tick *n* falls at *n* times the period, counted in whole nanoseconds from the thread's start. A late wake-up delays a snapshot but never changes what is in it.
- **Sleeping.** The thread sleeps with `std::this_thread::sleep_until`, so `NeuronServer` needs no Windows header (ADR-003).
- **Catching up.** When it wakes, it runs every tick that is due, but at most `MAX_TICKS_PER_WAKE`, 4. Still behind after those, it starts counting again from now, so a stall is skipped rather than caught up in a spiral.
- **Stopping.** `Stop` asks the thread to stop and joins it.
- **Who owns the host.** While the thread runs, the host belongs to it, and its owner calls nothing but `Stop`.

The host's thread and its clients share nothing but the loopback's queues.

**Stepping.** The tests call `Step` themselves, one tick at a time, and run no thread but the one test that starts and stops it. S-M4's `--bench` will do the same (§14). No test times the thread, because a CI runner's timing is not a measurement.

**Tests.**
- **`NeuronCoreTests`:**
  - `MessageTests`, new, 11 tests:
    - every message round-trips, bytes for bytes;
    - every truncation of a valid message is refused as `Truncated`, and every other refusal by its name;
    - golden bytes for one message of each type, written into the test, so that a change of encoding is a visible diff: 12, 116, 112 and 16 bytes;
    - FNV-1a's published vectors;
  - `LoopbackTransportTests`, new, 5 tests: order, every byte, closing from either end, destroying an end, and two threads;
  - `RigidTransformTests`, 3 more tests:
    - `QuaternionOf` inverts `RotationOf` within 1.8 × 10⁻⁷;
    - the 24 cube rotations survive their quaternions exactly;
    - rotations are stored as NVF stores them.
- **`NeuronServerTests`**, new, 8 tests over a world of one ship:
  - the handshake;
  - the clients the host closes;
  - a snapshot every step, with consecutive ticks;
  - the same bytes to every client;
  - a pause that freezes the world and not the clock;
  - detonate and restore reaching the world;
  - a client that closes, dropped;
  - the thread starting and stopping cleanly.

**Measured.**
- **Natively,** at `-O1`, all 143 tests of `NeuronCoreTests` and all 8 of `NeuronServerTests` pass.
- **Under forced contraction,** 142 and 8 pass. The one failure is `TracesThePinnedVoxels`, 9 pixels of the three-quarter image, which fails the same way on `main` (ADR-014).
- **ThreadSanitizer** reports nothing over `LoopbackTransportTests`' two threads, or over `NeuronServerTests`, whose host runs its own thread.
- **In CI,** MSVC's Debug|x64 build, on 2026-09-28: all 192 tests of the four suites pass, and clang-tidy is clean over the tree's 99 translation units.
- **On the owner's machines,** on 2026-09-28, the three suites pass on ARM64 and on x64. That is S-M3's "done when" (§16): the host's thread and the loopback's are the tree's first, and ARM64 orders memory more weakly than x64 (§6.3, ADR-012).

## What this forecloses

- **Sharing an object across the boundary**, in one process or two. The server's state reaches the client only as these messages, through a transport (`AGENTS.md` R18).
- **Changing a message without its version.** A change of layout bumps the layout version, and a change of meaning bumps the protocol.
- **A snapshot that differs between sessions,** and a command applied at any tick but the current world tick.
- **A lock-free queue** without a measurement that asks for one, and its own ADR (§6.3).
- **A test that times the thread.**
- **A Windows timer in `NeuronServer`** until the bench shows `sleep_until` wakes too late for the interpolation delay to absorb (§17).
