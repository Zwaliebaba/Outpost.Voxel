# ADR-008 — Lighting read from the file

**Status:** accepted, 2026-09-27 · **Lands with:** M3 of [`Design/Archive/SampleRenderer.md`](../Archive/SampleRenderer.md) (§15) · **Amended by:** [ADR-011](ADR-011-engine-axes.md), the engine's axes · **Note:** the owner revised D4 on 2026-09-28, and it no longer leaves bloom, image-based light or anything else out of the look; the file's settings for them are still not read, since bloom's parameters are the renderer's own ([`SpaceScene.md`](../SpaceScene.md) §12.2)

## Context

M3 lights the station with what its file asks for: the `rOBJ` chunks of §3 carry a sun, a sky, a ground, a background and an exposure, and palette entries 10, 15 and 16 are emissive. MagicaVoxel documents none of it. The order of `_angle`'s two values, the azimuth's zero direction and sense, the units of `_expo`, and how `_emit` and `_flux` become light are all unwritten, and design D12 rules out settling them against MagicaVoxel renders. §16 therefore asks that each be a parameter with a stated default, and §7.2 that the emissive mapping be the sample's own, tuned by eye. This records those defaults, and what a file that says something else gets.

## Decision

**One function reads the settings, and everything downstream takes its result.** `NeuronCore::ReadRenderSettings` turns the model's render objects into a `RenderSettings`, in the renderer's terms: angles in radians, colors linear. It reads these keys and nothing else:

| Object | Key | Becomes |
|---|---|---|
| `_inf` | `_angle` | The sun's elevation, then its azimuth, in degrees |
| `_inf` | `_i`, `_k` | The sun's intensity and color: *E*sun = `_i` × color |
| `_uni` | `_i`, `_k` | The ambient's intensity and its upper color |
| `_setting` | `_ground` | Whether the ground plane is shown; G toggles it at run time |
| `_ground` | `_color` | The ground's albedo, and the ambient's lower color |
| `_bg` | `_color` | What a ray that meets neither a voxel nor the ground sees |
| `_film` | `_expo` | The exposure, a multiplier applied before the tone map |

Colors are three sRGB bytes, converted with the exact curve the palette uses (§7.2).

**A value that cannot be read keeps the station's.** Each key falls back on its own to `DefaultRenderSettings()`, which holds the values §3 measured: sun at 50°, 50°, intensity 0.7, white; sky 0.7, white; ground shown, 80 80 80; background black; exposure 1. The key may be missing, not a number, not finite, a negative intensity or exposure, or a color byte outside 0–255 or not whole. The `.vox` reader refuses geometry it does not understand by name (§7.1). A lighting value is not geometry, and one the reader cannot parse should not stop a model from being looked at.

**`_angle` is (elevation, azimuth), and the azimuth runs from −Y towards +X.** The direction towards the sun is (sin *a* cos *e*, −cos *a* cos *e*, sin *e*): at azimuth 0 the sun stands on the model's −Y side, and at 90° on its +X side. This is an assumption, and nothing in the file or MagicaVoxel's documentation settles it. At the station's 50°, 50° the order of the two values is moot, but the zero direction and the sense are not. If they are wrong, the sun comes from another side of the station, and fixing that means changing `SunDirection` and this paragraph.

**`_expo` is a multiplier.** The station's value, 1, then leaves the image as the tone map makes it. Read as stops, it would double it.

**An emissive entry's scale is `_emit` × 2^`_flux`, times a gain the viewer sets.** For the station's entries that is 0.6 × 2² = 2.4, from the values §3 measured. An entry whose `_type` is not `_emit` has none. The gain starts at 1. `[` and `]` divide or multiply it by 1.1, within 0.01 and 100. The window's title shows the brightest entry's scale times the gain, which is 2.40 at start for the station. The owner tunes it by eye while accepting M3's look (§15). The value accepted becomes the default, recorded here in the commit that sets it. The owner accepted M3's look on 2026-09-28 with the gain at 1, so 1 stays the default.

**The ground is the plane z = 0, and it is lit like a voxel.** Its height is MagicaVoxel's ground height, where §3's placement puts the station's lowest layer. It uses the same formula as a voxel, without emission, with the normal +Z. The shadow map's box is grown down to z = 0, so that the station's shadow on the ground falls inside the map. Beyond the map's 1,024-unit square the ground is lit.

**These are written in the engine's axes of today, +Z up (§7.5).** The Direct3D axes of `Design/NeuronVoxelFormat.md` §12 are not in place yet. When they land, that move carries four things from here into its table (§12.2): `SunDirection`'s formula, the ambient's *N*z, the ground plane z = 0, and the shadow view's world up in `MakeShadowView`.

**Not read:** `_inf` `_area`, since the sun's size is not modelled (§10); `_film`'s ACES switch and gamma, since the tone map is always the ACES fit through an sRGB view (§11); `_lens`, since the camera has its own; and `_ibl`, bloom, fog and atmosphere, which D4 leaves out.

## What this forecloses

- Reading a render setting anywhere but `ReadRenderSettings`, or a consumer that takes the file's strings rather than `RenderSettings`.
- Refusing a model because of a lighting value.
- A different emissive mapping, azimuth convention or exposure unit without a change to this ADR.
