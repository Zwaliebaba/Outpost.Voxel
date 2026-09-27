# ADR-006 — Depth conventions

**Status:** accepted, 2026-09-27 · **Lands with:** M2 of [`Design/SampleRenderer.md`](../SampleRenderer.md) (§15)

## Context

Design §7.5 fixes the conventions: the view uses reversed-Z with an infinite far plane and depth = *n* / view depth, and the shadow map standard Z. Depth conventions are the classic place for a sign error, so §7.5 also asks that each be named once, by a helper per view that says which way "nearer" points, and that nothing compare raw depths outside those helpers. M2 builds the view splat, the first code that clears, tests and writes depth; this records how.

## Decision

**The view's two facts live in `NeuronCore/PerspectiveView.h`.** `PERSPECTIVE_FAR_DEPTH` is 0, the far plane at infinity and the value a view is cleared to. `IsNearerPerspectiveDepth(a, b)` is `a > b`. `ViewTargets` clears the depth buffer to the one. `ViewSplatPass` tests with `D3D12_COMPARISON_FUNC_GREATER` beside a `static_assert` on the other, so neither can change alone. `PerspectiveDepth` is the one place *n* / *t* is written; *n* is 0.1.

**The splat writes conservative depth.** The rectangle sits at the voxel's nearest point, so the hit can only be farther, and in reversed-Z farther is smaller. The pixel shader writes `SV_DepthLessEqual` = min(*n* / *t*, `SV_Position.z`); the `min` keeps a rounding error from breaking the promise the semantic makes. The depth buffer is `D32_FLOAT` on an `R32_TYPELESS` resource, so the lighting can read it as `R32_FLOAT` in M3.

**Ties go to the lower record.** `GREATER` fails on equal depths, so the first voxel drawn at a depth keeps it, and a draw covers its records in order. That is the reference tracer's tie rule, and what makes watertight seams deterministic (§4.2, item 12).

**The visibility buffer is cleared as a UAV.** `ClearRenderTargetView` takes floats, and no float converts exactly to `NO_VOXEL`, 0xFFFFFFFF. The conversion rules say a value out of range clamps to the maximum, but Microsoft's documentation tells an application that wants an exact integer pattern not to rely on the clear. A background that came out as voxel 0 would be a visible defect on one GPU and not another. So the visibility texture also allows unordered access, and `ClearUnorderedAccessViewUint` writes the words exactly. It costs a second descriptor for the clear in a CPU-only heap, and two transitions a frame.

**The shadow map is M3's** and follows the same pattern in `OrthographicView.h`: standard Z, cleared to 1, and a `LESS` test tied to its own helper.

## What this forecloses

- Comparing depths anywhere but through the helpers, or clearing a depth buffer to a literal.
- A finite far plane in the view.
- Clearing the visibility buffer with `ClearRenderTargetView`.
