# ADR-024 — The fragmented detonation

**Status:** proposed, 2026-09-29: built and measured on the CPU, awaiting the owner's eye on hardware · **Lands with:** a follow-up to S-M1 and S-M2 of [`Design/Archive/SpaceScene.md`](../Archive/SpaceScene.md) (§5.5, §7.7) · **Supersedes:** [ADR-013](ADR-013-zero-gravity-detonation.md)'s motion, its defaults and its envelope · **Amends:** [ADR-014](ADR-014-placements.md)'s hash, `ExplosionConstants` and the splat's root signature

## Context

Under ADR-013 every voxel flies on its own. It leaves the blast point along the direction away from it, nudged by a small jitter, at a speed that falls off smoothly with distance and varies by ±20 %. One drag, the same for every voxel, slows it. The owner saw it on hardware and found it too smooth, on 2026-09-29: "all voxels are floating in a bubble". Real explosions break things apart. Some parts break off whole, others blow apart into pieces, and not everything moves out at the same speed.

The bubble follows from the model, not from its tuning:
- Every voxel's position is its launch velocity times (1 − e^(−drag *t*)) / drag, and the drag is the same for all. At any time the field is one picture scaled up, a self-similar expanding shell.
- Neighbours get nearly the same velocity, so the hull stretches rather than tears.
- No voxel moves with another, so nothing can break off in one piece.
- Every spin ends a whole number of quarter turns, so the debris settles square to the axes.

The owner answered four questions on 2026-09-29:
- **Tumble freely.** Fragments turn about any axis and end turned any way. This reverses ADR-013's square ending.
- **Drag varies by fragment.** The drag stays, so the envelope stays bounded in closed form, but it varies by fragment, lower for a larger one. There is no drag in a vacuum. The drag is a visual device that settles the debris in the entity's frame, and the envelope that culling and the shadow fit rely on needs it.
- **Debris only.** A detonation's fragments are scenery, as G51 of [`Design/GameConcept.md`](../GameConcept.md) has it. None becomes a wreck or salvage. The gameplay stays untouched.
- **Full stack.** The C++ twin, the shaders, the renderer and the tests.

D3 stands. pose(*i*, *t*) is a pure function of a voxel's index, its rest position, the event and the time, and nothing carries between frames.

## Decision

**Fragments.** A model breaks into fragments. A fragment is a face-connected piece of one part, and it flies as one rigid body. The breaking is a property of the model, not of the detonation: it is computed once, when the model loads (`NeuronCore/Fragmentation.h`). A seed moves the fragments and never changes their shapes. So a detonation stays a pure function of the event, and no per-event table is built, uploaded or cached.

**How a model breaks** (`FragmentModel`, per part, in the part's space).
- **Starts.** A voxel at distance *d* from the blast origin starts a fragment with chance 1 / (1 + (*d* / `shatterDistance`)³). The draw is `PcgHash` of its record index within its model, mixed with a salt, the fractional part of √2. The salt keeps the draw apart from every detonation hash.
- **Growth.** Each fragment grows from the voxel that started it, a face step at a time. It takes every voxel it reaches first, up to `growthSteps` steps from its start. This is a breadth-first search from every start at once, in record order.
- **Leftovers.** A voxel no start reached, beyond every fragment's reach or in a piece of its own, starts a fragment of its own, in record order, and grows the same way.
- **Result.** A voxel starts a fragment with certainty at the blast origin and rarely far from it. So the core shatters and farther out the pieces are chunks, capped at `growthSteps` steps. Pieces follow the model's connectivity, so a gap in the hull separates them.
- **What each fragment keeps.** Its pivot is the mean of its voxels' centres, in its part's space, summed in double as `VoxelCentroid` sums. Its size scale is its voxel count to the power −1/3: exactly 1 for a lone voxel. Each part also keeps a radius: the farthest any of its voxel centres lies from its fragment's pivot, measured in float as the pose measures it.
- **Indices are the model's own,** part after part. So a model's debris does not depend on where its records sit in the scene's buffer, as ADR-014 has it for voxels. `DebrisDoesNotDependOnTheSceneOrder` holds it.

**The defaults** of the breaking (`DefaultFragmentationParameters`):

| Parameter | Default | Meaning |
|---|---|---|
| `blastOrigin` | the model's voxel centroid | Where the model shatters finest: where its detonation's blast comes from (§5.5) |
| `shatterDistance` | 0.06 × the farthest voxel centre from the centroid, at least 2.5 voxels | A voxel this far out starts a fragment with chance ½ |
| `growthSteps` | 12 | The most face steps a fragment grows from its start |

The shatter distance is relative to the model's size, and was first absolute. At 12 voxels, which suits the station, the frigate went to 1,181 voxels in 902 fragments: a bubble again. At 0.08 of its size, the frigate's core was 1.3 voxels, and it broke into 40 chunks with 5 lone voxels. A floor of 3 gave it 194 fragments.

The first cut, 0.08 of the size, a floor of 3 and 10 steps, broke the station into 8,346 fragments. On 2026-09-29 the owner asked for somewhat bigger fragments. The present defaults break it into 4,834, so a station fragment averages 47 voxels where it averaged 27. The capital ship goes from 456 fragments to 267, and the frigate from 194 to 131.

**The motion** (`ExplosionPose`, per voxel, as part of its fragment *f* with pivot *p* and size scale *s*).
- **The blast travels.** The fragment starts once the blast has crossed the model to its pivot: a delay of |*p* − origin| / `shockSpeed`. Before that it is exactly intact. So the model visibly tears outward from the blast point.
- **The launch** points away from the blast origin, measured from the pivot, jittered as ADR-013's was. Its speed is `launchSpeed` / (1 + *d* / `falloffDistance`) × e^(`speedSpread` · *z*) × *s*:
  - *d* is the pivot's distance from the origin;
  - *z* = 2 (*u*₁ + *u*₂ + *u*₃) − 3 is near normal, with unit variance, and bounded to [−3, 3), so a few fragments outrun the field;
  - the factor *s* makes a larger fragment slower.
- **Its drag** is max(`drag` × √*s*, `minDrag`). A larger fragment has a lower drag, so it slows later and keeps going longer. Fragments settle on different time scales, which breaks the self-similar scaling that made the bubble.
- **Its end.** Its pivot ends launch / fragment drag from where it started. Relative to a lone voxel from the same place that is *s* / max(√*s*, `minDrag` / `drag`) ≤ √*s*: a larger fragment ends nearer. `LargerFragmentsFlySlowerAndLessFar` holds it.
- **Its turn.** It turns about a hashed axis, uniform on the sphere, through its pivot. The angle is `maxSpinRadians` × *u* × *s* × the motion made under its own drag. A small fragment tumbles more, a large one less, and each ends turned any way. The turn is Rodrigues' formula, `Turn` in both twins.
- **The inherited velocity** carries every fragment alike, under the lone voxel's drag and from the detonation on. So the debris field drifts as one body, and the envelope's drift is still inherited velocity / `drag`. ADR-014 folded the inherited velocity into each voxel's launch. Under per-fragment drags that would spread a moving ship's debris along its heading by mass, so this ADR separates it.
- **The voxel's centre** is rest + (turned offset − offset) + flown + carried, where the offset is the rest centre minus the pivot. Every voxel of a fragment turns alike, exactly, and any two keep their distance. `FragmentsMoveAsRigidBodies` checks 30,694 such pairs.

**The parameter block and its defaults** (`DefaultExplosionParameters`), in voxels and seconds:

| Parameter | Default | Was (ADR-013) | Meaning |
|---|---|---|---|
| `launchSpeed` | 180 | 350 | A lone voxel's launch at the origin, before its variation |
| `falloffDistance` | 150 | 150 | The launch speed halves this far from the origin |
| `directionJitter` | 0.35 | 0.35 | Length of the hashed unit vector added to the direction |
| `speedSpread` | 0.3 | `speedJitter` 0.2 | *σ* of the log-normal-like speed variation |
| `drag` | 1 per second | 1 | A lone voxel's drag, and the inherited velocity's |
| `minDrag` | 0.3 per second | — | The least drag of any fragment. It must be positive and at most `drag` |
| `maxSpinRadians` | 4π | `maxQuarterTurns` 4 | A lone voxel turns through up to this in all |
| `shockSpeed` | 400 | — | The blast's speed across the model |

The launch speed fell from 350 to 180 to keep the envelope inside the shadow map. The bound takes the tail of the speed variation, e^(3 × 0.3) = 2.46, so the fastest lone voxel can leave at 443 voxels a second, against ADR-013's 420. These are a first cut. The owner tunes the look by eye on hardware, as ADR-013's defaults were tuned, and the values accepted become the defaults, recorded here in the commit that sets them.

**Randomness.** Fragment *f*'s *k*th random number is `PcgHash`(8*f* + *k* + seed × 0x9E3779B9), as ADR-014 had it for voxels, and *f* is the fragment's index within its model. There are eight streams:
- the jitter's height and heading;
- three uniform numbers for the speed;
- the spin axis's height and heading;
- the spin's amount.

**Exact at both ends.**
- **At time 0,** and until the blast reaches a fragment, its motion made is exactly 0. So its angle is 0, its cos exactly 1 and its sin exactly 0, and `Turn` gives the offset back exactly. The pose is the rest to the bit. `ExplosionStartsIntact` checks all 225,048 of the station's voxels. `NothingMovesBeforeTheBlastArrives` checks the delay.
- **At the end.** Once e^(−drag *t*) falls below half a float's last bit below 1, a fragment's motion made rounds to exactly 1 and it is still. That is by its delay plus 32 / its drag at the latest, so by the stop time plus 32 / `minDrag` for every fragment. `EndsStill` checks it.

**The envelope** (`BoundExplosion`, which now also takes the part's fragment radius *R*).
- **Radius.** Let reach be `launchSpeed` × e^(3 `speedSpread`) / `drag`. A pivot *d* from the origin moves at most reach / (1 + *d* / `falloffDistance`), because *s* / max(`drag` √*s*, `minDrag`) ≤ √*s* / `drag` ≤ 1 / `drag`. Pivots lie among the voxel centres, so ADR-013's convexity argument bounds them. A voxel keeps its distance from its pivot however the fragment turns. So the radius is max(*f*(0), *f*(farthest)) + *R* + √3/2.
- **Stop time.** Every fragment has started by farthest / `shockSpeed`. After that, what is left of any fragment's motion is at most e^(−`minDrag` (*t* − delay)) of its whole. That whole is at most reach, the drift's length, and the arc of `maxSpinRadians` at *R* + √3/2. The stop time is the delay plus ln(256 × that sum) / `minDrag`.
- **No seed.** The envelope does not depend on the seed. So `SceneModels::Reach` still bounds a detonation before it happens, and `PlacementEnvelope` bounds one placement's.

**On the GPU.**
- **Two buffers, uploaded once** by `VoxelScene` beside the records, from the `SceneFragments` the client poses with:
  - `g_fragmentOf`: each record's fragment within its model, a `StructuredBuffer<uint>` parallel to the records;
  - `g_fragments`: every model's fragments, model after model, a `StructuredBuffer<Fragment>`.
- **`Fragment`**, 16 bytes (R16): `pivot` at 0, `sizeScale` at 12. The truth is `NeuronCore/Fragmentation.h`, the mirror is `Shader/Fragment.hlsli`, and the layout echo covers it.
- **`ExplosionConstants`**, 68 bytes where it was 60. `speedJitter` becomes `speedSpread` at 40, and `drag` stays at 44. `minDrag` is at 48, `maxSpinRadians` 52, `shockSpeed` 56, `seed` 60 and `firstFragment` 64: where the placement's model's fragments start in the scene's buffer. `maxQuarterTurns` and `hashBase` go, and so does `Placement::hashBase`.
- **The splat's root signature** gains two vertex-visible root SRVs, t2 and t3, before the overdraw table, which stays last. Both permutations share it. Only the oriented one declares the buffers, and it reads them only once a detonation's time is past 0.
- **The renderer's checks.** `CheckPlacements` refuses a detonation whose fragments lie beyond the scene's, since the shader indexes them unbounded.

**On the CPU.**
- `PlacementDetonation` carries a `PartFragments`: views of its part's fragment indices and its model's fragments, with the model's first fragment in the scene and the part's radius.
- `SceneFragments` builds them for a scene, model after model, as `SceneRecords` lays out the records. `SceneModels` holds one, and `PlacedVoxelBox` poses through it, so the scene tracer and every test pose exactly as the shader does.
- **One breaking per scene.** The renderer takes the `SceneModels`' `SceneFragments` and uploads it, so a scene is broken once, when it loads. `VoxelScene` refuses fragments that are not of its records. A scene built without them, as in the GPU tests that pose nothing, breaks its models itself.

**The station and the ships under the defaults.** Measured by a throwaway probe that ran the C++ twin over every voxel, built with GCC 13.3 at `-O2` for x86-64 in the session's Linux container. The suite logs the same figures.

| | Station | Capital ship | Frigate |
|---|---|---|---|
| Voxels | 225,048 | 10,747 | 1,181 |
| Fragments | 4,834 | 267 | 131 |
| Lone voxels | 968 | 43 | 30 |
| Fragments over 50 voxels | 1,294 | 54 | 4 |
| Largest fragment | 765 | 632 | 118 |
| Part radius *R* | 13.88 | 10.02 | 9.19 |
| Breaking it, once at load | 83 ms | 2.3 ms | 0.4 ms |
| Envelope radius | 457.47 | 453.61 | 452.79 |
| Stop time | 40.53 s | 39.82 s | 39.69 s |
| The blast crosses it in | 0.40 s | 0.09 s | 0.04 s |
| Median displacement, all voxels | 57 | 75 | 103 |
| Median displacement, lone voxels | 138 | 191 | 202 |
| Median displacement, fragments of 2–50 | 86 | 105 | 106 |
| Median displacement, fragments over 50 | 53 | 71 | 52 |
| Launch speed at its start, 10th / 50th / 90th percentile / most | 14 / 25 / 52 / 342 | 20 / 32 / 64 / 332 | 23 / 59 / 122 / 390 |
| Motion made by 1 s, median | 31 % | 33 % | 44 % |
| Motion made by 5 s, median | 88 % | 88 % | 94 % |

- **Station, by distance.** The mean size of a voxel's fragment, by eighth of the station's extent out from the blast, is 7.4, 54.1, 179.9, 253.2, 260.7, 236.7, 147.6 and 40.3 voxels. It shatters at the core and breaks into chunks farther out. The outer eighth is smaller because a thin structure there has fewer neighbours to take.
- **Against ADR-013.** ADR-013's station debris ended 137 to 410 voxels from where it started, with a median of 250. Here the median is 57, and it was 63 with the first cut's smaller fragments. The big chunks drift off slowly and the dust flies far, rather than every voxel riding one shell. Whether that is enough violence is the owner's call on hardware. `launchSpeed` scales all of it.
- **The shadow map.** The station's envelope sits at 35.29 + 457.47 = 492.77 of the map's 512. `ExplosionStaysInsideItsEnvelope` holds the defaults to that.
- **How loose the bound is.** The followed voxels reached 300 of the 457.47. The bound takes every fragment at the tail of its variation, e^(3*σ*), which few reach. It is honest, and 34 % loose.
- **How loose the stop time is.** The stop time is set by the heaviest fragments at `minDrag` 0.3, and by 1/256 of a voxel. So nine tenths of the motion is made by 5 s, and the rest to 40 s is sub-voxel creep. ADR-013's stop was 11.61 s. E and R run the clock to it, so reassembly from a full stop takes up to 40 s at normal speed.
- **GPU memory.** 4 bytes per record, plus 16 per fragment. For the three models, 236,976 records and 5,232 fragments, that is 948 KB and 84 KB, uploaded once.
- **Loading.** A scene is broken once, when it loads: 83 ms for the station, on the probe's build.

**Verification in this change.**
- **`NeuronCoreTests`**, 212 tests with `FragmentationTests` new, and **`GameLogicTests`** pass under GCC 13.3 against a throwaway stand-in for the test framework.
- **Every shader** in `NeuronClient` and `NeuronClientTests` compiles with dxc for Linux at Shader Model 6.7, warnings as errors.
- **clang-tidy 18** finds nothing in the changed NeuronCore and test sources.
- **On WARP in CI,** MSVC's Debug|x64 build, on 2026-09-29, with the first cut's fragments:
  - all 286 tests passed, clang-tidy included;
  - the oriented splats drew the block exactly as the twin poses it, at 0.1, 0.4 and 1 s and at its stop time, whole and turned, in the view and the sun's map: no pixel differed, not even on an edge;
  - the layout echo read every field of the new `ExplosionConstants` and `Fragment` where the structs put them.
- **Not verified:** the ARM64 and Release builds. The bigger fragments and the shared breaking had not yet been through CI when this was written.

**Tests.**
- **`FragmentationTests`:**
  - every voxel is in one face-connected fragment of its own part;
  - pivots are the means;
  - size scales are exact;
  - the radius holds every voxel and is at most twice the growth steps;
  - a model breaks the same way twice;
  - `SceneFragments` lies model after model;
  - the station and both ships shatter finer near the blast than halfway out, and their figures are logged.
- **`ExplosionTests`,** on fragments broken from random models under random breaking and parameter blocks:
  - a lone voxel at the origin flies along its jitter;
  - fragments end still, and move as rigid bodies;
  - nothing moves before the blast arrives;
  - a larger fragment flies slower and less far;
  - seeds differ;
  - the inherited velocity carries everything alike;
  - the envelope holds and stops;
  - nothing is faster than its bound.
- **`MilitaryStationTests`:** the station under the defaults, as ADR-013's were, less the square ending.
- **`PlacementTests` and `SceneTracerTests`:** detonated placements carry their parts' fragments, and brute force agrees with the tracer on 6,000 rays over 12 random scenes.
- **`ExplosionSplatTests`:** the block's detonation now has a slow blast (`shockSpeed` 20) and a `minDrag` of 0.75, so fragments start one after another inside the drawn times.
- **The layout echo:** the new `ExplosionConstants` and `Fragment`.

## Not built here

Each of these is its own decision:
- **Blasting from the reactor.** G51 detonates a ship when a reactor fails. The blast origin, and the point the model shatters finest about, stay the centroid until modules are placed.
- **Vaporising the core.** A shrinking or vanishing voxel needs a scale in the pose, and the splat draws unit boxes today.
- **Heat and a flash.** Emissive, cooling fragments and a light at the event. Bloom exists to carry them.
- **Different cut lines per detonation.** The breaking is fixed per model. Two detonations of one model differ in motion, not in shape. A few variants per model, chosen by the seed, would cost memory in proportion.
- **Posing each fragment once.** Every voxel of a fragment computes the same launch, drag and turn. A compute pass that poses fragments once a frame is the lead [`Design/GameConcept.md`](../GameConcept.md) §14 already names for the heaviest frame, and this makes it cheaper still.
- **Fragments as wrecks.** A large piece that breaks off is scenery. Making it salvage is a change to G51, not to this ADR.

## What this forecloses

- A pose that depends on anything but the fragment's index within its model, its pivot and size, the voxel's rest centre, the event and the time. The breaking is the model's, computed at load, and the same on every client.
- A detonation without fragments. A lone voxel is a fragment of size 1, and `PlacementDetonation` always carries its part's.
- A `minDrag` of 0 or less, or above `drag`, and a `shockSpeed` of 0 or less. The envelope is bounded by them.
- ADR-013's square ending. Debris ends turned any way.
- A different default, or a different construction, without a change to this ADR.
