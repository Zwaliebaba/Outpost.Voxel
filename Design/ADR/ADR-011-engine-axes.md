# ADR-011 — The engine's axes: Direct3D's, converted from MagicaVoxel's by the reader alone

**Status:** accepted, 2026-09-28 · **Lands with:** N-M0 of [`Design/NeuronVoxelFormat.md`](../NeuronVoxelFormat.md) (§10, §12) · **Amends:** the axes of [ADR-002](ADR-002-voxel-record-and-palette.md)'s figures, [ADR-008](ADR-008-lighting-from-the-file.md)'s sun, ambient and ground, and [ADR-009](ADR-009-explosion-motion.md)'s motion

## Context

Until N-M0 the engine's world was MagicaVoxel's, right-handed with +Z up. NVF puts model space on Direct3D's customary axes (N9), and the owner moved the whole engine to them before any NVF work, so that the `.vox` reader is the one place C++ converts (N10). NVF §12 lists what depends on the axes and says how the move is verified. This records what the move did and what the verification measured.

## Decision

**World space is Direct3D's: left-handed, +X right, +Y up, +Z forward.** One unit is one voxel edge, and the ground is the plane y = 0. A view's basis has right × up = +forward. `MakeViewBasis` builds it as right = worldUp × forward and up = forward × right, which is the only place handedness enters the code. When forward is vertical it falls back to +Z as world up.

**The `.vox` reader converts, and nothing else does.** MagicaVoxel is right-handed with +Z up. The reader swaps y and z, (x, y, z) → (x, z, y), in every `SIZE`, every `XYZI` voxel and every `_t` as it reads them (`FromMagicaVoxelAxes`). The origin *T* − ⌊*s*/2⌋ is therefore computed from swapped values. The swap is its own inverse and exchanges handedness, so nothing is negated and integers stay integers. Records keep the file's order. A record index names the same voxel as before the move, and ties still go to the lower record (ADR-006).

**Everything downstream is the old quantity, swapped.** In the engine's axes:

| What | Now |
|---|---|
| The station (ADR-002's figures) | One instance of 207 × 255 × 228 at origin (−103, 0, −114). The occupied box runs from (−102, 0, −113) to (103, 255, 114), so the lowest layer rests on y = 0 |
| Orbit camera | World up +Y; forward (cos *p* cos *y*, sin *p*, cos *p* sin *y*). The default yaw and pitch, the pitch limit and the drag directions keep their values, and a heading of 0 still looks along +X |
| Sun (ADR-008) | (sin *a* cos *e*, sin *e*, −cos *a* cos *e*). The azimuth still runs from MagicaVoxel's −Y towards +X, which is the engine's −Z towards +X |
| Ambient, ground and shadow view (ADR-008) | Ambient on *N*y; the ground plane y = 0, with normal +Y; the shadow view's world up +Y |
| Explosion (ADR-009) | Gravity along −Y; contacts and rest at heights in y; the upward bias and the ground layer's lift along +Y; the jitter's height in y and its ring in x and z; the envelope from y = 0 up to its apex. The station's blast origin is (0.36, 93.96, −10.48) |

ADR-002, ADR-008 and ADR-009 keep their figures in MagicaVoxel's axes. A point (x, y, z) there is (x, z, y) here.

**The spins relabel, as NVF §12.2 foresaw.** A voxel spins about two coordinate axes that a hash picks by index, and indices 0, 1 and 2 are still X, Y and Z. So a spin that was about MagicaVoxel's Y, which is horizontal, is now about the engine's Y, which is vertical. Converting the spins exactly would mean remapping the hash's indices, and that is not done. The spins are random anyway, and every rest orientation is one of the cube's symmetries. The rest state is unchanged, and so are the centres and contacts in flight; only a flying voxel's orientation differs from before the move.

**The Normal debug view's colours change meaning.** It shows a normal *N* as ½ + ½*N*. Up is now green rather than blue, and blue is the engine's +Z, which is MagicaVoxel's +Y.

**The pinned images' edge rule gains two checks.** NVF §12.4 lets a pixel differ from its pin only where its ray meets the grown box of the voxel it now shows and misses the shrunk box of the voxel it was pinned to. It took that rule from the explosion's GPU tests. There, the voxel whose shrunk box must be missed is the twin's, which the twin found along the very ray the test casts, so missing it means grazing it. Here it is the pin, which the ray before the move found. Whether the ray the test casts is still that ray is what the test is for, and the rule does not check it. A pin the ray passes nowhere near counts as grazed, and the voxel the pixel shows is on the ray by construction. Measured by building the CPU suite with `MakeViewBasis`'s cross products in the old order, which mirrors every image: the three-quarter view then differs from its pins in 958 pixels, and the rule counts all 958 as differences on an edge. The test also requires two more things:
- the ray meets the grown box of the voxel the pixel was pinned to;
- where the pixel was pinned to no voxel, the ray misses the shrunk box of the voxel it shows.

Both are stricter than §12.4's wording, so nothing it fails can pass. With them, the mirrored image fails in 952 pixels away from any edge, and 6 still count as edges. The wording also fails one case that rounding can cause: a ray that newly grazes a voxel in front of its pin. That has not happened in any run so far.

## Figures

**Pinned before the move.** Commit db7d82c added `NeuronCoreTests/PinnedStation.h` before any axis moved. It holds, in MagicaVoxel's axes:
- the record index of the voxel each pixel shows, in four images the reference tracer made of the station. Three come from the view splat tests' cameras at 161 × 91. The fourth is the sun's view at 91 × 91: NVF §12.4 does not ask for it, but it was added because the shadow pass has a basis of its own;
- 13 voxels' ground contacts and centres at 0.25, 1, 3 and 6 seconds after the detonation;
- eight normals' lit colours, and one column of ground and sky.

A throwaway program generated it from 3c7dc2b's `NeuronCore`, built by GCC 13.3 at `-O2` for x86-64. Before the move, CI's MSVC Debug build reproduced all four images with no pixel differing (run 36378594862).

**After the move.** The tests swap the pins' cameras, normals and centres into the engine's axes and compare, under §12.4's rule with the two checks above:

| Image | Pixels pinned to a voxel | Differ on an edge, GCC 13.3 `-O2` | Differ on an edge, MSVC Debug |
|---|---|---|---|
| Three-quarter | 810 | 0 | not yet measured |
| Level from the west | 776 | 0 | not yet measured |
| From above | 725 | 0 | not yet measured |
| From the sun | 1,630 | 0 | not yet measured |

The GCC run finds no pixel that differs at all. The flights agree within 2 × 10⁻³ voxels and 10⁻⁴ seconds, and the shades within 10⁻⁵. The explosion's envelope is unchanged to the last digit: 904.4196 by 926.4196 voxels across, in x and z, and 315.36603 high, with every voxel at rest by 10.461849 seconds. That is expected: `BoundExplosion` works on each axis alone, so the swap only relabels its numbers. The GCC figures come from the CPU suite, built by GCC 13.3 at `-O2` against a stand-in for the test framework. CI's first run after the move measures the MSVC column, and the commit that sets `PIN_EDGE_LIMIT` from it records it here.

**The mirror test.** `ViewTests::ImagesAreNotMirrored` looks from (0, 0, −20) along +Z, with +Y up, at 64 × 64. A voxel at (4, 0, 0) must land in the right half and one at (0, 4, 0) in the top half. With the cross products in the old order the first lands in the left half. Four other view tests fail with it too, on their check that right × up = +forward, and so do the pinned images and one lighting test. No test that compares the GPU with its twin can see a mirror, because both sides take the same basis (NVF §12.3).

**The reader.** `VoxModelTests::ReadsOneModel` reads a voxel stored at MagicaVoxel (1, 2, 3) as (1, 3, 2), with its origin computed from the swapped `SIZE` and `_t`. `MilitaryStationTests` holds the station's lowest layer at y = 0.

## What this forecloses

- A second converter in C++. Nothing after the `.vox` reader knows MagicaVoxel's axes, and another format that stores other axes converts in its own reader.
- A Z-up constant, or a vertical taken as z, anywhere in the engine or the game.
- Negating an axis to convert. The swap exchanges handedness on its own.
- Regenerating the pins. A difference outside the edge rule is investigated, never re-pinned.
