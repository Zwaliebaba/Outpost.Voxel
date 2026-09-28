# ADR-012 — ARM64 as a second platform

**Status:** accepted, 2026-09-28 · **Amends:** [ADR-001](ADR-001-repository-layout.md), which built x64 only and made ARM64 a new ADR · **Rule:** `AGENTS.md` §3

## Context

The tree was x64 only (ADR-001, `AGENTS.md` §3), and `Build/CheckProjectFiles.py` failed anything else. The owner works on a Snapdragon X laptop, whose Qualcomm Adreno X1-85 ran M5's measured bench ([`SampleRendererPerformance.md`](../SampleRendererPerformance.md)). There, an x64 build runs only under Windows' x64 emulation.

The owner added `Debug|ARM64` and `Release|ARM64` to every project and to the solution in 52d4db7, built Release for ARM64, and ran it. The checker then failed `main`'s CI with 25 findings, one rule's worth: x64 is the only platform. The owner decided to keep ARM64 as a valid platform, with CI building x64 only.

## Decision

**x64 and ARM64 are the platforms.** Every project and the solution have Debug and Release on each, and no other configuration. Output lands in `ARM64\<Configuration>\` beside `x64\<Configuration>\`, and neither is committed.

**A configuration reads the same on both platforms, except for the instruction set.** x64 states `/arch:AVX2` (`AdvancedVectorExtensions2`), as ADR-001 did. ARM64 states `/arch:armv8.7` (`CPUExtensionRequirementsARMv87`), which the owner chose in 52d4db7.

The compiler may then use any instruction up to Armv8.7-A, so an older ARM64 CPU can meet one it does not have, as a pre-Haswell CPU can with AVX2. Microsoft's `/arch` (ARM64) documentation notes that the compiler does not yet generate every feature a level allows. So the floor is what the build permits, not a measured list of the instructions it uses. The owner's Snapdragon X runs it.

**CI builds and tests Debug|x64 only.** ARM64 is built by whoever runs on it, as Release is built by whoever ships. What stands in for the builds CI does not run is the static check. `Build/CheckProjectFiles.py` requires the four configurations, each platform's instruction set, and the two alignments of `AGENTS.md` §3:
- Debug against Release on each platform;
- x64 against ARM64 in each configuration, where only `EnableEnhancedInstructionSet` may differ.

When it landed, 52d4db7's configurations passed it as they stood. Every setting matched between the platforms except the instruction set, in all eight projects and both configurations.

**`Build/RunClangTidy.py` stays on x64.** It reads each project's `<Configuration>|x64` settings, as CI does.

## What this forecloses

- A third platform, or a configuration other than Debug and Release, without an ADR.
- A setting that differs between the platforms other than the instruction set, and a floor raised or lowered on either platform without an ADR.
- Code that builds or behaves correctly on one platform only: an intrinsic without the other platform's path, or code that relies on x64's stronger memory ordering. CI will not catch such code on ARM64, so whoever writes it builds ARM64 and says so (`AGENTS.md` §6).
- Treating an ARM64 test failure as a platform quirk. The suites run on x64 in CI, and their tolerances were set there. An ARM64 run that fails is a finding, as it would be on x64.
