# ADR-025 — The light of a detonation: hot debris, a flash and a shell of gas

**Status:** proposed, 2026-09-29: built and tested on the CPU, awaiting CI's WARP run and the owner's eye on hardware · **Lands with:** a follow-up to [ADR-024](ADR-024-fragmented-detonation.md) · **Amends:** [`Design/Archive/SampleRenderer.md`](../Archive/SampleRenderer.md)'s exclusion of transparency, for emission alone; the lighting pass's inputs; and the frame's passes · **Amended by:** [ADR-035](ADR-035-combat.md), whose detonation that has lost voxels blasts from what it has left, sized by it

## Context

ADR-024 breaks a detonated model into fragments that fly, tumble and settle. The owner found that "much better". What the detonation still lacks is light: in the image, a reactor failure is a silent rearrangement of cold voxels.

On 2026-09-29 the owner chose from three options:
- **Built now:**
  - debris that glows while it is hot;
  - a flash;
  - a shell of hot gas.
- **Not built:** smoke. It would absorb light and so need ordering, and it would linger. It is left out.
- **The flash casts no shadow.** A second shadow map per flash is a cost left for later.

Space shapes the look:
- **No air:** no fireball burns, no smoke rises, and no blast wave travels.
- **What there is:**
  - a flash;
  - incandescent debris that cools;
  - a shell of hot gas and plasma that races outward, glows, thins and is gone within a second or two.

The shell is the "pressure dome" the owner asked about. It is the one see-through thing the renderer has drawn. SampleRenderer ruled transparency out, because every pass assumed a pixel shows one opaque voxel. A shell that only gives off light and absorbs none needs no order among shells and blocks nothing behind it. It can be added over the lit image, so the exclusion falls for emission alone. Anything that absorbs, smoke or dust, still needs its own decision.

D3 stands. Every term is a pure function of the event and the time since it:
- the heat of a fragment also depends on its pivot and size;
- the flash and the shell also depend on the model's radius and the event's seed.

The server knows none of it, and a client that joins late sees the same light. R15 holds: every GPU algorithm has its C++ twin in `NeuronCore/Blast.h`.

## Decision

**What a frame lights.** The client turns each detonated entity into a `Blast` (`SceneModels::Blast`):
- its origin, the mean of its model's voxels in the world;
- its drift, the inherited velocity over the lone voxel's drag, so that the light moves with the debris;
- its model's radius, which the flash and the shell scale with;
- the event's seed;
- the time since the event.

`FrameSettings::blasts` carries the frame's list. At most `MAX_LIT_BLASTS`, 8, are lit, the nearest the camera: a flash for each still flashing and a shell for each still glowing.

**Hot debris** (in the lighting pass).
- **Heating.** A fragment is cold until the blast reaches its pivot, at ADR-024's shock speed. Then its heat is 1 / (1 + (*d* / `heatDistance`)²), where *d* is the pivot's distance from the blast origin.
- **Cooling.** It cools as e^(−`coolingRate` × *s* × *t*), where *s* is ADR-024's size scale. A lone voxel cools fastest, and a large chunk glows for many seconds.
- **The ramp.** Heat 0 to 1 maps onto a black body from 900 K to 6,500 K at unit luminance: the 16 steps of the ramp, interpolated. Channels below Rec. 709 are clamped to zero, and a new `BlackBodyColor` shares the stars' colour integration.
- **Radiance.** The glow is the ramp's colour × `heatGain` × heat², independent of the voxel's albedo, since incandescence is not reflected light.
- **What the pass reads.** Each placement's heat, as `PlacementHeat`: the detonation's origin in its part's space, the time, the shock speed, the heat parameters and the model's first fragment. It also reads the scene's fragment buffers of ADR-024.

**The flash** (in the lighting pass). A point light at the blast's centre, with no shadow.
- **Intensity.** `FLASH_IRRADIANCE` × radius² × e^(−*t* / `FLASH_SECONDS`), so that its irradiance at the model's radius is `FLASH_IRRADIANCE` at its peak.
- **Colour.** The ramp's colour at the same fading share, from white to red.
- **Softness.** It stops growing within a softness of 0.15 × the radius.
- **Why no albedo-free glow.** It lights every voxel that faces it, the entity's own and its neighbours'. It adds nothing to the background: the shell stands in for the flash's glow in space.

**The gas shell** (a new pass: `GasShellPass`, `Shader/GasShellCS.hlsl`, after the sky and before bloom).
- **Growth.** A sphere about the blast's centre whose peak radius grows as `SHELL_REACH` × radius × (1 − e^(−*t* / `SHELL_GROWTH_SECONDS`)). The first frames race outward and it slows towards twice the model's radius.
- **Density.** The density falls as e^(−(*r* − peak)² / thickness²), with a thickness of 0.12 × the peak radius + 1 voxel. It is modulated by two octaves of value noise, hashed from the event's seed (`ShellNoise`, mean near 1). The noise's cells scale with the shell, so the filaments grow with it.
- **Emission.** The ramp's colour at e^(−*t* / `SHELL_FADE_SECONDS`), times `SHELL_GAIN`, normalized so that a path straight through the peak gives `SHELL_GAIN` at the start. A path grazing the limb is longer, so the shell's rim is brightest: the dome.
- **Integration.** 24 samples at the middles of equal steps across where the ray meets the sphere, three thicknesses past the peak. The ray stops at the depth the view splat wrote, so the station hides the shell's far side.
- **Where the ray meets the sphere.** The intersection is taken from the ray's point nearest the centre (Haines et al., *Ray Tracing Gems*, ch. 7), not from b² − ac. That form cancels for a small shell far off, and a GPU that fused a multiply-add would round it apart from the twin.
- **Additive.** The pass reads and writes the HDR colour as a UAV, adding each shell's light. It is recorded only when a shell is lit, and timed as `GpuPass::GasShell`. The bench's CSV and summary gain its column.

**The defaults**, a first cut for the owner's eye:

| Parameter | Value | Meaning |
|---|---|---|
| `heatDistance` | 0.3 × the model's radius, at least 2 | A fragment this far out starts at half heat |
| `coolingRate` | 0.8 per second | A lone voxel's; a fragment's is this × its size scale |
| `heatGain` | 6 | Radiance at white heat |
| `FLASH_SECONDS` | 0.15 | The flash fades as e^(−*t* / 0.15 s) |
| `FLASH_IRRADIANCE` | 4 | At the model's radius, at the peak; the sector's sun is 0.93 |
| `SHELL_REACH` | 2 | Radii the shell's peak grows towards |
| `SHELL_GROWTH_SECONDS` | 0.4 | |
| `SHELL_FADE_SECONDS` | 0.5 | |
| `SHELL_GAIN` | 3 | Radiance of a path straight through the peak, at the start |
| A light is dropped | below 1/1000 of its peak | The flash by 1.04 s, the shell by 3.45 s |

**The GPU's inputs** (R16: each C++ struct in `NeuronCore/Blast.h` is the truth, with a mirror and the layout echo).
- **`BlastLighting`,** 528 bytes, a constant buffer of the lighting pass at b3: the 16 ramp colours as `float4`, 8 `Flash`es of 32 bytes, the flash count and the heat gain.
- **`GasShells`,** 400 bytes, the gas shell pass's constant buffer: 8 `GasShell`s of 48 bytes and a count.
- **`PlacementHeat`,** 32 bytes a placement, a structured buffer parallel to the placements in the frame's ring. A whole placement's is zero, whose time 0 heats nothing.
- **The lighting pass's root signature** gains the constant buffer at b3 and root SRVs at t6 (the heat), t7 and t8 (ADR-024's fragment buffers).
- **The lighting pass** finds a pixel's placement itself rather than through `FindVoxel`, so that it has the placement's heat.
- **The frame's fixed constants** grow from four aligned pieces to nine.

**Measured**, by a throwaway probe of the twin, built with GCC 13.3 at `-O2` for x86-64:

| Model | Radius | Heat distance | Shell's peak at 0.15 / 0.5 / 1 / 3 s | Straight through at 0.05 / 0.3 / 1 s |
|---|---|---|---|---|
| Station | 199.1 | 59.7 | 124.5 / 284.1 / 365.5 / 398.0 | 2.66 / 1.52 / 0.22 |
| Capital ship | 45.8 | 13.7 | 28.7 / 65.4 / 84.1 / 91.6 | the same |
| Frigate | 22.1 | 6.6 | 13.9 / 31.6 / 40.7 / 44.3 | the same |

- **The flash's irradiance at the model's radius:** 2.75 at 0.05 s, 1.24 at 0.15 s, 0.29 at 0.3 s and 0.03 at 0.5 s, against the sun's 0.93.
- **The twin's cost:** 1.5 µs a pixel for a shell over the whole view, single-threaded, 88 ms for 320 × 180. That is for scale only. The GPU's cost is what matters, and the bench's `gas shells` column will measure it. It is the first number to watch at gate 4, though the shell is gone within 3.5 s of each detonation.

**Verification in this change.**
- **`NeuronCoreTests`,** 219 with the new `BlastTests`, pass under GCC 13.3. They cover:
  - the ramp;
  - heating at the blast's arrival and cooling by size;
  - the flash's inverse square, facing and fading;
  - the nearest eight kept;
  - the pixel's sum;
  - the noise's range, mean, smoothness and seed;
  - the shell's growth and fading, its brighter limb, and a voxel hiding what lies behind it.
- **Every shader** compiles with dxc for Linux at Shader Model 6.7, warnings as errors.
- **clang-tidy 18** finds nothing in the changed NeuronCore sources.
- **Not run here:** the MSVC build and the WARP suites, which CI runs:
  - `LightingPassTests`, whole and now with hot debris and a flash at 0.3 s and 3 s;
  - `GasShellPassTests`, two shells over the lit station against `GasShellPixel`;
  - the layout echo of the three new structs.
- **Not run by anyone:** Release, ARM64, and a look on hardware.

## Not built here

- **Smoke and dust.** Anything that absorbs light needs order, sun lighting and a cost that lingers, and is its own decision.
- **The flash's shadow,** a second shadow map per flash.
- **A shock ring,** a stylized ring at the shell's front. The shell's limb brightening is what stands for the dome.
- **Blasting from the reactor.** Every term is centred where ADR-024 centres the blast, the model's centroid.

## What this forecloses

- A detonation's light that depends on anything but the event, the time, the model's radius and, for heat, its fragments. It stays a pure function, computed alike on every client.
- A see-through pass that absorbs, under this decision. The gas shell pass only adds light.
- More than `MAX_LIT_BLASTS` lit detonations a frame, without a change to this ADR and the constant buffers' sizes.
- A different default, or a different construction, without a change to this ADR.
