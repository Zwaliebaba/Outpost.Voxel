# ADR-002 — Voxel record, palette and what the reader accepts

**Status:** accepted, 2026-09-27 · **Lands with:** M1 of [`Design/Archive/SampleRenderer.md`](../Archive/SampleRenderer.md) (§15) · **Amended by:** [ADR-011](ADR-011-engine-axes.md), the engine's axes, and [ADR-014](ADR-014-placements.md), which gives each drawn voxel a scene-wide id

## Context

The design fixes the shape of a voxel on the GPU (§7.1, D5) and the owner fixed the palette at sixteen entries (R14). It also asks the reader to refuse, by name, every `.vox` feature it has no tested answer for (§7.1, and the owner's answer 3 in §16). M1 builds the record, the reader and the model in `NeuronCore` (then named `VoxelCore`; ADR-003), so this is where those choices become code, and where the handful of choices the design leaves open are made.

## Decision

**The record is one `uint32`.** Bits 0–23 hold the model coordinates x, y and z at eight bits each; bits 24–27 hold the palette entry minus one; bits 28–31 are zero. `PackVoxelRecord` and `UnpackVoxelRecord` in `NeuronCore/VoxelRecord.h` are `constexpr` and are the C++ half of the pair R15 requires; the HLSL half lands with the first shader in M2. The file's entries 1–16 become 0–15, so entry 0, which MagicaVoxel never draws, cannot be expressed at all.

**A model's placement travels per instance, not per voxel.** Each placed model is a `ModelInstance`: a range of the record buffer, the model's size, and its origin, the translation minus ⌊size / 2⌋ (§3). Records are stored instance after instance, in scene-graph order, and a model placed twice is stored twice: every drawn voxel has its own record index, which the visibility buffer reports and the explosion moves as its own piece.

**The palette keeps what the file says.** Sixteen `PaletteEntry` values hold the file's sRGB bytes and alpha as bytes. Converting them to linear albedo is the lighting's job (§7.2, M3), so the reader interprets no colour. Emissiveness belongs to the entry: a `MATL` whose `_type` is `_emit` marks it, and `_emit` and `_flux` are kept as numbers, because how they become radiance is also M3's to decide (D12). Materials for entries beyond 16 are ignored, since no voxel can use them.

**Render settings are kept verbatim.** Each `rOBJ` becomes a dictionary of strings, in file order. The reader does not know what `_inf` or `_lens` mean; M3 does.

**What the reader accepts.** VOX versions 150 and 200; any chunk it does not know, skipped whole with its children (this file's `META`, `NOTE` and `rCAM`, and `IMAP` or `PACK` in others); the scene graph from node 0, summing translations along the path; and hidden nodes, hidden groups and nodes on hidden layers, skipped with everything beneath them, unread.

**What it refuses, each with its own `VoxError`:**

| Error | Refused because |
|---|---|
| `NotAVoxFile` | the magic is not `VOX `, or the first chunk is not `MAIN` |
| `UnsupportedVersion` | the version is neither 150 nor 200 |
| `Truncated` | a chunk header is cut short or gives a negative size, or a chunk runs past the end of the data that holds it |
| `MalformedChunk` | a size or count disagrees with its chunk: content in `MAIN` or bytes after it, a `SIZE` without an `XYZI` or the reverse, a count the content cannot hold or does not fill, a palette that is not 256 entries, a `_t` that is not three integers, an `_emit` or `_flux` that is not a number |
| `ModelTooLarge` | a model dimension exceeds 256 |
| `VoxelOutOfBounds` | a voxel lies outside its model's `SIZE` |
| `DuplicateVoxel` | two voxels of one model share a position |
| `ColorOutOfRange` | a voxel uses entry 0 or an entry above 16 (R14) |
| `MissingPalette` | there is no `RGBA` chunk |
| `MissingSceneGraph` | there is no node 0 |
| `BadSceneGraph` | a child that does not exist, a cycle, a node reached twice, two nodes with one id, a shape naming a missing model, or nesting deeper than 64 |
| `UnsupportedRotation` | a transform's `_r` is anything but 4, the identity |
| `UnsupportedAnimation` | a transform with other than one frame, or a shape with other than one model |
| `TranslationOutOfRange` | a summed translation leaves ±2²⁰ on any axis |
| `FileNotFound`, `ReadFailed` | `LoadVoxModel` could not open or read the file |

Two of these go beyond the list in §7.1, and are decided here. Refusing bytes in or after `MAIN` is the design's "any size or count that disagrees with its chunk" applied to the outermost chunk. `TranslationOutOfRange` is new: without it, a file could overflow the integer sums of nested translations, which is undefined behavior, and beyond 2²³ a voxel centre, an integer plus a half, is no longer exact in single precision. 2²⁰ is four thousand times the extent of any one model and leaves every centre exact.

**Every rule has a test.** `NeuronCoreTests/VoxModelTests.cpp` builds a file in memory for each acceptance and each refusal above, and checks every truncation of a valid file. `NeuronCoreTests/MilitaryStationTests.cpp` loads the real asset and pins the figures of §3.

## Figures

Measured by `MilitaryStationTests`, which loads `GameData/MilitaryStation.vox` with this reader: one instance of 207 × 228 × 255 at origin (−103, −114, 0), 225,048 records (900,192 bytes as one `StructuredBuffer<uint>`), seven entries in use with the counts of §3, 12,583 emissive voxels in entries 10, 15 and 16, and an occupied world box from (−102, −113, 0) to (103, 114, 255), so the lowest layer rests on z = 0.

## What this forecloses

- More than sixteen colors, or a model coordinate above 255: either is a format change (R14) and needs its own ADR.
- Per-voxel materials: a material belongs to a palette entry.
- Rotated or mirrored nodes, animation frames and multi-model shapes. The reader refuses them; accepting any of them needs a tested answer for MagicaVoxel's pivot and rotation conventions, and an ADR.
- Interpreting color in the reader: it hands sRGB bytes on.
- Translations beyond ±2²⁰: a scene that needs them needs a different placement scheme.
