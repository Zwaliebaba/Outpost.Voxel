# Sample renderer — measured performance

**Status:** measured 2026-09-28 · **Milestone:** M5 of [`SampleRenderer.md`](SampleRenderer.md) (§15) · **Run:** `Outpost.exe --bench 20` on a Qualcomm Adreno X1-85

M5 is done when a measured performance note exists, with an ADR for any decision it drives. This is that note. It records one run on one GPU, set frame by frame against the CPU twin. It drives no ADR, and it corrects one sentence of the design.

## What was run

The owner ran `Outpost.exe --bench 20` (§13) on a Snapdragon X laptop whose GPU reports itself as "Qualcomm(R) Adreno(TM) X1-85 GPU". The build was Release for ARM64, from the axis move's tree (cfe439d, merged as cd11925) with the ARM64 configurations that 52d4db7 then committed. The GPU driver's version was not recorded.

The bench draws 1,200 timeline frames at 1920 × 1080 with vsync off, after 120 warm-up frames. Each frame is drawn twice, back to back, once with conservative depth and once with plain `SV_Depth`. The station stands intact for frames 0 to 299, explodes over frames 300 to 899, and lies at rest from frame 900.

A throwaway program built from the same tree's `NeuronCore` replayed the timeline on the CPU. For every frame it counted:
- the visible rectangles;
- the pixel centres inside them, the fragments that plain depth shades;
- the 2×2 quads each rectangle's two triangles touch.

For every 20th frame it also replayed the draw in order and counted:
- the fragments whose ray hits their box;
- the covered pixels;
- what an ideal early depth test under conservative depth would leave, per pixel and per quad. A triangle's quad runs if any of its pixels passes the test.

## What the GPU draws

The timeline is the twin's, frame for frame: the same explosion time and camera yaw in every frame. The covered pixels are within 4 of the twin's on all 60 sampled frames, and exactly equal on 27. They also identify the tree. Against the twin of the tree before the axis move, the frames in flight differ by up to 188 pixels.

## Pass times

Median / mean / 95th percentile, over the 1,200 frames:

| Pass | Conservative depth | Plain `SV_Depth` |
|---|---|---|
| Shadow splat | 1.570 / 1.498 / 1.683 ms | 1.570 / 1.498 / 1.685 ms |
| View splat | 1.578 / 1.601 / 1.817 ms | 1.562 / 1.583 / 1.800 ms |
| Lighting | 0.678 / 0.681 / 0.844 ms | 0.679 / 0.681 / 0.844 ms |
| Tone map | 0.181 / 0.171 / 0.191 ms | 0.182 / 0.172 / 0.193 ms |
| The frame's four passes | 3.996 / 3.951 / 4.440 ms | 3.969 / 3.934 / 4.421 ms |
| CPU frame interval | 5.347 / 5.335 / 6.042 ms | 5.299 / 5.302 / 6.015 ms |

The GPU was not the limit. With two frames in flight, the interval exceeds the frame's GPU time by a median 1.3 ms, so the GPU waited, and the bench's frame rate on this machine was set on the CPU or present side. A GPU that is not kept busy may not run at its highest clock. The absolute times are whatever the GPU's clock governor chose. The comparisons within a frame's pair do not depend on it.

## What `PSInvocations` counts

With plain depth, `PSInvocations` equals the twin's count of four lanes for every 2×2 quad each triangle touches. It agrees within 0.03 % on every one of the 1,200 frames. On this GPU the counter counts quads, helper lanes included.

A rectangle covers 16 to 19 pixel centres at this framing, so the quads come to 1.92 to 1.99 lanes per fragment. About half of a rectangle's fragments hit its box: 0.51 to 0.53. That share is how tight the bounds are.

`PSInvocations` per covered pixel therefore mixes three things: the looseness of the bounds, the depth complexity and the quads. With conservative depth it is 64 intact, 13 in flight and 12 at rest. The twin's fragments per covered pixel, which leaves out the quads and early rejection, is 34 intact and 8 exploded. §14 had called `PSInvocations` against covered pixels "the tightness of the bounds in one number". On this GPU it is not, and §14 now says so.

## Early rejection under conservative depth

On the sampled frames, the medians per phase:

| Phase | Plain over conservative, measured | Ideal, per quad | Share of the ideal saving realised |
|---|---|---|---|
| Intact | 1.03 | 1.49 | 10 % |
| In flight | 1.29 | 1.30 | 98 % |
| At rest | 1.29 | 1.29 | 99 % |

The ideal is the twin's quad lanes over the lanes an ideal early depth test leaves, in draw order. Early rejection works on this GPU once the voxels separate. On the intact station it hardly works.

The ratio does not jump when the oriented permutation takes over at detonation. It climbs as the voxels separate, from 1.01 at frame 300 to 1.28 at frame 320.

A model reproduces it: the early test sees only the depth written by rectangles at least 128 draws earlier. With that one parameter, the twin's replay matches the measured ratio within 0.004 at ten frames, from intact to at rest. In the model, most of the intact station's ideal saving comes from occluders drawn fewer than 128 draws before the voxel they hide, which are its neighbours in the file. Their depth is written by the pixel shader, after the test, and has not landed when the test runs. This is a fitted model, not documented hardware behaviour.

## What the invocations cost

Nothing measurable. Within each frame's pair, plain depth shades a median 1.5 M more lanes, up to 2.65 M. Its view splat takes a median 0.983 of the conservative time (0.989 mean, 1.046 at the 95th percentile). Across frames, the view splat's time does not follow its lanes either: the correlation is −0.10.

The time does vary with other things:
- **Detonation.** From frame 300 to 301, with the debris still in place, both splats slow by 0.22 to 0.25 ms: the view splat from 1.56 to 1.81 ms, the shadow splat from 1.32 to 1.54 ms. That is what the oriented permutation costs over the aligned one.
- **The spreading debris.** It moves both times after that. The shadow splat takes 1.23 ms intact and about 1.58 ms once the debris has spread over its map.

These counters cannot separate the pose, the debris' footprint and the clock.

## What it decides

**No ADR for draw order.** The test was set before the run: sorting the intact station front to back earns an ADR if this GPU's plain-over-conservative ratio, in file order, comes close to the ideal. It does not come close: 1.03 against 1.49 per quad.

The latency model shows the test was the wrong one. Sorted front to back, occluders come long before what they hide. Under the same model, sorting cuts the intact station's lanes at frame 0 from 7.99 M to 0.66 M, twelve times fewer. But the lanes that early rejection removes cost this GPU no measurable time, so sorting would buy nothing here.

The decision holds for this GPU only. A GPU whose view splat is bound by pixel shading would reopen it, and a `--bench 20` run on such a machine would show it.

**§14 is corrected.** `PSInvocations` against covered pixels is what the GPU shaded per visible pixel, in whatever unit it counts. The tightness of the bounds is the twin's figure.

**Where the time is, if it ever matters.** On this GPU it is not in the view splat's pixel shader. The step at detonation points at the oriented permutation's vertex work. Every voxel's four vertices evaluate the pose, in both splats, which is eight evaluations per voxel per frame. Posing each voxel once per frame in a compute pass that both splats read is the first thing to measure if the explosion's frame time ever matters. That is a lead, not a decision.
