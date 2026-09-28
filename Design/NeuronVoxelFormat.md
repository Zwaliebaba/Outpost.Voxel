# Neuron Voxel Format (NVF) — Design and Implementation

**Status:** accepted by the owner, 2026-09-28, with §12.4's verification amended; the questions of §11 are answered; N-M0 to N-M3 are done and N-M4 awaits the owner's checklist; the game concept, which the owner accepted on 2026-09-28, re-plans N-M1 to N-M4 (§10) · **Date:** 2026-09-27
**Inputs:** MagicaVoxel `.vox` (the existing reader, [`SampleRenderer.md`](Archive/SampleRenderer.md) §7.1) · **Assets:** `GameData/MilitaryStation.vox`, `CapitalShip.vox`, `Frigate.vox`

This document says what NVF is, how models get into it, and how its tools are built. `AGENTS.md` says how the code is written; the engineering decisions below land as ADRs in the commits that implement them (§10).

## 1. Summary

NVF is the game's own voxel model format. A model is a small tree of rigid **parts**, each a voxel grid in the R14 record, sharing one 16-entry palette, plus named **hardpoints**: points with a free orientation that say where an engine, a weapon or a docking port is mounted. MagicaVoxel stays the voxel editor. A command-line importer, `NvfImport`, turns a `.vox` into an `.nvf`, reading parts and hardpoints from node names. A Blender extension opens an `.nvf`, shows its voxels as a locked preview, lets an artist add, move, rotate and name hardpoints and part pivots, and writes the file back with every voxel untouched.

| # | Decision | Source |
|---|---|---|
| N1 | `.vox` is the authoring format; `.nvf` is what the game loads. The game never reads `.vox`. | Owner, 2026-09-27 |
| N2 | A model is a tree of named rigid parts. Each part is one voxel grid of at most 256³, grid-aligned at rest; all parts share one 16-entry palette (R14, D5). | Owner, 2026-09-27 |
| N3 | A hardpoint has a name, a type, a parent part, a position and a free orientation, stored as a unit quaternion. | Owner, 2026-09-27 |
| N4 | Hardpoints and part pivots can be authored in MagicaVoxel as named marker models, and refined in Blender. | Owner, 2026-09-27 |
| N5 | Blender edits hardpoints and pivots only. It never changes a voxel, a colour, or the part tree. | Owner, 2026-09-27 |
| N6 | The file is little-endian and chunked, as `.vox` is: readers skip chunks they do not know. Every record is fixed-size and 16-byte aligned, so the voxel array can be copied into a GPU buffer as it lies. | §4 |
| N7 | NVF has two implementations, C++ in `NeuronCore` and Python in the Blender extension. One committed golden file proves they agree, byte for byte. | §9 |
| N8 | The model says **where** something is mounted and **what kind** it is. What it does — thrust, damage, fire arcs — is gameplay data and never enters the file. | §2 |
| N9 | NVF uses Direct3D's customary axes: left-handed, +X right, +Y up, +Z forward. Tools convert from right-handed, +Z-up spaces by swapping y and z. | Owner, 2026-09-27; §4.1 |
| N10 | The whole engine moves to the same axes, before NVF work starts. The `.vox` reader becomes the one place C++ converts MagicaVoxel's axes, so nothing downstream of it, the importer included, swaps anything. | Owner, 2026-09-27; §12 |

## 2. Scope

**In scope:** moving the engine to Direct3D's axes (§12); the binary format and its validation; its reader and writer in `NeuronCore`; the `.vox` reader changes that markers need; `NvfImport.exe`; the Blender extension; tests and CI for all of it; converting the three assets in `GameData/`.

**Out of scope, and why:**

- **Loading NVF in `Outpost.exe`.** The renderer keeps drawing `.vox` until a follow-up moves it (§10). It needs per-part transforms, which is renderer work. By then the engine already uses NVF's axes (N10, §12), so placing a part takes no conversion.
- **Animating parts.** NVF stores each part's rest translation and its pivot; turning a turret is the game's job.
- **Gameplay data** (N8), materials beyond the palette, collision, LODs, per-voxel attributes such as hit points. The chunked layout leaves room for each as a new chunk and a minor version (§4.6).
- **Editing voxels anywhere but MagicaVoxel** (N5), and importing any source format other than `.vox`.

## 3. Workflow

```
 MagicaVoxel ──► Ship.vox ──NvfImport──► Ship.nvf ◄──► Blender (hardpoints, pivots)
  voxels, parts,               │             │
  marker models                └─ --check ───┴─► CI: is the .nvf stale against its .vox?
```

1. An artist models in MagicaVoxel. Parts are separate models with path names (`hull`, `hull/turret`); markers are small models named `<part>@<hardpoint>` (§5).
2. `NvfImport Ship.vox Ship.nvf` writes the `.nvf`.
3. The artist opens `Ship.nvf` in Blender, adds or adjusts hardpoints, and exports over the same file.
4. When the voxels change, the artist edits `Ship.vox` and runs `NvfImport` again. It **keeps** every hardpoint that was authored in Blender (§6.2), so the Blender work survives a voxel edit.

**Both files are source and both are committed.** The `.vox` owns voxels, palette, parts and marker hardpoints; the `.nvf` owns hardpoints authored in Blender. `.blend` files are scratch and are never committed. `*.nvf` is marked `binary` in `.gitattributes`, and `NvfImport --dump` prints a file as text for review (§6.3).

## 4. The format

### 4.1 Conventions

- Little-endian throughout. Floats are IEEE-754 binary32 and must be finite.
- **Model space follows Direct3D's convention (N9):** left-handed, +X right, +Y up, +Z forward, one unit per voxel edge. Direct3D itself fixes no up axis; this is the one its documentation and DirectXMath's `…LH` functions customarily use.
- **Conversion from the authoring tools.** MagicaVoxel and Blender are both right-handed with +Z up. A point (x, y, z) there is (x, z, y) in NVF, and a size or an integer voxel coordinate is swapped the same way. The swap is its own inverse and exchanges handedness, so no axis is negated, and integer coordinates stay integers. A rotation matrix *R* becomes *P R P*, where *P* is the swap. There are exactly two converters. In C++ it is the `.vox` reader, for the whole engine (§12); in Python it is the Blender extension's import and export (§7). Everything else, `NvfImport` included, sees Direct3D axes only.
- **Part space** has its origin at the minimum corner of the part's voxel (0, 0, 0), with model-space axes. Voxel (x, y, z) occupies [x, x+1) × [y, y+1) × [z, z+1).
- **A hardpoint's frame:** local +Z points out of the mount — the way a weapon fires and an engine's exhaust leaves — local +Y is up, and +X is right, a left-handed frame like model space. In MagicaVoxel and Blender, that forward shows as +Y and up as +Z, so an artist sees the same arrow either way.
- Names are ASCII. A **part path** is one or more segments joined by `/`. A **hardpoint name** is two or more segments joined by `.`, and the first segment is its **type** (`engine.main`, `weapon.left`, `dock.aft`). A segment matches `[a-z0-9_]{1,31}`. `pivot` is reserved and is not a type, though a part may be named `pivot`. Hardpoint names are unique in the file, not only in their part, so the game can look one up by name.

### 4.2 Layout

```
Header (16 bytes)
Chunk: STRS  string table
Chunk: PALT  palette
Chunk: PART  part table
Chunk: VOXL  voxel records
Chunk: HPNT  hardpoints (count may be zero)
[unknown chunks: skipped by a reader of this version]
```

**Header**

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | `char[4]` | magic | `NVF ` |
| 4 | `u16` | versionMajor | 1 |
| 6 | `u16` | versionMinor | 0 |
| 8 | `u32` | chunkCount | chunks after the header |
| 12 | `u32` | reserved | 0 |

**Chunk header**, 16 bytes, followed by `sizeBytes` of content and zero padding to the next multiple of 16. Because the header and every chunk header are 16 bytes, every chunk's content starts 16-byte aligned.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | id |
| 4 | `u32` | sizeBytes — content only, padding excluded |
| 8 | `u32` | elementCount — records in the content; for `STRS`, its byte count |
| 12 | `u32` | reserved, 0 |

The five chunks of §4.3 appear exactly once each, in that order. For a table, `sizeBytes` must equal `elementCount` × the record size. A chunk a reader does not know may lie anywhere after the header: its framing is checked like any chunk's, and the reader reports its id (§4.6). A reader of 1.0 reads every minor version of major version 1.

### 4.3 Chunks

**`STRS` — strings.** Concatenated UTF-8, each NUL-terminated. Other chunks refer to a string by its byte offset. Each string is stored once. The writer lays them out in first-use order — part names in part order, then hardpoint names in hardpoint order — so two writers produce the same bytes. A reference points at a string's first byte. The whole chunk is well-formed UTF-8 and ends with a NUL; a string nothing refers to is allowed, since a newer chunk may refer to it.

**`PALT` — palette.** Exactly 16 records of 16 bytes. Entry *i* is the colour a record with colour field *i* shows (R14: the `.vox` palette entry minus one).

| Offset | Type | Field |
|---|---|---|
| 0 | `u8[4]` | red, green, blue, alpha — the file's sRGB bytes |
| 4 | `u32` | flags — bit 0 emissive; others 0 |
| 8 | `f32` | emit |
| 12 | `f32` | flux |

This is `PaletteEntry` as the `.vox` reader already produces it (SampleRenderer §7.2); the renderer's own mapping of `emit` and `flux` is unchanged.

**`PART` — parts.** 48 bytes each. Part 0 is the only root; a part's parent comes before it, so a single pass in file order builds the tree.

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | nameOffset — the full path, e.g. `hull/turret`: part 0's is one segment, every other part's its parent's path plus one |
| 4 | `u32` | parentIndex — `0xFFFFFFFF` for part 0, otherwise less than this part's index |
| 8 | `u16[3]` | sizeVoxels — 1 to 256 on each axis |
| 14 | `u16` | flags — bit 0 `PivotAuthored`: the pivot was set in Blender (§6.2); others 0 |
| 16 | `i32[3]` | translation — this part's space in its parent's, in voxels; for part 0, in model space |
| 28 | `f32[3]` | pivot — in part space: where the game rotates the part about |
| 40 | `u32` | firstVoxel — index into `VOXL` |
| 44 | `u32` | voxelCount — at least 1 |

A part's ranges in `VOXL` follow one another in part order, without gaps or overlap. Parts are translated but never rotated at rest (N2). A part's origin in model space, the sum of the translations from part 0 down to it, lies within ±2²¹ on every axis (ADR-019). The default pivot is the part's geometric centre, size / 2, taken per axis after the swap (§11, question 5).

**`VOXL` — voxels.** `u32` records exactly as R14 packs them: x, y, z in bits 0–23, the colour in bits 24–27, bits 28–31 zero. Within a part, a record lies inside `sizeVoxels`, and no position repeats. Records keep the order the `.vox` gave them, because the renderer breaks depth ties by record order (ADR-006).

**`HPNT` — hardpoints.** 48 bytes each.

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | nameOffset |
| 4 | `u32` | partIndex |
| 8 | `u32` | flags — bit 0 `FromVox`: this hardpoint came from a marker (§6.2); others 0 |
| 12 | `f32[3]` | position — in part space, voxel units |
| 24 | `f32[4]` | rotation — x, y, z, w: a unit quaternion from the hardpoint's frame to part space, with w ≥ 0 |
| 40 | `u32[2]` | reserved, 0 |

The type is not stored separately. It is the name's first segment, and the reader exposes it, so the two can never disagree. Writers store hardpoints in name order, by the bytes of their names, so that `NvfImport`'s merge and Blender's export write the same bytes for the same hardpoints (ADR-019).

### 4.4 Validation

A reader refuses by name, as the `.vox` reader does, returning `std::expected<NvfModel, NvfError>` in C++ and raising `NvfError` in Python, with the same set of names in both:

`FileNotFound`, `ReadFailed`, `WriteFailed` (opening, reading or writing a file), `NotAnNvfFile`, `UnsupportedVersion`, `Truncated`, `MalformedChunk` (bad size, count or padding, a nonzero reserved field or undefined flag bit, a count beyond the limits below), `MissingChunk`, `ChunkOutOfOrder`, `BadString` (an offset off a string start, invalid UTF-8, no final NUL, a name that breaks §4.1), `DuplicateName`, `BadPartTree` (no root, a second root, a parent not before its child, a path that is not its parent's plus one segment), `PartTooLarge`, `TranslationOutOfRange` (a part's origin beyond ±2²¹), `BadVoxelRange`, `VoxelOutOfBounds`, `DuplicateVoxel`, `ReservedBitsSet` (a voxel record's bits 28–31), `BadHardpointPart`, `NotFinite`, `NotUnitRotation` (|‖q‖ − 1| > 10⁻⁴, or w < 0).

Both readers check in the order ADR-019 fixes and report the first failure, so that a file with two faults gets the same name from each. A writer refuses, by the reader's name, a model the reader would refuse or would read back otherwise, so that whatever it writes reads back as it was written (ADR-019).

Limits, so that a bad file fails fast rather than allocating: 1,024 parts, 4,096 hardpoints, 64 KiB of strings. The voxel count is bounded by the parts' sizes.

### 4.5 Sizes, measured from the assets

Each asset is one part today. The voxel chunk is 4 bytes per voxel; everything else totals under 1 KiB.

| Asset | Model | Voxels | `VOXL` bytes |
|---|---|---|---|
| `MilitaryStation.vox` | 207 × 228 × 255 | 225,048 | 900,192 |
| `CapitalShip.vox` | 45 × 77 × 21 | 10,747 | 42,988 |
| `Frigate.vox` | 33 × 27 × 12 | 1,181 | 4,724 |

Measured on 2026-09-27 by walking the files' chunks with a throwaway script. Each has one model, no rotations, at most 16 colours, and no names. The frigate was measured again on 2026-09-28: the repair merged in 786afeb grew it from 26 deep to 27 ([`SpaceScene.md`](SpaceScene.md) §4).

### 4.6 Versioning

A minor version adds chunks, or defines flag bits a reader of an older minor version will refuse. A major version changes a record or a rule, and a reader refuses a major version it does not know. A tool that **rewrites** a file — Blender's export, `NvfImport`'s merge — refuses one that contains chunks it does not know. It cannot carry them across safely, because a newer chunk may refer to hardpoints or parts by index.

## 5. Authoring in MagicaVoxel

The importer reads meaning from **node names**, which MagicaVoxel stores as the `_name` of a transform node and shows in its outliner.

| Node name | Meaning |
|---|---|
| empty | A part named `main`, allowed only when it is the file's one and only part (all three assets today) |
| `hull`, `hull/turret`, `hull/turret/barrel` | A part. Its parent is the path without its last segment, and that part must exist. |
| `hull/turret@weapon.main` | A marker: hardpoint `weapon.main` on part `hull/turret` |
| `hull/turret@pivot` | A marker: part `hull/turret`'s pivot. At most one per part; it has no orientation. |

**A marker is a small model with an odd size on every axis**, for example 1 × 1 × 1, or 1 × 3 × 1 drawn as an arrow along MagicaVoxel's +Y, which is NVF's forward +Z (§4.1), so the artist can see which way it points. Its **centre voxel's centre** is the position. Its rotation, MagicaVoxel's `_r`, is the orientation, so an artist points a weapon by rotating its marker. Its voxels and colours are dropped. The size must be odd because, with an odd size, MagicaVoxel's pivot is the centre voxel and there is nothing ambiguous to guess. That is why the reader can accept these rotations while it keeps refusing others (§6.1).

Hidden nodes and hidden layers are skipped, as the reader already does, so a hidden marker is not imported. Parts are never rotated (N2): a rotated part is refused by name.

## 6. `NvfImport`

### 6.1 Changes to the `.vox` reader

`ParseVoxModel` gains two things and keeps every other refusal of SampleRenderer §7.1:

- **Names.** `ModelInstance` gains `name`, the `_name` of the transform node that places it.
- **Rotations on odd-sized models.** `ModelInstance` gains `rotation`: MagicaVoxel's `_r`, validated (two distinct axis indices, not a reflection) and conjugated into engine axes (§4.1), stored as one of the 24 proper axis-aligned rotations. The reader accepts a rotation other than the identity only when every dimension of the model is odd. Otherwise it still returns `UnsupportedRotation`, for the reason §7.1 gives.

The renderer never draws a rotated instance, so `VoxelScene` refuses one by name. `OccupiedBounds` places a turned model's voxels where its turn puts them, about its centre voxel, so that the server measures what a marker covers (ADR-020). The sample's behavior on the three assets does not change.

### 6.2 Conversion

The conversion lives in `NeuronCore` as `ImportVoxModel(const VoxModel&, const NvfModel* _previous) → std::expected<NvfModel, std::vector<NvfImportError>>`, so `NeuronCoreTests` exercises it. It returns every refusal it finds, each naming its node, so that an artist fixes a file in one pass (ADR-020). The executable is a thin command line around it.

1. Split instances into parts and markers by name (§5), and refuse a malformed name, a missing parent part, a duplicate, or a rotated part.
2. Parts are ordered by path, which puts every parent first. Part translation: part 0's is its instance origin, and every other part's is its origin minus its parent's. Size and records are copied as they are, in their order. The reader has already put all of them in engine axes (§12), so the importer converts nothing.
3. Marker position: the centre of the marker's centre voxel, minus its part's origin. Rotation: the instance's rotation, already in engine axes, turned into a quaternion from a fixed table of the 24 proper rotations, so the result is exact and the same on every run. The table spells each rotation with w > 0, or for a half turn with w = 0 and the first nonzero of x, y and z positive. Hardpoints from markers carry `FromVox`.
4. **Merge with the previous `.nvf`.** Every previous hardpoint **without** `FromVox`, which means it was authored in Blender, is carried over, re-attached to its part by path. Previous `FromVox` hardpoints are dropped, and the current markers replace them. A carried-over hardpoint whose part no longer exists is an error, not a silent drop. So is a carried-over name that collides with a marker's name, and the refusal names the marker: a hardpoint moved in Blender has lost `FromVox` (§7), Blender owns it from then on, and the marker is what the artist deletes (§11, question 6). Pivots follow the same rule: a pivot set in Blender (a part flag, `PivotAuthored`, bit 0 of the part's flags) survives unless a marker now sets it, and goes with its part when the part is gone.

### 6.3 Command line

```
NvfImport <input.vox> <output.nvf>            import; merges with output.nvf if it exists
NvfImport <input.vox> <output.nvf> --replace  import, discarding output.nvf's hardpoints
NvfImport <input.vox> <output.nvf> --check    exit 1 if importing would change output.nvf
NvfImport --dump <file.nvf>                   print parts, pivots and hardpoints as text
```

The tool writes to a temporary file and renames it over the output, so an error never leaves half a file behind. It prints one line per refusal, naming the node and what to do about it, and exits nonzero. Its exit codes are 0 when it wrote the file or `--check` finds it up to date, 1 when `--check` finds it stale, and 2 for a refusal, a mistake on the command line, or a file it cannot read or write. It leaves an existing `.nvf` it cannot read as it is, unless `--replace` discards it (ADR-020).

## 7. Blender extension

**Target:** Blender's extension platform, `blender_version_min = "4.2.0"`, the first LTS with extensions. It is tested on the current LTS. It needs nothing outside Blender's bundled Python.

**Import** (*File › Import › Neuron Voxel (.nvf)*) builds one collection per file:

- **Axes.** The extension swaps y and z on the way in and on the way out (§4.1), so the scene is in Blender's own right-handed, +Z-up space. A hardpoint's forward shows as the empty's +Y arrow and its up as +Z. The swap lives in `Geometry.py`, which the operators and the panel call, never in `NvfFormat.py`, which stays in NVF axes like its C++ twin.
- **One object per part**, parented as the part tree is, placed at its translation, with 1 Blender unit = 1 voxel. Its mesh is a **surface-only preview**: one quad for each voxel face whose neighbour is empty, coloured from the palette. Faces between two voxels are never drawn, so the preview grows with the surface, not the volume. Blender keeps colour attributes on points and corners, not faces, so each face's four corners carry its voxel's sRGB bytes in a byte colour attribute. Solid shading shows it with *Color › Attribute*, and a shared material shows it in Material Preview (ADR-020). Location, rotation, scale and mesh are locked, and the object is not selectable by default.
- **One empty per hardpoint**, parented to its part, displayed as arrows, with rotation mode `QUATERNION`. Its NVF name and `FromVox` flag are custom properties. Object names are unique across a `.blend`, so two imported ships would otherwise clash over `engine.main`. The object's own name is display only.
- **One sphere empty per part for its pivot**, with rotation and scale locked.
- The file's bytes are kept in the `.blend`, base64-encoded in a text datablock, so export needs nothing but the `.blend` and cannot drift from a moved or changed source file.

**The sidebar panel** (*N › NVF*): add a hardpoint to a part at the 3D cursor (pick the part and a type, type an identifier), rename one, snap its position to the nearest voxel centre, face centre or edge midpoint, snap its rotation to the nearest 90°, and **Validate**, which lists every problem export would refuse. Snapping to an edge means its midpoint (§11, question 9), so that every snap lands on the half-voxel grid and is exact in the file. A new hardpoint faces the part's forward, and a hardpoint renamed from a marker is Blender's from then on.

**Export** (*File › Export › Neuron Voxel (.nvf)*): parse the stored bytes, replace the hardpoint table and the pivots with what the scene holds now, and serialize. Voxels, palette and part tree come from the stored bytes, so they are written back byte for byte (N5). Export refuses rather than guesses when:

- a part was added, deleted, renamed, reparented or moved;
- a hardpoint is not parented to a part;
- a hardpoint has scale, or a name that breaks §4.1, or duplicates another name;
- a part's pivot was deleted, copied, or parented elsewhere;
- the stored file holds chunks this version does not know (§4.6);
- the file it would write over holds other voxels, colours or parts than the ones imported. NvfImport has run since, and writing would put the old voxels back (N5; §11, question 11). A file there that this version cannot read is left alone too, as NvfImport leaves one.

It lists every problem at once, as the importer does. A `FromVox` hardpoint keeps its flag while its name, part, position and rotation are those it was imported with, as single precision holds them. Once any changes, it counts as Blender-authored from then on (§6.2). A pivot moved from where it was imported becomes `PivotAuthored`.

**Layout:** `Tools/Blender/NeuronVoxelFormat/` holds `blender_manifest.toml`, `__init__.py` (Blender's spelling), `NvfFormat.py` (the format), `ImportOperator.py`, `ExportOperator.py`, `HardpointPanel.py` and `Preview.py`, `Checklist.md` (§9), and a `Tests/` folder. Two more modules hold what the extension decides without Blender: `Geometry.py`, the swap, the cube's turns, snapping and the preview's surface; and `Rebuild.py`, the export's rules. With `NvfFormat.py`, they import no `bpy`, so CI tests them (ADR-020). The manifest declares GPL-3.0-or-later (§11, question 8), and the extension's build leaves out `Tests/` and the checklist. The Python follows the style of `Build/*.py`. Classes are PascalCase with no affixes (R2); Blender's own identifiers, `bl_idname` and the like, keep Blender's spelling (R4).

## 8. Code and repository

| Where | What |
|---|---|
| `NeuronCore/NvfModel.h/.cpp` | `NvfModel`, `NvfPart`, `NvfHardpoint`, `NvfError`; `ParseNvfModel`, `LoadNvfModel`, `SerializeNvfModel`, `SaveNvfModel`. Record structs with `static_assert`s on size and every offset, as R16 does for GPU layouts. |
| `NeuronCore/NvfImport.h/.cpp` | `ImportVoxModel` and `NvfImportError` (§6.2). |
| `NeuronCore/VoxModel.h/.cpp` | Names and odd-sized rotations (§6.1). |
| `NvfImport/` (new project) | Console application: `Main.cpp` and the command line. References `NeuronCore` only. |
| `NeuronCoreTests/` | `NvfModelTests.cpp`, `NvfImportTests.cpp`, and the reader's new cases; `NvfGolden.cpp`, the golden model in code, and `RepositoryFile.cpp`, which finds the golden file and the assets. |
| `Tools/Blender/NeuronVoxelFormat/` | The extension (§7). |
| `Tools/Golden/Golden.nvf` | The golden file (§9). |
| `GameData/*.nvf` | The three converted assets. |

`AGENTS.md` changes: the `NvfImport` project goes into §2's table and into `.clang-tidy`'s `HeaderFilterRegex`. `Tools/` is a new top-level folder with Python in it, recorded in the layout ADR. **R20** is added, R18 and R19 having gone to the space scene's client/server rule and the game concept's opponent rule: *NVF has one specification, this document's §4, and two implementations, which the golden file keeps in agreement.* `Build/CheckProjectFiles.py` learns that `Tools/` holds no C++.

## 9. Verification

- **Golden file.** `Tools/Golden/Golden.nvf` is a small model that uses every field: three parts, two levels deep, all 16 palette entries, emissive entries, hardpoints with and without `FromVox`, a non-trivial rotation, and an authored pivot. A C++ test builds that model in code and asserts that `SerializeNvfModel` reproduces the file byte for byte. A Python test parses the file, asserts the same values, and asserts that serializing it again reproduces the bytes. If either implementation drifts, its test fails.
- **Refusals.** For every `NvfError` and `NvfImportError`, a test corrupts a known-good file or builds a bad `.vox` and expects that name, in both languages for `NvfError`.
- **Rotations.** All 24 proper `_r` values map to the quaternions in the table, and each quaternion turns NVF's forward +Z to where the swapped matrix sends MagicaVoxel's +Y. The 24 reflections are refused.
- **Axes.** A reader test loads an asymmetric `.vox`, with a voxel only at MagicaVoxel (1, 2, 3) and a marker pointing along +X, and asserts engine (1, 3, 2) and a forward of +X. The engine-side tests of the move itself are in §12.4. In Python, a round trip through the extension's swap returns the input exactly.
- **Merge.** Re-importing keeps Blender-authored hardpoints and pivots, replaces `FromVox` ones, and errors on an orphan or a name clash.
- **Assets.** A test imports each `GameData/*.vox` and compares the result with its committed `.nvf`. CI also runs `NvfImport --check` over every pair, so an `.nvf` stale against its `.vox` fails the build.
- **CI.** The Windows job runs the new C++ tests with the rest, and runs `--check`. The Linux job runs the Python tests with the system Python: `NvfFormat.py`, `Geometry.py` and `Rebuild.py` import no `bpy`.
- **Blender, headless.** `Tests/ExtensionTests.py` drives the operators in Blender's `bpy` module, which Blender publishes for its bundled Python 3.11. CI has none, so they skip there (§11, question 10), and they are run on 4.2 and on the current LTS before the extension changes.
- **Blender, by hand**, because CI has no Blender. This is a written checklist in the extension's folder: import the frigate, add an engine and a weapon, rotate one, export, re-import, confirm the hardpoints; then change the `.vox`, run `NvfImport`, and confirm the Blender hardpoints are still there. It is run before the extension's milestone is called done, and the report says it was.

## 10. Milestones

| # | Delivers | Done when | Status |
|---|---|---|---|
| N-M0 | §12: the engine on Direct3D's axes; SampleRenderer updated; axes ADR | Every suite green with its constants converted; the pinned tracer images reproduced under §12.4's rule; the mirror test green; the owner has run `Outpost.exe` and seen the same station from the same side | Done: PR #6, 2026-09-28; the owner's check by hand passed the same day |
| N-M1 | §4 in `NeuronCore`: reader, writer, validation; golden file; format ADR | Golden and refusal tests green | Done on 2026-09-28 ([ADR-019](ADR/ADR-019-nvf-format.md)): green in CI, runs 36460843922 and 36462201748 |
| N-M2 | §6.1 reader changes; `ImportVoxModel`; `NvfImport.exe`; the three assets converted; `--check` in CI; project/layout ADR | Import, rotation and merge tests green; CI checks the assets | Done on 2026-09-28 ([ADR-020](ADR/ADR-020-nvf-import.md)): green in CI, run 36464724053 |
| N-M3 | `NvfFormat.py` and its tests in CI | Python golden and refusal tests green on Linux | Done on 2026-09-28 ([ADR-019](ADR/ADR-019-nvf-format.md)): green in CI, run 36466992290 |
| N-M4 | The Blender extension | The §9 checklist passed by hand on the frigate and the capital ship | Built on 2026-09-28 ([ADR-020](ADR/ADR-020-nvf-import.md)): its tests pass headless on Blender 4.2.23 and 4.5.14; the checklist by hand is the owner's |
| later | `Outpost.exe` loads `.nvf` instead of `.vox` | A separate design change to SampleRenderer §7 | Not started |

**Re-planned by the game concept.** The owner accepted [`GameConcept.md`](GameConcept.md) on 2026-09-28, in which ship and station designs and their modules are `.nvf` files (its §5 and §12.1). N-M1, N-M2 and the follow-up that has `Outpost.exe` load `.nvf` therefore open its G-M1. N-M3 and N-M4 wait until after its slice: the mounts it needs sit on the grid, turned by the cube's 24 rotations, which MagicaVoxel's markers already express (§5). N-M3 and N-M4 were built alongside the concept, on the branch that carried N-M1 and N-M2 (PR #16), before its plan reached that branch. Whether they land before the slice is the owner's to say.

ADR numbers are taken in order when each ADR lands. ADR-008 to ADR-010 went to M3, M4 and the canvas, so the axes ADR is [ADR-011](ADR/ADR-011-engine-axes.md), and SampleRenderer §17's list names it. The space scene takes ADRs in parallel, and S-M4 took ADR-018, so on 2026-09-28 the owner reserved ADR-019 for N-M1's format ([ADR-019](ADR/ADR-019-nvf-format.md)) and ADR-020 for N-M2's importer and layout; the space scene and the game concept continue from ADR-021.

## 11. Risks and open questions

**Answered by the owner on 2026-09-27:**

1. **Axes: Direct3D's convention (N9, §4.1).** The draft proposed MagicaVoxel's right-handed, +Z-up space, with a hardpoint's +Y pointing out. The owner chose Direct3D's customary left-handed axes, +Y up and +Z forward, for model space and the hardpoint frame alike. It costs a y/z swap in the two tools that convert, and none anywhere else.
2. **Committing `.nvf` beside `.vox` (§3).**
3. **Blender 4.2 as the floor (§7).**
4. **`main` as the single-part default name (§5).**

**Answered by the owner on 2026-09-28, as N-M1 began:**

5. **A part's default pivot is its geometric centre, `size / 2` (§4.3).** The draft's ⌊size / 2⌋ was the corner of MagicaVoxel's pivot voxel: half a voxel off-centre on every odd axis, and not a point a marker can give, since markers land on voxel centres.
6. **Refining a marker's hardpoint in Blender keeps the name clash an error (§6.2, §7).** Moving a `FromVox` hardpoint in Blender clears its flag, so the next import finds an authored hardpoint and a marker of one name. The refusal names the fix: delete the marker in MagicaVoxel, now that Blender owns the hardpoint. A marker seeds a hardpoint once.
7. **NVF's ADRs are ADR-019 and ADR-020 (§10),** reserved because the space scene takes ADRs in parallel; S-M4 had already taken ADR-018.

**Answered by the owner on 2026-09-28, as N-M4 began:**

8. **The extension's manifest declares GPL-3.0-or-later (§7),** Blender's norm for add-ons. The rest of the repository has no licence file, and the manifest speaks for the extension alone.
9. **Snapping to an edge means the edge's midpoint (§7),** rather than the nearest point on the edge, so that snaps repeat and land on the half-voxel grid.
10. **The extension's tests that need Blender run locally and skip in CI (§9).** What can be decided without Blender is kept apart from it, so that CI tests it.
11. **Export refuses to write over voxels NvfImport has changed since the import (§7).** Export works from the bytes the `.blend` stores, so writing from an older `.blend` would put the older voxels back.

**Risks:**

- **Is the name clash in the merge an error or a rename?** An error is chosen: it is loud and never guesses, but an artist must then delete one of the two.
- **MagicaVoxel may change how it stores `_name` or `_r`.** Both are undocumented conventions of the file. The reader's tests pin today's behavior, and the importer refuses what it does not recognise.
- **Blender API drift across versions.** The preview and panel code is small and kept apart from `NvfFormat.py`, so the format itself never depends on `bpy`.
- **A carried-over hardpoint is anchored to its part's grid.** Its position is in part space, from the grid's minimum corner. Growing a part toward negative coordinates in MagicaVoxel moves that corner, and the Blender-authored hardpoints `NvfImport` carries over move off their voxels without a refusal. The checklist of §9 looks at the hardpoints after a voxel edit.
- **Markers and the game's `.vox` loading.** From N-M2 the reader returns marker models as instances. The game still loads `.vox` (§2), so a committed asset with a marker would draw it as voxels, or be refused if the marker is rotated (§6.1). Until the game loads `.nvf`, the three assets carry no markers, and their hardpoints are authored in Blender.
- **A preview of a large model may be slow.** The station is the worst case the repository has. Its surface is 360,330 quads, which Blender 4.2 imported headless in 2.6 s (ADR-020); the checklist looks at the viewport. If the surface mesh is too heavy there, the fallback is a point cloud instanced with cubes through geometry nodes.

## 12. Moving the engine to Direct3D's axes

### 12.1 Why, and why first

The engine's world is right-handed and +Z up, MagicaVoxel's (SampleRenderer §7.5). Leaving it there would put a y/z swap between every NVF model and the world, forever, and every future system — physics, gameplay, audio — would have to know which side of the swap it is on. Moving the engine means one convention everywhere below the authoring tools. It comes before N-M1 because the importer relies on the reader doing the conversion (N10), and because it is cheapest now, while the renderer is small.

### 12.2 What depends on the axes

This was counted by reading the code on 2026-09-27. N-M0 converted all of it on 2026-09-28, and [ADR-011](ADR/ADR-011-engine-axes.md) records what it found. There are no view or projection matrices anywhere. Every view is a basis (right, up, forward) plus scalars, and view depth is always `dot(offset, forward)`, which is positive in front of the camera. So the shaders, the ray-box code, `SplatBounds`, reversed-Z, conservative depth, and the reference tracer are all free of any axis convention. What is left:

| Where | Today | After |
|---|---|---|
| `MakeViewBasis` (`NeuronCore/PerspectiveView.cpp`) — **the only handedness in the code** | `right = forward × worldUp`, `up = right × forward`; fallback world up +Y when forward is vertical | `right = worldUp × forward`, `up = forward × right`, so `right × up = +forward`; the fallback becomes +Z, since +Y is now the world's up and would be degenerate |
| `.vox` reader (`NeuronCore/VoxModel.cpp`) | Voxels, `SIZE` and `_t` as stored | Each swapped (x, z, y) as it is read, before *T* − ⌊*s*/2⌋; `_r` conjugated (§6.1). Records keep their order, so ties still go to the lower record (ADR-006). |
| Orbit camera (`GameLib/OrbitCamera.cpp`) | `WORLD_UP` +Z; forward (cos *p* cos *y*, cos *p* sin *y*, sin *p*) | `WORLD_UP` +Y; forward (cos *p* cos *y*, sin *p*, cos *p* sin *y*). That is the old vector swapped, so the default yaw, the pitch limits and the drag direction keep their values and the camera behaves exactly as before. Pan and fly follow `WORLD_UP`. |
| Lighting, from M3 ([ADR-008](ADR/ADR-008-lighting-from-the-file.md)): `SunDirection`, `Ambient` and `LightPixel` (`NeuronCore/Lighting.cpp`) and their twins in `NeuronClient/Shader/Lighting.hlsli`; `MakeShadowView` (`NeuronCore/OrthographicView.cpp`); `FitShadowView` (`GameLib/Game.cpp`) | Sun (sin *a* cos *e*, −cos *a* cos *e*, sin *e*); ambient on *N*z; ground plane z = 0, normal +Z; shadow view's world up +Z; shadow box grown down to z = 0 | Each swapped: sun (sin *a* cos *e*, sin *e*, −cos *a* cos *e*); ambient on *N*y; ground plane y = 0, normal +Y; world up +Y; box grown down to y = 0. M3's lighting, shadow and station tests carry Z-up constants of their own |
| The explosion, from M4 ([ADR-009](ADR/ADR-009-explosion-motion.md)): `ExplosionPose` and `BoundExplosion` (`NeuronCore/Explosion.cpp`) and the pose's twin in `NeuronClient/Shader/Explosion.hlsli`; the corner and rest tests | Gravity along −Z; contacts and rest at centre heights √3/2 and 0.5 in z; the upward bias and the lift along +Z; the envelope from the ground at z = 0 to its apex | Each along +Y. The spins turn about coordinate axes chosen by hash, so they only relabel, and the tests' lowest point is taken along +Y |
| Comments that state the convention | `PerspectiveView.h`, `OrthographicView.h`, `OrbitCamera.h`, `VoxModel.h` | Restated |
| Tests | About 35 constants in `NeuronCoreTests` (view, bounds, reader and station tests) and about 15 in `NeuronClientTests` (view splat and debug view cameras) are Z-up positions, sizes and up vectors | Each swapped. `ExpectOrthonormalRightHanded` becomes `ExpectOrthonormalLeftHanded` and asserts `right × up = +forward`. |
| `SampleRenderer.md` | §7.5 table; the asset description of §3; the ground plane, ambient term (*N*·z) and sun azimuth of §11; gravity and "vertical" in §12 | Rewritten in the new axes, and the same commit records the axes ADR |

The octahedral normal encoding needs no change. It is exact for all six axis directions whatever the axes; only the Normal debug view's colours change meaning, with green now up. Winding is untouched, because every pipeline draws with `CULL_MODE_NONE`. The first mesh the engine draws, a ground quad or anything else, uses Direct3D's default of clockwise front faces, which is what left-handed winding means.

### 12.3 What is subtle

- **A mirrored image passes every existing test.** If the cross products keep today's order, right comes out as −X and the picture is mirrored. The GPU-against-CPU tests would still pass, because both sides share one basis. §12.4's mirror test is there for that.
- **The half-voxel trap.** The station is 207 × 228 × 255 in MagicaVoxel, so ⌊*s*/2⌋ differs per axis. If the swap is applied to the voxels but not to `SIZE` or `_t`, or after the origin is computed instead of before, the station ends up half a voxel off its floor, and nothing fails loudly.
- **Not every computation permutes exactly.** The swap moves coordinates without rounding, but a sum over coordinates is taken in axis order. `Length`, and so `Normalize`, adds x², y² and z² in that order, and after the swap adds them in another, which may round differently; `/arch:AVX2` also lets MSVC fuse different multiply-adds (`AGENTS.md` §3). The view's forward and right, and so every ray, can move by an ulp. Depth, itself such a sum, moves almost everywhere. The voxel a pixel shows changes only where its ray passes within rounding of an edge.
- **Surfaces that meet later milestones.** The sun's azimuth from `rOBJ`, the ambient's up, the explosion's gravity: whichever of them lands after N-M0 is written in the new axes from its first line. The design text is updated in N-M0 so that nobody implements the old axes by mistake.

### 12.4 Verification

- **Pinned before the change.** In a commit of its own, before any axis moves, a test renders the station with the reference tracer from three cameras and compares the voxel each pixel shows with a reference that the same commit adds. After the move, the same cameras, converted, must show the same voxels. Only the voxel index is pinned. The reader keeps the records' order, so an index survives the move; depth does not (§12.3).
  - A pixel that differs is judged by SampleRenderer §14's rule, with the edge test of the explosion's GPU tests. It may show another voxel, or none, only where its ray meets the grown box of the voxel it now shows, if any, and misses the shrunk box of the voxel it showed, if any. Each box is grown or shrunk by 1/256 of a voxel. An exact tie between axes in the tracer's traversal, whose order the swap also changes, falls under the same rule.
  - A difference anywhere else fails and is investigated. It is never re-pinned.
  - The differences on edges get a bound from the first run after the move, and the axes ADR records it with the count per camera.
- **Mirror test.** A single voxel at +X, seen from a camera looking along +Z with +Y up, must land in the right half of the image, and one at +Y in the top half. This is the test that catches a mirrored basis (§12.3).
- **Reader.** The asymmetric `.vox` of §9 lands at swapped coordinates, and the station's lowest layer is at y = 0.
- **Every suite** green with its constants converted, and the checkers clean.
- **By hand.** The owner runs `Outpost.exe` and sees the station upright, from the same side, and orbiting the same way under the mouse as before.

## 13. References

- MagicaVoxel file format, chunks and scene graph: https://github.com/ephtracy/voxel-model/blob/master/MagicaVoxel-file-format-vox.txt and `MagicaVoxel-file-format-vox-extension.txt` in the same repository.
- Blender extensions and the manifest: https://docs.blender.org/manual/en/latest/advanced/extensions/getting_started.html
- `Design/Archive/SampleRenderer.md` §7.1 (the reader), §7.2 (palette), §7.5 (coordinates); ADR-002 (record and palette); ADR-006 (tie order).
