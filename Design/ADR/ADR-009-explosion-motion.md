# ADR-009 — The explosion's motion model and its defaults

**Status:** accepted, 2026-09-27 · **Lands with:** M4 of [`Design/SampleRenderer.md`](../SampleRenderer.md) (§15)

## Context

§12 fixes the model's shape. pose(*i*, *t*) is a pure function of a voxel's index, its rest centre, a small parameter block and the time since the detonation. Randomness comes from a hash, and flights are ballistic, with bounces solved in closed form. Each voxel spins through whole quarter turns, eased to rest. An envelope is bounded in closed form, the aligned permutation draws at *t* = 0 and the oriented one after. §12 leaves the defaults to M4 and asks that they be recorded here.

M4 implemented the model in `NeuronCore/Explosion.cpp`, with its twin in `NeuronClient/Shader/Explosion.hlsli`, and measured it on the station. Two of §12's sentences did not survive the measurement, so this also records what replaces them.

Every figure below was measured the same way. A native probe ran the C++ twin, built with g++ 13 at `-O2`, over all 225,048 of the station's voxels. It took 4,000 samples of each voxel between detonation and rest for corner heights, and 408 for the envelope and rest times, at the defaults below.

## Decision

**The parameter block and its defaults.** `DefaultExplosionParameters` holds them. Lengths are in voxels and times in seconds.

| Parameter | Default | Meaning |
|---|---|---|
| `blastOrigin` | the centroid of every voxel's centre; the station's is (0.36, −10.48, 93.96) | What every voxel is launched away from |
| `gravity` | 30 | Downward acceleration |
| `launchSpeed` | 50 | Launch speed at the origin |
| `falloffDistance` | 150 | Speed is `launchSpeed` / (1 + distance / `falloffDistance`), so it halves 150 voxels out |
| `upwardBias` | 0.5 | Added to the launch direction's +Z before it is normalized |
| `directionJitter` | 0.35 | Length of a hashed unit vector, uniform on the sphere, added to the direction |
| `speedJitter` | 0.2 | Speed varies by up to 20 % either way |
| `restitution` | 0.3 | Vertical speed a bounce keeps |
| `horizontalDamping` | 0.4 | Horizontal speed a bounce keeps |
| `maxQuarterTurns` | 4 | Each of a voxel's two spins turns 1 to 4 quarter turns, either way |

Three constants are not parameters:
- a voxel bounces three times;
- its bounces happen where its bounding sphere meets the ground, at centre height √3/2;
- it rests with its centre at 0.5.

The defaults were chosen to meet two limits. The first is that the envelope stays inside the shadow map's 1,024-unit square (§10). It reaches 452 and 463 voxels from the station's middle, of the square's 512.

The second is that everything comes to rest in about ten seconds at time scale 1:
- Measured rest times run from 0.53 s to 8.75 s, with a median of 5.20 s and 95 % resting by 7.71 s. The envelope's bound is 10.46 s.
- Voxel centres reach from −221 to 217 in x and from −225 to 230 in y, and 268 at the highest; the station's own top is 254.5.
- The fastest voxel moves at 126 voxels a second.

The core near the blast origin flies fastest, and the tower's top mostly falls. The look is the owner's to judge by eye when M4 runs on hardware. The values the owner accepts become the defaults, recorded here in the commit that sets them.

**Randomness.** Voxel *i*'s *k*th random number is `PcgHash`(8*i* + *k*), from `NeuronCore/Hash.h`. Its top 24 bits make a float in [0, 1) exactly. Six streams are used: the jitter's height and heading, the speed's variation, the spin axes, and each spin's turns. No buffer order enters.

**Flights.** The launch velocity points away from the origin, with the bias and jitter added. Flight is ballistic. The first flight ends where the centre falls to √3/2. Each contact multiplies the vertical speed by `restitution` and the horizontal velocity by `horizontalDamping`. After the third bounce, the last flight ends at 0.5 and the voxel stops. Every evaluation follows the whole trajectory from the launch in closed form, so reassembly is only *t* running back.

**§12 said the first flight's contact height is capped at the launch height. Instead, the ground layer is lifted.** The cap was there so that the 22 voxels on the ground have a valid first flight. But it bounces them with their centre at 0.5 while they are turned. With the defaults, 9 of the 22 pushed a corner up to 0.20 voxels into the ground, which §14 forbids. Instead, a voxel whose centre starts below √3/2 is launched upward at least at √(2*g*(√3/2 + 0.5 − *z*₀)). That carries it half a voxel above the bounding radius, and it starts to turn only once its centre has passed that radius. Every bounce but the landing then happens at the bounding radius, for every voxel.

**§12 said the rotation reaches its rest orientation exactly at landing. Instead, it gets there as the voxel last falls through the bounding radius.** Landing square only keeps corners out of the ground while the spin is slow next to the last fall. At the defaults no voxel dips. With restitution 0, eight quarter turns and gravity 60, 12 of the station's voxels pushed a corner up to 0.05 voxels into the ground just before landing. So the spin now runs only while the centre is at least √3/2 above the ground, where no rotation can reach it. The voxel falls its last 0.37 voxels already square. No corner can then go below the ground for any parameter block, and a test checks this on random ones. The angle is eased with *s*(2 − *s*) over the spin's time, so the spin starts at full rate and ends with no angular velocity.

**Exact at both ends.** A spin of *q* quarter turns takes its whole turns exactly, as 0 and ±1 by quadrant, and only the remainder goes through cos and sin. At rest, every voxel lies exactly flat, its centre at *z* = 0.5 and its axes signed unit vectors. At *t* = 0, every voxel is exactly its intact self. So the oriented permutation at time 0 draws the intact boxes, and §14 compares it with the aligned one.

**The envelope, in closed form** (`BoundExplosion`). It starts from the fastest launch, `launchSpeed` × (1 + `speedJitter`), and the lowest voxel's lift.
- **First flight's horizontal reach:** at most the greatest range from the station's height at that speed, (*v*/*g*)·√(*v*² + 2*gh*), or, for a lifted voxel, full speed for twice its lift's time aloft.
- **Each bounce's reach:** at its damped horizontal speed, for as long as its restituted vertical speed keeps it up.
- **Apex:** the highest launch's.
- **Rest time:** the sum of the longest flights.

The box is the model's, grown by that reach and the bounding radius, from the ground to the apex. It is loose: the measured debris reaches about half as far. The tests hold it against sampled trajectories of the station and of 24 random parameter blocks.

**What uses the envelope.**
- The shadow view is fitted to it once (§10), which also reaches the ground as M3's fit did.
- F frames it once the explosion has started, and frames the model before.
- The explosion clock stops at its rest time, so reassembly never takes longer than the explosion did.

**Controls (§13).**
- E runs time forward from wherever it is.
- R runs it back to 0, where it stops.
- Space pauses.
- + and − double or halve the time scale, between 1/16 and 8.
- The title shows *t*, a scale other than 1, and "paused".
- Fly mode's rise and sink move from E and Q, which M2 used, to Page Up and Page Down, because the design's E detonates.

**Axes.** The model is written in today's axes, +Z up (§7.5). `Design/NeuronVoxelFormat.md` §12.2 lists what N-M0 converts: gravity, the contacts, the lift and the envelope's height.

## What this forecloses

- State carried between frames: the pose stays a pure function of index and time (D3).
- A rotation while a voxel's bounding sphere could reach the ground.
- A parameter block whose envelope leaves the shadow map's square without the map being refitted. The station's test holds the defaults to it.
- A different default, or a different construction, without a change to this ADR.
