# ADR-021 — The sky, and the lighting from the world

**Status:** accepted, 2026-09-28 · **Lands with:** S-M5 of [`Design/SpaceScene.md`](../SpaceScene.md) (§11, §12.1, §15) · **Supersedes:** [ADR-008](ADR-008-lighting-from-the-file.md), but for its emissive mapping and `SunDirection`'s convention · **Amends:** [ADR-018](ADR-018-client.md), whose client kept the station's lighting until now, and [ADR-017](ADR-017-sector.md), whose placeholder settings become the defaults · **Refines:** SpaceScene §11.3, on the dust's lanes

## Context

S-M5 draws the sky of §11 and lights the space scene from the welcome, as §12.1 asks, where until now the client lit it with the station file's settings (ADR-008, ADR-018). The design fixed the sky's structure: a catalog of stars generated from the world's sky seed and drawn as sprites with a point-spread function; a galaxy and a sun evaluated per pixel; one pass at the far plane. It left most of the functions' forms and every default to the build, tuned by eye. This records them, and what the build settled on the way.

The owner made two decisions while it was built, on 2026-09-28:
- **S-M5 runs before the game concept's slice.** The concept, accepted the same day, had deferred S-M5 to after its slice (GameConcept G22). The owner chose to finish S-M5 first. S-M6 and S-M7 still wait.
- **The sky uses no textures.** The owner added two sprites, `GameData/Textures/Glow.dds` and `Starburst.dds`, and asked whether the sky needs textures. It does not, by §11.1. The sprites are 128 × 128 BGRA8, white with their shape in alpha, and without mips. They are a lens glow and diffraction spikes, which §12.2 rules out: bloom is the frame's only glow. Using them would also need a DDS reader (D9 rules out a library), mips, and a twin (R15). The owner kept the design. The two files stay in `GameData`, and no code reads them.

## Decision

**The lighting comes from the world.**
- `MakeLightingParameters(WorldSettings, gain)` takes the welcome's sun, direction and irradiance, and its hemisphere's two colors as they are, at an intensity of 1, over a black background that the sky then covers.
- **The defaults are the sector's.** ADR-017's settings, placeholders until now, already are §12.1's defaults: the sun at 50°, 50°, with a radiance of 0.7 and an angular radius of 0.27°, and an ambient of 0.05 above and below. S-M5 keeps them.
- `RenderSettings`, `ReadRenderSettings` and their tests go. The `.vox` reader still keeps the `rOBJ` chunks verbatim, and `KeepsItsRenderObjects` checks that it does.
- ADR-008's emissive mapping, `_emit` × 2^`_flux` × the gain, is unchanged, and so is `SunDirection`'s convention, through which the sector places its sun.
- The exposure stays the client's, 1, the station file's `_film _expo` (`GameLib::EXPOSURE`).
- The tests that pinned the station's lighting keep its values explicitly: `StationLighting` in `NeuronCoreTests/PinnedStation.h`, and every pin passes unchanged. The WARP tests light with `TestWorld`, the same lighting in the world's terms.

**The catalog** (`NeuronCore/StarCatalog.h`) runs only on the CPU.
- **Count and magnitudes.** 20,000 stars by default. The number brighter than magnitude *m* is 4 × 10^(0.45 (*m* + 1)), which 20,000 stars follow down to magnitude 7.22. No star is brighter than −1.5, where the real sky's brightest stands.
- **Directions.** Each star belongs to the disk with a probability of 0.6 × *f*², where *f* is its faintness: 0 at magnitude −1.5 and 1 at the faintest. Every other star falls uniformly over the sphere.
  - The disk's sine of latitude is exponential, with a scale of 0.12.
  - 40 % of the disk's stars gather about the core, with a normal spread of 0.9 radians in longitude.
  - Measured on seed 2 by `GathersFaintStarsTowardThePlane`: 23 % of the stars brighter than magnitude 3 lie within 0.2 of the plane in sine, against 20 % for a uniform sky, and 53 % of those fainter than 6.
- **Temperatures.** 80 % are log-normal about 5,200 K, with a spread of 0.18 in the logarithm. The rest are log-uniform over the whole range, 2,500–30,000 K.
- **Colors.** Planck's spectrum is sampled every 5 nm from 380 to 780 nm and weighted by Wyman, Sloan and Shirley's multi-lobe fit to the CIE 1931 functions. The result goes to linear Rec. 709 through Lindbloom's D65 matrix, is normalized to unit luminance, and is moved toward white by a saturation of 0.35.
- **Randomness.** Each draw takes 24 bits of `PcgHash(PcgHash(index × 11 + stream) + seed)`, so the same seed gives the same bytes.
- **Frame.** The directions are made in the galaxy's frame, whose +Y is its north pole and +X its core, and turned into the world by the welcome's galactic plane.
- **Record.** `StarRecord` holds a direction, a flux and a color in 28 bytes, as a structured buffer packs them. It is shared with HLSL under R16.

**The point-spread function** (`NeuronCore/Sky.h`, twin of `Shader/Sky.hlsli`):
- **Shape.** A Gaussian with σ = 0.7 pixels. Its share of a pixel along one axis is ½ (erf(upper) − erf(lower)), written from the tails wherever both edges lie on one side of the star. There the two error functions are nearly equal, and their difference would cancel.
- **erfc.** Abramowitz and Stegun's 7.1.26 in both languages. In single precision its worst error is 6.3e-7 over [0, 6.4], and 5.4e-7 with fused multiply-adds, measured by a throwaway program in steps of a millionth.
- **Gain.** `STAR_GAIN` is 12, tuned by eye on renderings of the twins. At that gain a star of magnitude 5 peaks at about 20 of 255 in sRGB, by the tone map's arithmetic, and fainter ones fade toward nothing.
- **Threshold.** `STAR_DARKEST_VISIBLE` is 0.004: below about 0.0044, the ACES fit's toe gives an sRGB byte of zero at an exposure of 1.
- **Quad.** A star's quad reaches r = ½ + σ √(2 ln(*b* / 0.004)) for brightness *b*. Every pixel outside it receives less than the threshold; the bound is in `Sky.h`. A star whose brightest pixel stays below the threshold is not drawn. At gain 12 every star of the default catalog is drawn: the faintest peaks at 0.0043.
- **Tests.** Summed over its pixels, a star's shares come to one wherever it falls in its pixel. `QuadHoldsEveryVisiblePixel` holds the bound over a sweep of brightnesses and positions.

**The galaxy** is a function of the direction alone, in the galaxy's frame, so it has no seam anywhere. Its constants were tuned by eye on panoramas and 1080p views rendered from the twins; the owner judges them on hardware (§12.1).
- **The disk.** An exponential in the sine of the latitude. Its scale grows from 0.05 at the anticentre to 0.10 at the core, and its brightness from 0.35 to 1, both linear in (1 + *x*) / 2. Its color is (1, 0.97, 0.92).
- **The bulge.** exp(−60 ((1 − *x*) + 2 *y*²)), in (1, 0.85, 0.66).
- **The star clouds.** The disk times e^(±1.0 (2 fbm − 1)): five octaves of value noise at frequency 12, stretched 2.5 times across the plane.
- **The dust.** An optical depth of 1.5 × exp(−|*y* − 0.01| / 0.03). It is denser toward the core, from 0.4 to 1, and shaped into lanes by clamping (fbm − 0.45) / 0.25: five octaves at frequency 10, stretched 7 times across the plane. It reddens by (1, 1.35, 1.8).
- **Clumps, not ridges.** §11.3 shapes the lanes with a ridged noise, and the first cut did: five octaves, each folded about its middle and squared. On the twins' renderings its dust read as one even lane along the plane. A threshold leaves dust only where the noise is high, with clear gaps between, and the stretch across the plane draws those clumps out into lanes.
- **Where noise runs.** Only within 0.6 of the plane in sine, fading over the outer 0.2 of that. Beyond it the disk is too faint for clouds or dust to show, and the pixel pays for no noise.
- **Noise.** Value noise over a lattice hashed with `PcgHash`, chained over *x*, *y*, *z* and the seed, 24 bits a value. Each octave's lattice is shifted by (0.37, 0.61, 0.83). The dust's seed is offset by 0x9E3779B9.
- **Gain.** `GALAXY_GAIN` is 0.06.

**The sun** is the welcome's disc, limb-darkened with *u* = 0.6.
- Its radiance at the middle is *E* / (π *R*² (1 − *u* / 3)), so that the disc gives the lighting's irradiance *E*. `SunGivesTheLightingsIrradiance` integrates the twin's disc to 0.7 within one per cent.
- The angle from the disc's middle is taken as the chord between the two unit vectors, |*d* − *s*|. Within a pixel of the disc the two differ by less than a millionth of the angle, θ² / 24, and the chord keeps its precision near the sun, where an angle from the cosine would not.
- **No arcsine.** The first cut took the angle as 2 asin(|*d* − *s*| / 2). WARP's arcsine behaves like Abramowitz and Stegun's 4.4.45, the approximation Cg's reference arcsine uses, whose error near zero is 6 × 10⁻⁵ radians: 1.3 % of the angle at the disc's rim, where a pixel's value moves by the disc's whole radiance over one pixel's angle. The second WARP run failed there, at 932 against the twin's 996.5, and the twin with that approximation gives 932 bit for bit.
- The edge is antialiased over one pixel's angle at the middle of the view.

**The pass** (`NeuronClient/SkyPass`) runs after the lighting and before bloom.
- **Targets.** The HDR color gains a render-target view, and the view's depth a read-only depth-stencil view. The depth sits in `DEPTH_READ` together with its shader-resource states while it is bound (`ViewTargets::BeginSky`). The HDR color now rests readable by every stage between passes, since bloom reads it in compute.
- **Draws.** One triangle, `FullScreen.hlsli`'s at z = 0, then one instanced strip of four corners a star. A star that is not drawn puts its corners on one point outside the view.
- **Depth test.** Both draws test `EQUAL` against the far plane and write no depth. A `static_assert` ties the test to `IsFarPerspectiveDepth` in `PerspectiveView.h`, beside ADR-006's helpers (§9).
- **Blending.** The stars blend ONE + ONE into red, green and blue, and leave alpha alone.
- **Stores.** The HDR color holds halves, and Direct3D rounds what it stores toward zero (ADR-022). The triangle stores a pixel's color once, and every star that adds to it stores it again, so the twin rounds as often, in the order the GPU draws.
- **Data.** The catalog is copied to the GPU once, through `RendererDesc::stars`. The sky's parameters travel every frame in `FrameSettings::sky`, as `SkyConstants`, which is shared with HLSL under R16.
- **Timing.** `GpuPass::Sky` times the pass, and the bench's CSV and summary carry it.
- **WARP test.** `SkyPassTests` draws the station's view, 161 × 91, over the depth its own view splat wrote: the sun near a corner, the galaxy's core across the view, the catalog, a star in the open and a brighter one the station hides. It compares every pixel with the twins. Within 1/128 of a pixel of a quad's edge, where the rasterizer's snapping decides whether the star reaches the pixel, what the star adds is allowed besides. The first run failed at (7, 0), up to four steps under a twin that rounded to the nearest; after five stars, the truncating twin gives that pixel bit for bit. It names up to eight pixels beyond its bounds rather than stopping at the first, and logs how many sky pixels away from a quad's edge match the twins to the bit, and the most halves any other strays.
- **Measured.** On the run of 74d2f64, the first with the truncating twin and the chord, the view held 13,841 sky pixels: 12,105 lit by stars, 5 by the sun, and 49 hiding the bright star. Away from a quad's edge, 13,082 matched the twins to the bit and none strayed more than one half step. The bounds, four steps and a floor for the faintest galaxy, stay as headroom. The test writes the color under the sky before the pass rather than clearing the target to it, since the debug layer warns about a clear without the clear value the target was made with, and any warning fails a WARP test; the renderer never clears the HDR color, whose every pixel the lighting writes.

## Consequences

- **Glow.** No star and no sun draws a glow of its own. A lens effect, spikes included, would be a new decision against §12.2, with its own ADR.
- **Seeds.** A world's sky is a function of its welcome: two clients of one build see the same sky. Two builds may round the galaxy's noise differently in the last bits; the sky is cosmetic.
- **Defaults.** The tuned values are the implementer's, from the twins' renderings rather than hardware. Each lives twice, in `Sky.cpp` and `Sky.hlsli`, and the WARP test catches the two drifting apart. The owner's judgement at S-M5, and in S-M6 with anti-aliasing (§12.1), may change them.
- **Cost.** The galaxy's cost falls on the pixels within 0.6 of the plane in sine. The pass is timed on its own, and the bench (S-M8) says what it costs.
