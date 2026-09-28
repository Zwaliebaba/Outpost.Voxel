# ADR-013 — The zero-gravity detonation, and no ground

**Status:** accepted, 2026-09-28 · **Lands with:** S-M1 of [`Design/SpaceScene.md`](../SpaceScene.md) (§3.1, §5.5, §16) · **Supersedes:** [ADR-009](ADR-009-explosion-motion.md), the explosion's motion · **Amends:** [ADR-008](ADR-008-lighting-from-the-file.md)'s ground, and [ADR-011](ADR-011-engine-axes.md)'s rows and pins for the explosion and the ground

## Context

On 2026-09-28 the owner replaced the station sample with a scene in space (SpaceScene S1). It has no ground, and stations and ships must be able to explode there (S17). ADR-009's motion is built on the floor:
- ballistic flights under gravity, with three bounces solved in closed form;
- a lift for the layer that stands on the ground;
- a spin that must be done before a corner can reach the floor;
- an envelope that grows from y = 0 up to the apex.

None of that survives without gravity. SpaceScene §5.5 keeps what does. pose(*i*, *t*) is still a pure function of the voxel's index, its rest position and the time since the detonation (D3). Each voxel leaves the blast point with hashed jitter and a speed that falls off with distance, and spins about hashed coordinate axes, all under a drag. The field therefore slows to a stop within a radius the parameters bound in closed form.

The ground goes from the lighting too (§3.1): `LightPixel`'s ground branch, the switch that showed it, the G key, and the reading of `_setting _ground`.

The figures below were measured in two ways, and each says which:
- **The probe.** A throwaway native program ran the C++ twin over all 225,048 of the station's voxels. It was built with GCC 13.3 at `-O2` for x86-64.
- **The suite.** `NeuronCoreTests` was built by the same compiler against a throwaway stand-in for the test framework, and every one of its 102 tests passed. The station's tests log what they measure.

CI's MSVC build runs the same tests. The GPU twins are compared on WARP there.

## Decision

**The parameter block and its defaults.** `DefaultExplosionParameters` holds them. Lengths are in voxels and times in seconds. The code keeps the explosion's names; the design calls the event a detonation.

| Parameter | Default | Meaning |
|---|---|---|
| `blastOrigin` | the centroid of every voxel's centre; the station's is (0.36, 93.96, −10.48) | What every voxel is launched away from |
| `launchSpeed` | 350 | Launch speed at the origin |
| `falloffDistance` | 150 | Speed is `launchSpeed` / (1 + distance / `falloffDistance`), so it halves 150 voxels out |
| `directionJitter` | 0.35 | Length of a hashed unit vector, uniform on the sphere, added to the direction away from the origin |
| `speedJitter` | 0.2 | Speed varies by up to 20 % either way |
| `drag` | 1 per second | Speed and spin fall as e^(−`drag` *t*); it must be positive |
| `maxQuarterTurns` | 4 | Each of a voxel's two spins turns through 1 to 4 quarter turns in all, either way |

Gone with the floor: gravity, the upward bias, restitution, horizontal damping, the three bounces, the rest height and the ground layer's lift.

**The motion.**
- Voxel *i*'s launch velocity *v* points away from the blast origin, jittered, at the speed that falls off with distance. A voxel at the origin itself has no direction away from it, and flies along its jitter.
- Under the drag, its centre at time *t* is *c*₀ + *v* (1 − e^(−drag *t*)) / drag. It leaves at *v*, slows exponentially, and ends *v* / drag from where it started.
- Each of its two spins turns through its whole number of quarter turns, times the same 1 − e^(−drag *t*), so the spin slows with the flight.

**Randomness is ADR-009's.** Voxel *i*'s *k*th random number is `PcgHash`(8*i* + *k*), and the six streams keep their numbers:
- the jitter's height and heading;
- the speed's variation;
- the spin axes;
- each spin's turns.

So a voxel keeps its jitter, its speed and its spins. The jitter's "height" is its y. It is uniform on the sphere, so it has no up.

**Exact at both ends.**
- **At time 0:** the motion made, 1 − e^(−drag *t*), is taken as exactly 0 on both sides, whatever `exp` makes of it. So time 0 is exactly the intact model, and the oriented permutation draws the intact boxes there, as it did under ADR-009.
- **At the end:** once e^(−drag *t*) falls below half of a float's last bit below 1, the motion made rounds to exactly 1. Every voxel is then still. It is square to the axes, since each spin has turned a whole number of quarter turns and `QuarterTurnCosSin` takes whole turns exactly.
  - At the default drag that is from 17.3 s on, by arithmetic: e^(−*t*) < 2^(−25).
  - The tests check it at 32 / drag.
  - The debris ends as axis-aligned voxels at scattered positions.

**The envelope is closed-form** (`BoundExplosion`): a sphere about the blast origin, and a stop time.
- **Radius.** Let reach be `launchSpeed` × (1 + `speedJitter`) / drag. A voxel *d* from the origin moves at most reach / (1 + *d* / `falloffDistance`) in all, so it ends within *f*(*d*) = *d* + reach / (1 + *d* / `falloffDistance`) of the origin.
  - *f* is convex, so over the voxels, which lie between 0 and the farthest centre in the model's box, it is largest at one end or the other.
  - The radius is the larger of *f*(0) and *f*(farthest), plus the voxel's bounding radius √3/2.
- **Stop time.** What is left of the motion at time *t* is e^(−drag *t*) of it. For a centre that is at most reach. A point of the voxel adds a quarter turn's travel, π/2 × √3/2, for each quarter turn its two spins have left.
  - The stop time is when that sum falls to 1/256 of a voxel, ln(256 (reach + 2 `maxQuarterTurns` × π√3/4)) / drag. 1/256 is the sliver of an edge within which the GPU tests let rounding decide.
  - From then on no point of any voxel is farther than 1/256 from where it ends.

**The station under the defaults**, measured by the probe unless it says the suite:
- **The envelope.** Reach is 420 voxels. The envelope's radius is 420.87 about the blast origin, which lies 35.29 from the middle of the station's box. The stop time is 11.61 s.
- **How tight the bound is.** The farthest any centre reached was 413.12 from the origin, 413.99 with the bounding radius, over 13 sampled times for every voxel. The bound is 1.6 % loose.
- **The stop.** At the stop time, the most any corner of any voxel had left to go was 0.003754 voxels, of the 0.003906 allowed. The suite's `ExplosionDriftsToAStop` finds the same over every voxel.
- **The fastest voxel.** The fastest leaves at 409.9 voxels a second, of 420 at most.
- **Where the debris ends.** Final displacements run from 137 to 410 voxels, with a median of 250. The field ends within 413 of the origin, with a median of 311.
- **How fast the motion is made,** by arithmetic: 22 % by 0.25 s, 63 % by 1 s, 86 % by 2 s, 95 % by 3 s and 99.3 % by 5 s.

**On WARP in CI**, MSVC's Debug|x64 build, on 2026-09-28:
- **The block.** The oriented splats drew it exactly as the twin poses it, at 0.1, 0.4 and 1 s and at its stop time, 5.53 s. That held in the view, in its plain-depth and overdraw variants, and in the sun's map: no pixel differed, not even on an edge.
- **Time 0.** The oriented permutations drew exactly what the aligned ones did.
- **The CPU figures.** MSVC's station figures equal the probe's and the GCC suite's, to the digits the tests print.

**What the defaults were chosen for.**
- **The shadow map.** The envelope stays inside the map's 1,024-unit square, which is centred on the station's box: 35.29 + 420.87 = 456.16 of 512. The station's test holds the defaults to that.
- **A burst.** The core leaves at up to 420 voxels a second, and the debris ends at one and a half to two times the station's size. The field has all but stopped well inside the bench's ten seconds of flight.
- **The owner's eye.** The owner tunes the look by eye when S-M1 runs on hardware (SpaceScene §17, question 13). The values accepted become the defaults, recorded here in the commit that sets them. On 2026-09-28 the owner ran S-M1 on hardware, saw the station lit without a floor, detonated and restored, and accepted the defaults above as they stand, debris ending square to the axes included.

**What uses the envelope.**
- **The shadow view** is fitted once around the sphere's box, in the same 1,024-unit square about the station's box as before. It no longer reaches down to a floor.
- **F** frames the sphere once the detonation has started.
- **The clock** stops at the stop time: E runs it forward, R back, and Space pauses it. Reassembly never takes longer than the detonation did.
- **`--bench`** maps its flight, 25 % to 75 % of the timeline, onto 0 to the stop time. Its phases are now intact, in flight and drifted to a stop. Its numbers change with the motion; M5's note stays the record of the gravity explosion's.

**Controls.** E, R and Space stay. + and −, the time scale, go, and so does G.

**The ground goes.**
- `LightPixel` shades a voxel or returns the background, in both twins.
- `RenderSettings` loses `groundVisible` and no longer reads `_setting _ground`.
- `_ground _color` stays, as the ambient's lower colour. `LightingParameters` and `LightingConstants` now call it `groundColor` rather than an albedo.
- `LightingConstants` keeps its layout. The word that held the switch is named `padding`, since HLSL starts the background at the next 16 bytes either way, and the layout echo reads it.

This amends ADR-008's table and its ground paragraph, and ADR-011's rows for the ground and the explosion.

**The pins of N-M0 that retire** (`Design/NeuronVoxelFormat.md` §12.4, ADR-011). On 2026-09-28 the owner confirmed that a pin of a retired behaviour retires with it (SpaceScene §17, question 17). That is not the re-pinning ADR-011 forbids. Re-pinning regenerates a pin so that a changed behaviour passes; these behaviours are gone.
- **`ExplodesAsPinned`,** the 13 voxels' ground contacts and their centres at 0.25, 1, 3 and 6 s. `PINNED_FLIGHTS` and `PINNED_FLIGHT_SECONDS` leave `PinnedStation.h`.
- **The lower 45 rows of `LightsAsPinned`'s column,** which showed the ground. The upper 46 rows, from the top down to the horizon, show the background and stay pinned, as `PINNED_SKY`.
- **Everything else stays as it was generated:** the four images of `TracesThePinnedVoxels`, which the suite still matches pixel for pixel, and the eight shades of `LightsAsPinned`.

**Tests.**
- **`ExplosionTests`** checks, on random parameter blocks:
  - the centroid;
  - that a voxel at the origin flies along its jitter;
  - that the end is still and square;
  - no quarter turns, no rotation;
  - every voxel exact at time 0, inside the envelope, and stopped by the stop time, with one voxel at the origin in each block;
  - no voxel faster than its launch.
- **`MilitaryStationTests`** checks the station under the defaults:
  - it starts exactly intact;
  - every voxel drifts to a stop;
  - it is no faster than its launch;
  - it stays inside its envelope, which fits the shadow map;
  - it repeats itself.
- **`ExplosionSplatTests` and `MeasurementTests`** stay as they were, on WARP. The block's times are now 0.1, 0.4 and 1 s and its envelope's stop time, and the measured case is taken at 0.25 s, while most of the station is still in view.
- **The layout echo** covers the new `ExplosionConstants`, 40 bytes, and `LightingConstants`.

## What this forecloses

- Gravity, bounces, a floor, or anything else in the motion that has an up, without a change to this ADR. The pose stays a pure function of index and time (D3).
- A drag of 0, or less: the envelope is bounded by the drag alone.
- A parameter block whose envelope leaves the shadow map's square while the map is fitted once. S-M7's cascades change how shadows are fitted, and this ADR's test changes with them.
- A different default, or a different construction, without a change to this ADR.
