# Outpost.Voxel — Sample Renderer Design

**Status:** accepted by the owner, 2026-09-27; the questions of §16 are answered · **Date:** 2026-09-27
**Technique:** A. Majercik, C. Crassin, P. Shirley, M. McGuire, *A Ray-Box Intersection Algorithm and Efficient Dynamic Voxel Rendering*, JCGT 7(3), 2018 — `Majercik2018Voxel.pdf`
**Asset:** `GameData/MilitaryStation.vox`

This is the design document `AGENTS.md` refers to: it says what is built, and `AGENTS.md` says how it is written. Where this document takes an engineering decision, the ADR that records it lands in the commit that implements it (§17).

## 1. Summary

The sample renders `MilitaryStation.vox` with the method of Majercik et al. Each voxel is splatted as a screen-space rectangle that bounds its projection, and the pixel shader intersects the pixel's ray with the voxel's box to obtain exact visibility, depth and normal. No mesh is built and no visibility is precomputed, so the same pass renders the intact station and the station blown apart into its 225,048 voxels, each moving and tumbling on its own. It runs on Direct3D 12 at feature level 12_1 and does not use DirectX Raytracing.

| # | Decision | Source |
|---|---|---|
| D1 | Direct3D 12, device created at feature level 12_1. DXR and mesh shaders are not used, even where the hardware offers them. | Owner, 2026-09-27 |
| D2 | The explosion breaks the station into its own voxels: 225,048 pieces, each an independently moving and rotating box. | Owner, 2026-09-27 |
| D3 | Explosion motion is analytic and stateless: a pure function of voxel index and time. | Owner, 2026-09-27 |
| D4 | Deferred shading: a sun with a shadow map rendered by the same splat technique, hemispheric ambient, emissive palette entries, ACES tone mapping. No ambient occlusion, no bloom. | Owner, 2026-09-27 |
| D5 | The palette has exactly 16 entries and will stay that way; a voxel's colour is a 4-bit index. | Owner, 2026-09-27 |
| D6 | Voxels become rectangles by vertex pulling, four vertices per voxel — the paper's own Direct3D path. | §9.1 |
| D7 | The view pass writes a visibility buffer (voxel index and normal), not a G-buffer. | §7.3 |
| D8 | Reversed-Z with an infinite far plane; conservative depth output through `SV_DepthLessEqual`. | §7.5, §9.3 |
| D9 | Shader Model 6.7, compiled by DXC at build time. No third-party code. | §5; owner, 2026-09-27 (ADR-007) |
| D10 | Every GPU algorithm has a CPU twin, and the GPU output is checked per pixel against it on WARP in CI. | §14 |
| D11 | Rendering is 1:1 at the window's size; the window opens borderless fullscreen, and `--bench` always renders at 1920 × 1080. | §13; owner's display and request, 2026-09-27 |
| D12 | There is no comparison against MagicaVoxel renders; MagicaVoxel conventions the file cannot settle stay stated defaults. | Owner, 2026-09-27 |
| D13 | A canvas draws text and the HUD over the frame: DirectWrite lays text out and rasterizes its glyphs, and Direct3D 12 draws them. Direct2D is not used. | §13; owner, 2026-09-28 (ADR-010) |

## 2. Scope

The sample exists to show the paper's rendering method working completely and verifiably on this asset under this API contract: the splat-and-intersect pipeline of the paper's §3–§5, with its hybrid screen-space bounds and conservative depth; the same pipeline rendering the shadow map; the dynamism claim, demonstrated by an explosion that moves every voxel without rebuilding anything; and an automated per-pixel check of what the GPU produces.

Out of scope: DXR and mesh shaders (D1); MagicaVoxel's path-traced look — global illumination, ambient occlusion, bloom, image-based light (D4); transparency (the file has no glass); anti-aliasing in the first version (§16); the paper's stochastic pruning (§4.2); splitting voxels into smaller pieces (D2); collision between voxels (D3); editing; `.vox` features this file does not use (§7.1); HDR display output.

### What this sample cannot tell you

The paper's speed-ups over mesh rasterization — about 7× across the full depth range and 20× for distant voxels — are reported for its OpenGL implementation, where a point sprite costs one vertex per voxel, on scenes of up to 53 million voxels on a GeForce 1080 (paper §6.3). This asset has 225,048 voxels, 235 times fewer than the scene of the paper's Figure 1, and D2 keeps it there. At that size any competent technique is fast, so the sample demonstrates correctness and dynamism, not throughput. Direct3D also has no point sprites, so each voxel costs four vertex-shader invocations where OpenGL spent one. Neither point matters here. Both would be the first things to measure if the paper's regime ever became the question, and the cheap way to get there would be replicating the station on a grid — which is not planned.

The technique is nevertheless the right one for this brief, and the explosion is the reason. The two usual alternatives for a static model — a greedy mesh (the paper's baseline) and ray marching a voxel grid in a compute shader — both rest on a structure that a per-voxel explosion invalidates every frame. The splat method has no such structure.

## 3. The asset, measured

Measured on 2026-09-27; the method is at the end of this section.

| Property | Value |
|---|---|
| File | 925,571 bytes, VOX version 200 |
| Models | One, `SIZE` 207 × 228 × 255 in MagicaVoxel's axes (x, y, z; z is up), which is 207 × 255 × 228 in the engine's (y is up, §7.5) |
| Voxels | 225,048, no two at the same position; occupied range x 1–205, y 1–227, z 0–254 in the file, which is x 1–205, y 0–254, z 1–227 in the engine |
| Scene graph | `nTRN` → `nGRP` → `nTRN` (`_t` = `0 0 127`, (0, 127, 0) in the engine, no `_r`, layer 0) → `nSHP` (model 0) |
| Palette | Entries 1–16 are the standard CGA/EGA 16-colour palette in its usual order; entries 17–256 are black and unused |
| Entries in use | 7 of 16 — 2: 59,013 · 8: 68,956 · 9: 84,490 · 10: 8,025 · 13: 6 · 15: 773 · 16: 3,785 |
| Materials | Entries 10, 15 and 16 are `_emit` (`_emit` 0.6, `_flux` 2): 12,583 emissive voxels, 5.6 %. The other 253 carry no `_type`, which is MagicaVoxel's diffuse default. |
| Render settings (`rOBJ`) | Sun `_inf`: `_i` 0.7, white, `_angle` `50 50`, `_area` 0.07 · sky `_uni`: 0.7, white · `_lens`: field of view 45° · `_film`: exposure 1, ACES on, gamma 2.2 · `_ground`: 80 80 80 · `_bg`: 0 0 0 · `_ibl` naming an HDR image that is not in the repository · bloom, fog and atmosphere entries |
| Cameras (`rCAM`) | Ten, all at MagicaVoxel's defaults (focus 0 0 0, radius 0): there is no stored viewpoint |
| Other chunks | Sixteen `LAYR` (colours only), one `NOTE`, and a `META` chunk (`_anim_range` `0 30`) that the loader must skip |

| Geometry | Value |
|---|---|
| Voxels with at least one face exposed to air | 205,676 (91.4 %) |
| Voxels with all six neighbours occupied | 19,372 |
| Exposed faces | 360,330 — as a naive face mesh, 720,660 triangles |
| Greedy mesh of those faces, merging only equal palette entries | 91,883 quads, 183,766 triangles |
| Voxels reachable by a ray from outside the model | 110,939, through 174,302 faces |
| Sealed air | 394,853 cells in 823 pockets |

The splat workload is 225,048 rectangles — 450,096 triangles and about 900,000 vertex-shader invocations per view — whether the station is intact or in pieces. Refusing to precompute visibility, as the paper does, costs a factor of 2.03 here (every voxel, against the 110,939 an outside ray can reach). We keep the refusal: the explosion exposes every voxel anyway, and a precomputed list is precisely what the method exists to do without.

Sixteen colours fit in four bits (D5), and emissiveness belongs to a palette entry, not to a voxel.

Placement: voxel *v* of a model of size *s* placed at translation *T* is taken to occupy the unit cube whose minimum corner is *T* + *v* − ⌊*s*/2⌋, with all three in the engine's axes (§7.5). For this file that puts the lowest layer exactly on y = 0, the height of MagicaVoxel's ground plane, its z = 0 (enabled in `_setting`). That is corroboration, not proof, and with no MagicaVoxel comparison planned (D12) it stays a stated default.

The file stores no usable camera. The default view frames the occupied box's bounding sphere — radius ≈ 199 units about (0.5, 127.5, 0.5) — in the file's 45° vertical field of view. That puts the camera about 520 units out, where a voxel spans about 2.5 pixels of a 1,080-line image.

*Method.* A throwaway Python script walked the chunk tree, built the dense 207 × 228 × 255 occupancy grid and tested six-neighbourhoods. It flood-filled air from outside the model's box with 6-connectivity to separate exterior air from sealed air, and greedy-meshed each slice per axis direction and palette entry. The loader tests of M1 re-derive the counts, size, translation, palette and emissive entries in C++ and pin them (§14). The geometry table is context and is not pinned.

## 4. The technique

### 4.1 What the paper specifies

Listing 2 (paper §3) is the whole method. The rasterizer acts as a generator of potentially visible pixels, and a small ray tracer runs in the pixel shader. For each voxel, the vertex shader reads the voxel's transform and material, culls conservatively against the near plane, and computes a screen-space rectangle that bounds the voxel's projection. For voxels that cover a few pixels this is the AABB of the projected bounding sphere; for larger ones (more than 20 × 20 pixels, §4) it is the AABB of the box's edges after clipping them against the frustum. The rectangle is placed at the depth of the voxel's nearest vertex, and under Direct3D each of four vertices moves to one corner by vertex index. The pixel shader builds the ray through the pixel, intersects it with the box (§5, Listing 5), discards on a miss, and otherwise shades and writes the hit's depth. Because the rectangle sits at the nearest vertex, the written depth can only move away from the camera, so early rejection under conservative depth stays valid. The same pass renders shadow maps. Voxels smaller than a pixel are optionally thinned stochastically (Listing 3). The method uses "no precomputation or spatial data structure", and an animation that moves every voxel independently "has zero impact on rendering performance" (§6.3).

The intersection (Listing 5) moves the ray into the box's frame and picks the three front-facing planes from the signs of the ray direction. It intersects all three and keeps the one whose hit lies within its face; that plane's sign vector is the normal. It is branchless, and two compile-time switches, `oriented` and `canStartInBox`, remove what a caller does not need. When a normal and distance are required it beat every earlier test the authors measured (Tables 2 and 3). Its "outside" form is wrong for a ray that starts inside the box.

### 4.2 Where the paper leaves the choice to us

1. **No point sprites.** Direct3D does not expose them (paper §3), so vertices are pulled: four per voxel, with the corner chosen by `SV_VertexID`. NVAPI's `QUAD_FILLMODE` would save a vertex, but it is vendor-specific and outside FL 12_1; the paper declined it for the same reason.
2. **Rectangle shape.** Listing 4 returns a square because a GL point is square. Under Direct3D the rectangle is the non-square AABB, which the paper notes bounds the voxel more tightly.
3. **Sphere radius.** Listing 4's `voxelSize * 1.732051` is the circumscribed radius only if `voxelSize` is the half-extent. We compute |half-extent| explicitly.
4. **Depth direction.** Under reversed-Z the nearest vertex has the *largest* depth, so the shader's promise becomes "output ≤ rasterized depth": `SV_DepthLessEqual`, with a `GREATER` depth test. The shader writes `min(hitDepth, SV_Position.z)`, so the promise also holds after rounding.
5. **The near-plane singularity** ("some care has to be taken", paper §6.3). A voxel whose bounding sphere crosses the near plane takes the clipped-edge path whatever its size, and the pixel shader discards hits nearer than the near plane. A voxel that contains the eye is therefore not drawn — which is what near-plane clipping does to a mesh — and the "outside" form of Listing 5 is correct for every ray we cast.
6. **Porting Listing 5 to HLSL.** There are three traps. HLSL's `sign` returns an integer vector. GLSL's `v * M` idiom is a multiply by the transpose, and translating it literally is where a silent transpose hides, so the port spells the change of frame out as dot products (§9.4). Finally, the paper's "a check for zeros in `ray.dir` is not needed" relies on IEEE division by zero and on comparisons with NaN being false, behaviour HLSL compilers do not promise by default. Exactly-zero components are not exotic here: a sun straight overhead makes every shadow ray axis-parallel, and a level camera at an odd resolution produces a whole row and column of them. Both twins are tested on exactly those cases (§14). If the GPU fails them, the fix is an explicit guard in the shader and an ADR, not a global strictness switch.
7. **Shadow projection.** The paper's claim covers "any pinhole perspective projection", but a sun is orthographic. That case is simpler — the rectangle follows exactly from the box's extents, and there is no singularity — and it gets its own permutation (§10).
8. **MSAA.** Listing 2 casts "through the current pixel or MSAA sample". The first version casts one ray per pixel and has no MSAA (§16).
9. **Stochastic pruning** (Listing 3) is left out. It gained 1.2× at 53 million voxels; at 225,048 there is nothing to gain, and it trades temporal stability for throughput.
10. **Host-side frustum culling** of voxel objects is moot when there is a single object.
11. **Output.** The paper shaded forward or into a G-buffer. We write a visibility buffer of voxel index and normal, the least that lets lighting run once per pixel — the paper's bandwidth argument applied to our own output.
12. **Watertight seams.** Listing 5 tests the hit against each face with a strict inequality, so a ray that meets two neighbouring voxels exactly on their shared edge, or runs exactly in the plane between two layers, hits neither. A camera on an integer coordinate would see a line of background through the station, and a shadow map whose texel centres land on voxel edges would leak light; the GPU and the reference tracer agree on such pixels, so no comparison would catch it. The face tests include their edges instead (§9.4): a ray on a seam hits both neighbours, and the depth test keeps the one drawn first, the lower index. An isolated box's boundary now counts as part of the box, which changes its silhouette by nothing. The owner chose this on 2026-09-27.

## 5. Platform contract

The renderer uses Direct3D 12 with the device created at minimum feature level 12_1 (D1), and Shader Model 6.7 (D9). DXR is never used; the renderer does not even query `D3D12_FEATURE_D3D12_OPTIONS5`. Mesh and amplification shaders are required only at FL 12_2 and are not used either.

The two things FL 12_1 adds over 12_0 — conservative rasterization tier 1 and rasterizer-ordered views — have no job in this design. Rays go through pixel centres, so ordinary rasterization of the bounding rectangle already reaches every pixel whose ray can hit the box; conservative rasterization would only add invocations that miss. Nothing in the model is transparent for ROVs to order. In fact nothing here needs more than FL 11_0 plus SM 6.0 — conservative depth output is a Shader Model 5 feature. FL 12_1 stays because it is the owner's contract, and its only practical effect is to refuse FL 11_x and 12_0 hardware that could run the sample. The owner confirmed that on 2026-09-27. Shader Model 6.7 is the same kind of floor: the owner raised it from 6.0 on 2026-09-27, no shader uses a feature above 6.0 yet, and it refuses adapters and drivers that report less (ADR-007).

In-box WARP implements FL 12_1 on Windows 10 1709 and later, so CI can run the real renderer (§14).

The design uses graphics and compute pipeline states; root signature 1.0 with root CBVs, root SRVs and a few small, fully populated descriptor tables, within resource binding tier 1's limits; committed resources; `R32_TYPELESS` depth read back as `R32_FLOAT`; an `R32G32_UINT` render target; a typed UAV store to `R16G16B16A16_FLOAT`; comparison sampling; timestamp, pipeline-statistics and occlusion queries; a flip-model swap chain; and DirectWrite, which lays the canvas's text out and rasterizes its glyphs on the CPU (§13, ADR-010).

It deliberately does not use:

- enhanced barriers or SM 6.6 dynamic resources, which depend on runtime and driver support FL 12_1 does not imply and which nothing here needs;
- `ExecuteIndirect`, because there is no GPU-driven work at this scale;
- geometry shaders, because vertex pulling does the same job without them;
- wave intrinsics, which are optional below FL 12_2 and not needed;
- any vendor extension;
- the Agility SDK — the in-box runtime covers everything above, so no `D3D12Core.dll` ships;
- Direct2D and Direct3D 11On12, because the canvas's glyphs go from DirectWrite straight to Direct3D 12 (ADR-010).

One package comes from outside the SDK: WinPixEventRuntime, with which a program names the regions of a frame for PIX. The owner added it to the client engine, and no code calls it yet (ADR-004).

Shaders are compiled at build time by MSBuild's `FxCompile`, which switches to `dxc.exe` when a Shader Model 6 profile is selected, into headers embedded in the binary. Their flags are identical in Debug and Release, because `AGENTS.md` §3 allows the two configurations exactly four differences: optimised, debug information embedded for PIX, warnings as errors. The port in §9.4 uses only scalar conditions, so it compiles identically under HLSL 2018 and 2021, whichever the SDK's `dxc.exe` defaults to.

At start-up the application enumerates adapters in high-performance order, creates the device at 12_1, and requires `D3D12_FEATURE_SHADER_MODEL` ≥ 6.7. Otherwise it refuses, with a message that names the adapter and what it lacks. `--warp` selects WARP.

## 6. Architecture

### 6.1 Projects

ADR-001 settled the first layout with the first project. The owner then reshaped it into an engine and a game, split along the client/server line the game will grow into (ADR-003); `AGENTS.md` §2 maintains the table below.

| Project | Kind | Namespace | Depends on | Holds |
|---|---|---|---|---|
| `NeuronCore` | static library | `NeuronCore` | — | The engine core that client and server share: `.vox` reader, voxel model, maths, the CPU twins (ray-box, bounds, pose, packing), reference tracer. No Windows or Direct3D headers. |
| `NeuronClient` | static library | `NeuronClient` | `NeuronCore` | The client engine: device, resources, passes and their shaders (in `Shader/`), the canvas with its DirectWrite text and its twin (ADR-010), window, input, clock. Owns the single Windows include header of `AGENTS.md` §4. |
| `NeuronServer` | static library | `NeuronServer` | `NeuronCore` | The server engine; empty until the server has code of its own. |
| `GameLogic` | static library | `GameLogic` | `NeuronServer`, `NeuronCore` | The game's rules on the server side; empty for now. |
| `GameLib` | static library | `GameLib` | `NeuronClient`, `NeuronCore` | The game on the client side: camera controls, scene setup. |
| `Outpost` | Win32 application | `Outpost` | `GameLib`, `GameLogic`, `NeuronClient`, `NeuronServer`, `NeuronCore` | `Outpost.exe`: entry point and command line. The client, and until `Server.exe` splits off, the server process too. |
| `NeuronCoreTests` | test DLL | `NeuronCoreTests` | `NeuronCore` | CPU tests. |
| `NeuronClientTests` | test DLL | `NeuronClientTests` | `NeuronClient`, `NeuronCore` | GPU tests on WARP. |

There is one solution, `Outpost.Voxel.slnx`, at the root, where CI looks for it. The test DLLs land at `x64\<Configuration>\<Project>.dll`, which is where the workflow's test step expects them. `.clang-tidy`'s `HeaderFilterRegex` names every library and the application. Dependency edges run one way, and the client side and the server side never reference each other. `NeuronCore` stays free of Windows and Direct3D so that the CPU twins share nothing with the GPU code they check except the algorithm. The application's build copies `GameData/MilitaryStation.vox` into the output directory, beside `Outpost.exe`; the test suites look for it upward from their working directory, which finds that copy under Test Explorer and the repository's own under CI.

### 6.2 Shader sources

HLSL lives in `NeuronClient/Shader/`: shaders belong to the library that uses them, in its `Shader` folder (`AGENTS.md` §2). A `.hlsl` file is one entry point: a few lines that set permutation switches, then an include. Algorithms live in `.hlsli` files shared by the permutations: the ray-box port (§9.4), the screen-space bounds, the explosion pose, the splat vertex and pixel bodies, packing, the canvas's arithmetic, and the mirrors of the layouts shared with C++ (§7.4). The splat shaders come in four permutations — `ORIENTED` 0/1 × `ORTHOGRAPHIC` 0/1 — for each of the vertex and pixel stages, and the view splat's pixel stage has two measurement variants of each orientation besides: `PLAIN_DEPTH` (§9.3) and `COUNT_OVERDRAW` (§11). `.hlsli` is registered in `.editorconfig` and `.gitattributes`, and R17 in `AGENTS.md` governs both extensions (§17).

### 6.3 Data flow

```
GameData/MilitaryStation.vox
   │  NeuronCore reader: validate, place, pack
   ▼
voxel records, 225,048 × u32 · palette, 16 entries · frame constants (cameras, t)
   │
   ├──► shadow splat, orthographic ─────► shadow map, D32 4096²
   │                                          │
   └──► view splat, perspective ────────► depth D32 + visibility R32G32_UINT
                                              │
                                              ▼
                                 lighting, compute (reads all of the above)
                                              │
                                              ▼
                                 HDR colour, R16G16B16A16_FLOAT
                                              │
                                              ▼
                                 tone map, ACES ──► back buffer, sRGB view
                                                          ▲
                           canvas: text and panels ───────┘  (DirectWrite glyphs, R8 atlas)
```

## 7. Data

### 7.1 Voxel records and the loader

A voxel is one 32-bit record. Bits 0–23 hold x, y and z (8 bits each, model coordinates 0–255, in the engine's axes), bits 24–27 hold the palette entry minus one, and bits 28–31 are zero. The buffer is a `StructuredBuffer<uint>` of 900,192 bytes. `NeuronCore` owns the pack and unpack functions, and a `.hlsli` mirrors them. Widening a field is a format change (R14).

A model's placement travels in constants, not per voxel: with `modelOrigin` = *T* − ⌊*s*/2⌋, a voxel's centre is `modelOrigin` + (x, y, z) + 0.5.

The loader accepts VOX versions 150 and 200, skips chunks it does not know (this file's `META`), follows `nTRN`/`nGRP`/`nSHP`, skips hidden nodes and applies translations. It swaps MagicaVoxel's y and z in every size, voxel and translation as it reads them, before any origin is computed, and keeps the records in the file's order (§7.5). Anything this design has no tested answer for is rejected with a named error, returned as `std::expected<VoxModel, VoxError>`:

- a rotation (`_r`) — this file has none, and MagicaVoxel's rotation and pivot rules for even sizes cannot be checked against it;
- a colour index above 16 (D5);
- a model dimension above 256;
- duplicate positions;
- any size or count that disagrees with its chunk.

### 7.2 Palette and materials

The palette is sixteen entries, each a linear albedo (converted from the file's sRGB bytes with the exact sRGB curve) and an emissive scale: 256 bytes of constants. MagicaVoxel does not document how `_emit` and `_flux` become radiance, so the sample defines its own mapping — one multiplier per entry, tuned by eye in M3 (D12) — and says so where it is implemented.

### 7.3 Visibility buffer

The view pass writes `R32G32_UINT`: x is the voxel index (`0xFFFFFFFF` where no voxel was hit), and y is the world-space normal, octahedrally encoded as two 16-bit snorm values. Beside it sits `D32_FLOAT` depth. The normal is stored rather than recomputed because in the exploded state it depends on the pose, and storing it keeps the lighting pass ignorant of the motion model. The cost is eight bytes per pixel.

### 7.4 Constants shared with HLSL

The C++ structs in `NeuronClient` are the source of truth, with `static_assert`s on their size and on every member's offset. Each has one hand-written HLSL mirror in a `.hlsli`: the constant buffers, and the canvas's quad, which is the element of a structured buffer (§13). A compute shader includes the mirrors and copies every field of every struct into a UAV, reading two quads so that the buffer's stride is checked too; a `NeuronClientTests` test fills the C++ structs with a sentinel pattern and compares, so layout drift fails CI. A single header shared by both languages was rejected: HLSL's type names (`float4`, `uint2`) are not legal C++ type names under `AGENTS.md` §1, and the macro layer that fakes them would cost more than the test.

### 7.5 Coordinate conventions

| Space | Convention |
|---|---|
| World | Direct3D's: left-handed, +X right, +Y up, +Z forward, one unit per voxel edge, ground plane y = 0 (ADR-011) |
| Model → world | MagicaVoxel's axes are right-handed with +Z up, so the reader swaps y and z (§7.1); then the minimum corner is *T* + *v* − ⌊*s*/2⌋ (§3); no rotations (§7.1) |
| View | Left-handed, looking down +Z, +Y up: right × up = forward |
| View projection | Reversed-Z with an infinite far plane: depth = *n* / view depth, cleared to 0, test `GREATER`, *n* = 0.1 |
| Shadow projection | Orthographic, standard Z: cleared to 1, test `LESS` |
| Screen | Direct3D: origin top left, +y down; one ray per pixel centre |

Depth conventions are the classic place for a sign error, so the code names each one once — a helper per view that says which way "nearer" points — and nothing compares raw depths outside those helpers. Handedness is the classic place for a mirror. It enters the code in one place, the view basis, and a test pins it (§14).

## 8. The frame

| Pass | Kind | Reads | Writes |
|---|---|---|---|
| Shadow splat | Graphics, depth only | Voxel records, constants | Shadow map |
| View splat | Graphics | Voxel records, constants | Depth, visibility |
| Lighting | Compute, 8 × 8 groups | Depth, visibility, shadow map, voxel records, palette | HDR colour |
| Tone map | Graphics, one full-screen triangle | HDR colour | Back buffer |
| Canvas | Graphics, one instanced draw of quads | Quads, glyph atlas | Back buffer |
| Coverage count, `--bench` only | Graphics, one depth-only triangle | Depth | An occlusion query's count |

| Resource | Format | Size at 1920 × 1080 |
|---|---|---|
| Voxel records | 225,048 × `uint` | 0.9 MB |
| Rectangle index buffer | 1,536 × `uint16` | 3 KB |
| Palette | 16 × 16 bytes | 256 B |
| View depth | `R32_TYPELESS` (DSV `D32_FLOAT`, SRV `R32_FLOAT`) | 8.3 MB |
| Visibility | `R32G32_UINT` | 16.6 MB |
| HDR colour | `R16G16B16A16_FLOAT` | 16.6 MB |
| Shadow map | `R32_TYPELESS`, 4096² | 67.1 MB |
| Back buffers | 2 × `R8G8B8A8_UNORM` | 16.6 MB |
| Glyph atlas | `R8_UNORM`, 1024² | 1.0 MB |
| Overdraw count | `R32_UINT` | 8.3 MB |

That is about 136 MB in all, half of it the shadow map.

Two frames are in flight on one direct queue, with one command list and one fence value per frame. Per-frame constants live in a ring in an upload heap and are bound as root CBVs; static data is uploaded once. Barriers are ordinary resource-state transitions. A resize waits for the GPU to go idle and recreates the size-dependent targets.

Each pass is timed by timestamp queries: one before the first pass, and one after each pass's last draw or dispatch, so that a pass's time runs from its predecessor's end to its own and the transitions between two passes count to the second. The view splat is also bracketed by a pipeline-statistics query (`VSInvocations`, `PSInvocations`, `CPrimitives`), and `--bench`'s coverage count by an occlusion query. Each frame in flight has its own slot of query heaps and readback memory, which the frame's last command resolves into and the renderer reads when it next waits on that frame's fence: two frames late. The title and the panel on screen show the means over half a second (§13).

## 9. The splat pass

### 9.1 Draw structure

Each view issues one `DrawIndexedInstanced`. A static 16-bit index buffer describes 256 rectangles (1,024 vertices, 1,536 indices), and the instance count is ⌈*N* / 256⌉ — 880 for this model. The vertex shader derives voxel = instance × 256 + vertex / 4 and corner = vertex mod 4, and emits a degenerate rectangle for indices beyond *N*. This keeps the index buffer at 3 KB whatever the model, lets the post-transform cache share each rectangle's four vertices, and avoids instances that are four vertices long. There is no input layout; everything is pulled by index. Culling is off (a rectangle's winding means nothing), depth clipping is on, and MSAA is off.

### 9.2 Vertex shader

All four vertices of a voxel evaluate the same function. The paper accepts that redundancy, and at about 900,000 invocations per view it does not justify a compute pre-pass.

1. *Pose.* Unpack the record: centre *c* = `modelOrigin` + (x, y, z) + 0.5, half-extent *h* = 0.5. In the oriented permutation, the explosion pose (§12) replaces *c* and supplies a rotation *R*.
2. *Cull.* With bounding radius *r* = |*h*| = √3/2, emit a degenerate rectangle if the sphere lies wholly behind the near plane or outside a side plane.
3. *Bounds* (paper §4). If the sphere lies wholly beyond the near plane, bound its projection with the quadric method of Listing 4 (after Sigg et al. 2006), and keep that rectangle if it is at most 20 × 20 pixels. Otherwise take the precise path: project the eight corners if all lie beyond the near plane, or else clip the twelve edges against the near plane and project what survives. The AABB of the projected points is exact for the clipped box. Clamp the rectangle to the viewport (an empty result culls the voxel), then grow it by 1/64 pixel so that the rasterizer's fixed-point snapping and top-left rule can never exclude a pixel centre the box covers. The paper also clips large boxes against the four side planes for a tighter fit. We start with the viewport clamp and let the statistics of §14 decide whether the extra clipping pays.
4. *Depth.* The rectangle sits at the voxel's nearest point: *n* / (view depth of *c* − *r*) on the quadric path, and *n* / (least view depth among the projected points) on the precise path. Neither can be nearer than the near plane.
5. *Emit* the corner at the pre-divided position (x, y, depth, 1), with the voxel index as a `nointerpolation` attribute — joined in the oriented permutation by *c* and the three columns of *R*.

In the orthographic shadow permutation, step 3 collapses to an exact expression: in light space the rectangle is *c*.xy ± (|*R*| *h*).xy and the nearest depth is *c*.z − (|*R*| *h*).z.

### 9.3 Pixel shader

The ray starts at the camera. Its direction combines the camera's axes with the pixel's normalised device coordinates, scaled by the field of view and aspect ratio, and has a view-space depth component of exactly one. The ray parameter *t* is therefore the view depth, and depth is *n* / *t* with no further division. The intersection is Listing 5, with `canStartInBox` false and `oriented` set per permutation. On a miss, or when *t* < *n*, the pixel is discarded. Otherwise the shader writes `SV_DepthLessEqual` = min(*n* / *t*, `SV_Position.z`) and `SV_Target0` = (voxel index, octahedral normal). The normal is *R* × sign vector in the oriented permutation, and the sign vector itself otherwise.

Conservative depth lets a GPU keep hierarchical and early depth rejection even though the shader writes depth. Whether a given GPU actually does, with `discard` also present, is implementation behaviour. M5 measures it by comparing `PSInvocations` against a variant that writes plain `SV_Depth`, `PLAIN_DEPTH`, which makes no promise and so lets no depth test run early; `--bench` draws every frame with both (§13). It is not assumed. On the one GPU measured so far, an Adreno X1-85, early rejection works once the voxels separate and hardly at all on the intact station, and the invocations it saves cost no measurable time ([`SampleRendererPerformance.md`](SampleRendererPerformance.md)).

### 9.4 Listing 5 in HLSL

This is the normative sketch; the C++ twin in `NeuronCore` is its line-for-line counterpart.

```hlsl
// Majercik et al. 2018, Listing 5, in HLSL. ORIENTED and CAN_START_IN_BOX are 0/1 compile-time
// switches standing in for the paper's `const bool` arguments.

struct Box
{
  float3 center;
  float3 radius;    // half-extents
  float3 invRadius; // 1 / radius; read only when CAN_START_IN_BOX
  float3 axisX;     // the box's axes in world space: the columns of the paper's box.rot
  float3 axisY;
  float3 axisZ;
};

float MaxComponent(float3 _v)
{
  return max(max(_v.x, _v.y), _v.z);
}

// _invDirection is read only when ORIENTED == 0. On a hit, _distance is in units of |_direction|
// and _normal faces back along the ray.
bool IntersectBox(Box _box, float3 _origin, float3 _direction, float3 _invDirection, out float _distance, out float3 _normal)
{
  float3 origin = _origin - _box.center;
  float3 direction = _direction;
#if ORIENTED
  // World to box, i.e. the transpose of box.rot, spelled out rather than written as GLSL's v * M.
  origin = float3(dot(origin, _box.axisX), dot(origin, _box.axisY), dot(origin, _box.axisZ));
  direction = float3(dot(direction, _box.axisX), dot(direction, _box.axisY), dot(direction, _box.axisZ));
#endif

#if CAN_START_IN_BOX
  float winding = (MaxComponent(abs(origin) * _box.invRadius) < 1.0) ? -1.0 : 1.0;
#else
  float winding = 1.0;
#endif

  // HLSL's sign() returns int3; GLSL's returns a float vector.
  float3 sgn = -float3(sign(direction));

  // Distance to the three candidate front faces. A zero direction component divides by zero here,
  // and the tests below then rely on IEEE infinities and NaN comparisons (§4.2, item 6).
  float3 d = _box.radius * winding * sgn - origin;
#if ORIENTED
  d /= direction;
#else
  d *= _invDirection;
#endif

  // Is each candidate hit in front of the origin and on its face? The face includes its edges, so
  // that a ray on the seam between two voxels hits both (§4.2, item 12); the paper's test is strict.
  bool hitX = (d.x >= 0.0) && all(abs(origin.yz + direction.yz * d.x) <= _box.radius.yz);
  bool hitY = (d.y >= 0.0) && all(abs(origin.zx + direction.zx * d.y) <= _box.radius.zx);
  bool hitZ = (d.z >= 0.0) && all(abs(origin.xy + direction.xy * d.z) <= _box.radius.xy);

  // Keep exactly one axis, carrying the sign of the face normal.
  sgn = hitX ? float3(sgn.x, 0.0, 0.0) : (hitY ? float3(0.0, sgn.y, 0.0) : float3(0.0, 0.0, hitZ ? sgn.z : 0.0));

  _distance = (sgn.x != 0.0) ? d.x : ((sgn.y != 0.0) ? d.y : d.z);
#if ORIENTED
  _normal = _box.axisX * sgn.x + _box.axisY * sgn.y + _box.axisZ * sgn.z;
#else
  _normal = sgn;
#endif
  return any(sgn != 0.0);
}
```

## 10. Shadow pass

The shadow map is an orthographic view along the sun direction, taken from the file's `_inf` angles (50°, 50°). Their order is moot at equal values; the azimuth's zero direction is a MagicaVoxel convention the sample assumes rather than verifies (D12), and the direction is a parameter (ADR-008). The azimuth runs from MagicaVoxel's −Y towards +X, which is the engine's −Z towards +X (ADR-011). The frustum is fitted once to the union of the station's bounds and the explosion's flight envelope (§12), so it never moves and shadows do not swim. At 4096² it gives four texels per voxel edge across a 1,024-unit square, and the explosion's defaults keep the envelope inside that.

The splat shaders run in their orthographic permutation. The rays share one direction and start on the light's near plane; depth is *t* / range, written as `SV_DepthGreaterEqual` = max(*t* / range, `SV_Position.z`); the depth test is `LESS`; and the pipeline has no render target.

When the lighting pass samples the map, it offsets the shaded position by 1.5 texels along the normal and filters 3 × 3 taps with `SampleCmpLevelZero`. The file's `_area` (the sun's apparent size) is not modelled.

## 11. Lighting and tone mapping

For each pixel, the lighting pass first checks the visibility buffer. Where no voxel was hit, it intersects the camera ray with y = 0. A hit is ground — albedo from `_ground` (80, 80, 80), normal +Y, shadowed like everything else — and a miss is background, black as `_bg` says. Where a voxel was hit, the pass fetches the voxel's record for albedo and emissive scale, takes the normal from the visibility buffer and the position from depth, and computes the colour

*C* = albedo × (*E*sun × max(0, *N*·*S*) × shadow + ambient(*N*)) + albedo × emissive

where *S* is the direction towards the sun, *E*sun its intensity (`_i` 0.7), and ambient(*N*) = 0.7 × lerp(ground colour, white, ½ + ½ *N*y) from `_uni`'s intensity and colour. ADR-008 records how each value is read, and what a file that says something else gets. Emissive voxels light only themselves: nothing blooms, and nothing receives their light (D4). Without ambient occlusion the result will look flatter than MagicaVoxel's path tracer; D4 accepted that.

Tone mapping applies the exposure (`_film` `_expo` 1), then Stephen Hill's fit of the ACES reference and output transforms, and writes through an sRGB render-target view. The sRGB curve stands in for the file's gamma 2.2.

Debug views replace the final image with one of: albedo; normal; voxel index, hashed to a colour; the shadow map; or an overdraw heat map that counts splat pixel-shader invocations per pixel through a UAV in a debug permutation.

The overdraw view draws the view splat's `COUNT_OVERDRAW` variant, whose pixel shader adds one to its pixel's count in an `R32_UINT` UAV before anything else. A UAV write is a side effect that must happen whatever the depth test would decide, so the hardware may not test depth before such a shader unless the shader asks for it (the HLSL reference, `earlydepthstencil`), which one that writes depth cannot. The variant therefore runs for, and counts, every fragment of every rectangle over the pixel: the bounds' looseness and the depth complexity together, which is everything the standard pass could run. The heat map is black where nothing ran, then blue, cyan, green, yellow and red at 1, 3, 8, 24 and 64 invocations, evenly spaced in log₂, and white above 64. Its twin is `OverdrawViewColor` in `NeuronCore/DebugView.h` (R15).

## 12. The explosion

**Contract.** pose(*i*, *t*) → (centre, rotation) is a pure function of the voxel index *i*, the voxel's rest centre, a small parameter block, and the time *t* ≥ 0 since detonation. *t* = 0 is the intact station. The oriented splat vertex shader evaluates the function for both views, and its C++ twin evaluates it for tests. No state is carried between frames, so reassembly is simply *t* running back to zero, and the piece count costs no memory. Pieces pass through one another and come to rest in interpenetrating piles (D3).

**Randomness** comes from an integer hash (a PCG-style permutation) of (*i*, *k*), never from buffer order — unlike Listing 3, which assumes the voxels are shuffled.

**Translation.** The launch velocity points away from a blast origin (by default the model's centroid), with a speed that falls off with distance, an upward bias and hashed jitter. Flight is ballistic under gravity, which pulls along −Y. Each ground contact is a quadratic solved in closed form; it reflects the vertical velocity, along y, with restitution *e* and damps the horizontal one, in x and z. After a small fixed number of bounces the voxel rests. The whole piecewise trajectory, rest time *T*rest(*i*) included, follows from the launch values at every evaluation. A voxel whose centre starts below the bounding radius, one of the 22 that start on the ground, is launched up fast enough to clear it, and turns only once it has, so that its first flight is valid and no corner of it enters the ground (ADR-009).

**Rotation.** Each voxel spins about two coordinate axes chosen by hash, through total angles that are hashed multiples of 90°, eased to zero angular velocity. At rest a voxel is therefore in one of the cube's 24 symmetric orientations. A voxel is one colour on every face, so that orientation is indistinguishable from its unrotated self, and the rest state is seamless: flat, bottom face on the ground (centre at y = 0.5), with no snap. In flight, contacts use the bounding-sphere radius, so no rotation can push a corner into the ground. The spin runs only while the centre is at least that radius above the ground: it ends as the voxel falls through it for the last time, and the voxel falls its last 0.37 voxels square and lands at 0.5 (ADR-009).

**Envelope.** The parameter block bounds the highest apex and the farthest landing in closed form. The shadow frustum (§10) and the camera's framing use those bounds.

**Permutations.** At *t* = 0 every rotation is the identity, and the axis-aligned permutation draws. For *t* > 0 the oriented one does. The two must agree at *t* = 0 (§14).

**Controls.** Detonate (*t* runs forward), reassemble (*t* runs back to 0), pause, and time scale. Parameter defaults are tuned in M4 and recorded in ADR-009.

## 13. Application

The application, `Outpost.exe`, is a Win32 window, Unicode and per-monitor DPI aware, with a flip-model swap chain of two buffers and a frame-latency waitable object; vsync is on by default.

Rendering is 1:1 at the window's client size, in physical pixels; there is no internal render scale, because the technique's product is an exact edge per pixel and its cost is linear in pixels (paper Fig. 8). The window is borderless fullscreen on the monitor it starts on, at that monitor's resolution, which on the owner's 1920 × 1080 display is exactly the benchmark resolution; the owner asked for this on 2026-09-27, with no key to leave it. A larger monitor therefore costs proportionally more per frame. `--size WxH` opens an ordinary window of that client size instead, for a debugger to sit beside. Alt+F4 closes the application. `--bench` always renders at 1920 × 1080 whatever the window or display, because that is the resolution the paper measured at (§6.3, Table 4) and a benchmark whose resolution depends on the monitor is not a measurement.

At 1080 lines and the default framing a voxel spans about 2.5 pixels (§3). With no anti-aliasing (§16), edges will crawl while orbiting; the size-dependent targets of §8 total about 58 MB at this resolution.

The camera orbits the model (left drag), pans (right drag), dollies (wheel) and re-frames it (F), or the explosion's envelope once it has started (§12). Tab toggles a fly mode (WASD and mouse, Page Up and Page Down to rise and sink) for getting in among the debris. The other keys are:

- E detonate, R reassemble, Space pause, +/− time scale;
- 1 the lit image, 2–6 debug views (§11), G ground, V vsync;
- [ and ] the emissive gain (ADR-008);
- F1 key map, F2 the figures on screen, Alt+F4 quit.

The window's title carries the frame time, GPU milliseconds per pass, `PSInvocations`, the emissive scale that [ and ] tune, and the explosion's time and scale. Borderless fullscreen draws no title bar, so the window shows the same figures itself: one per line, on a translucent panel in the top-left corner, which F2 hides and shows (D13, ADR-010).

They are drawn by the canvas, the 2D overlay the renderer draws last and the surface a HUD will draw on. DirectWrite lays the text out and rasterizes each glyph once into an atlas, and Direct3D 12 draws a quad over each glyph's bitmap, texel for texel; Direct2D takes no part. The text is grayscale antialiased, not ClearType: three coverages per pixel cannot be blended over a 3D image with one alpha.

Command line:

- `--vox <path>` — the model; default `GameData\MilitaryStation.vox` beside the executable, copied there by the build;
- `--size WxH` — the window's client size;
- `--warp`, `--adapter <n>` — adapter choice;
- `--d3d-debug` — the debug layer in Release. Debug builds always ask for it; where the Windows Graphics Tools that carry it are not installed, the window's title says so and the program runs without it;
- `--gbv` — GPU-based validation;
- `--bench <seconds>` — a fixed camera path and explosion timeline, with per-pass timings written to CSV.

`--bench` runs a timeline of `<seconds>` at 60 frames a second, one frame of it per rendered frame however long that frame takes, so that every run draws the same frames; a fast GPU finishes sooner. It renders at 1920 × 1080 with vsync off. The camera circles the model once at the default framing. The model stands intact for the first quarter of the timeline; over the middle half the explosion runs from its detonation to the envelope's rest time; for the last quarter it lies at rest.

Every frame is drawn twice, back to back: with the view splat's conservative depth and with plain `SV_Depth` (§9.3). Both count the pixels a voxel covers (§14). After 120 warm-up frames, every drawn frame goes to `Outpost-bench-<date>-<time>.csv` in the working directory: its GPU time per pass, its view-splat pipeline statistics and its covered pixels. A summary goes to a `.txt` beside the CSV and to a message box:
- the median, mean and 95th percentile of each pass in each variant;
- `PSInvocations` per covered pixel;
- plain depth over conservative for each frame's pair, in `PSInvocations` and in view-splat time;
- the view splat by phase: intact, in flight and at rest.

The canvas shows the run's progress, as a pass of its own. The summary counts only the frame's four passes of §8.

Loader failures are values (§7.1). A Direct3D failure during initialisation, or a device removal, ends the program with a message giving its `HRESULT` and the file and line that checked it (`winrt::check_hresult`, `AGENTS.md` R12). DRED is enabled, so a device removal also reports breadcrumbs and the faulting address.

## 14. Verification

`NeuronCoreTests` (CPU, deterministic) cover:

- the reader, against synthetic in-memory files for every rule in §7.1, and against the real file, reproducing the counts, size, translation, palette and emissive entries of §3;
- the ray-box twin, against a brute-force slab test with explicit face tracking, over seeded random boxes and rays, aligned and oriented, plus the edge cases: exactly-zero direction components, rays through edges and corners, and an origin inside the box (the documented wrong answer with `canStartInBox` false, the exit point with true);
- bounds: for seeded boxes and cameras, both the quadric and the precise rectangles contain every projected (clipped) corner, and no hit inside a rectangle is nearer than the rectangle's depth;
- pose: the rest pose at *t* = 0, continuity across contacts, no corner below the ground, a cube-symmetric rotation with the centre at y = 0.5 once *t* ≥ *T*rest, and identical results for identical inputs;
- handedness: a voxel on +X lands in the right half of the image and one on +Y in the top half, which is what a mirrored basis gets wrong and no comparison of the GPU with its twin can see (ADR-011);
- the station as the tracer drew it, and as the explosion and the lighting moved and lit it, before the engine moved to Direct3D's axes: pinned once, and never re-pinned (ADR-011);
- packing round trips for records and octahedral normals.

`NeuronClientTests` run on WARP at FL 12_1. If the device cannot be created, the suite fails; it does not skip. They cover:

- the layout echo of §7.4;
- the canvas: text in two faces and translucent fills, drawn and compared pixel for pixel with its twin's composite of the same quads over the atlas's CPU copy (§13), and a glyph rasterized once per face and size;
- the view splat's measurement variants, which draw what the standard pass draws, and the overdraw count, which lies in every pixel between the numbers of the twin's rectangles that cover its centre for certain and that might;
- the frame queries: the coverage count equals the pixels a voxel wrote, `PSInvocations` is at least that, `VSInvocations` is at least one per vertex drawn, and neither plain depth nor the overdraw variant runs the pixel shader less often than conservative depth;
- the overdraw view against its twin;
- the intact station from several fixed cameras at 161 × 91, with the visibility buffer read back and compared per pixel with the reference tracer — a 3D DDA through the dense grid that uses the ray-box twin per occupied cell. The odd resolution gives a level camera a whole row and column of rays with exactly-zero components (§4.2, item 6);
- a shadow map with the sun straight overhead, where every ray is axis-parallel;
- the aligned and oriented permutations at *t* = 0;
- a synthetic 8³ model at several values of *t*, against brute-force intersection of every box;
- a synthetic shadow map against the tracer's orthographic depth.

Comparison rule: CPU and GPU agree to rounding, not bit for bit. MSVC contracts to FMA under `/arch:AVX2` (`AGENTS.md` §3) and GPUs round division differently, so no test assumes bit equality across devices. Away from silhouettes, any mismatch fails the test. On silhouettes, each test carries an explicit bound, set from the first measured run and written down with its reason.

Manual acceptance covers what CI cannot. From M2 on, the owner runs the sample on hardware at every milestone, and at M3 judges the lighting by eye; there is no comparison against MagicaVoxel renders (D12).

Measurement (M5) covers per-pass timestamps; `PSInvocations` against covered pixels; the overdraw view; and `--bench`. Figures quoted in ADRs say how they were measured. `PSInvocations` against covered pixels is what the GPU shaded per visible pixel, in whatever unit that GPU counts. The Adreno X1-85 counts every lane of every 2×2 quad a triangle touches, helper lanes included, so it is not the tightness of the bounds. The tightness is the twin's figure: the share of a rectangle's pixels whose ray hits its box ([`SampleRendererPerformance.md`](SampleRendererPerformance.md)).

## 15. Milestones

| | Delivers | Done when |
|---|---|---|
| M0 | `Build/CheckFormat.py`, `Build/CheckProjectFiles.py` (with the HLSL rules), `Build/RunClangTidy.py`; `Outpost.Voxel.slnx` with the five projects; `SuiteSmoke` in both suites; `HeaderFilterRegex`; R14–R17 and the layout in `AGENTS.md`; CI guards removed; ADR-001 | CI is green with every gate running |
| M1 | `NeuronCore` (then `VoxelCore`): reader, model, maths, CPU twins, reference tracer; ADR-002 | `NeuronCoreTests` green; §3's pinned figures reproduced |
| M2 | The engine and game layout; window, device (hardware and WARP), aligned view splat, visibility buffer, debug views; ADR-003 to ADR-006 | `NeuronClientTests` green on WARP in CI; the owner sees the station on hardware |
| M3 | Shadow splat, lighting, ground, emissive, tone mapping; ADR-008 | Shadow tests green; the owner accepts the look |
| M4 | Pose in HLSL and C++, oriented permutations, time controls; ADR-009 | Explosion tests green; the owner has detonated and reassembled the station |
| M5 | Timings, pipeline statistics, overdraw view, `--bench` | A measured performance note ([`SampleRendererPerformance.md`](SampleRendererPerformance.md)), and an ADR for any decision it drives |

M0 is repository groundwork that `AGENTS.md` §6 already asks for. It is listed here because nothing after it can be verified without it.

## 16. Risks and open questions

**HLSL floating point and Listing 5** (§4.2, item 6). The paper's robustness argument assumes IEEE behaviour the compiler may not provide. The tests hit the exact cases, and a failure is fixed by an explicit guard.

**MagicaVoxel conventions the file cannot settle**: the pivot's rounding, the reference direction of the sun angles, and the emissive mapping. All of them are parameters with stated defaults. With no MagicaVoxel comparison (D12) they stay unverified; a wrong default shows up as a station half a voxel off the ground, a sun from a different azimuth, or emissives that glow too much or too little, and each is a one-line parameter change.

**Early rejection under conservative depth** is hardware behaviour. M5 measures it; nothing assumes it.

**WARP's shader model.** The WARP guide documents FL 12_1 but says nothing about Shader Model 6.7. On the owner's machine, in-box WARP runs the 6.7 suite (ADR-007); CI's first run after the change answers it for the runner. If WARP lacks it, that is a blocker to raise, not a suite to skip.

**Aliasing.** The first version has no anti-aliasing, so silhouettes and voxels smaller than a pixel will crawl in motion. If that matters, the options are the paper's route (a ray per MSAA sample) or TAA; either is an ADR.

**Flat lighting and interpenetrating piles** are accepted consequences of D4 and D3.

The owner answered the open questions on 2026-09-27:

1. FL 12_1 stays a hard floor, although it refuses FL 11_x and 12_0 hardware that could run this design (§5).
2. There is no comparison against MagicaVoxel renders (D12); M3 is accepted by eye.
3. The loader rejects `.vox` features this file does not use, such as rotated nodes or more than 16 colours, by name (§7.1).
4. The owner's display is 1920 × 1080, and the resolution policy of §13 (D11) applies.
5. Seams are watertight: Listing 5's face tests include their edges (§4.2, item 12).
6. The window is borderless fullscreen, with no key to leave it; `--size` still opens a window (§13, D11).

## 17. Conformance rules and expected ADRs

`AGENTS.md` reserves R14 onward for rules with a design source. The owner accepted these four on 2026-09-27, and they are R14–R17 in `AGENTS.md`, which is where they are maintained:

- **R14 — The voxel record is 32 bits and the palette has 16 entries.** Eight bits per coordinate and four for colour (D5). Widening either is a format change and needs an ADR.
- **R15 — No algorithm exists only on the GPU.** Ray-box, bounds, pose and packing each have a C++ twin in `NeuronCore`, and a test compares the two. The canvas's twin is in `NeuronClient`, because only the client draws a canvas (ADR-010).
- **R16 — Shared layouts have one source.** The C++ struct, with `static_assert`s on size and offsets, is the truth; its HLSL mirror is written once, in a `.hlsli`; the echo test proves they agree.
- **R17 — HLSL follows §1.** A `.hlsl` file holds one entry point and nothing but switches and an include; algorithms live in `.hlsli` files; the naming table applies; semantics and intrinsics keep the SDK's spelling (`SV_Position`).

Each expected ADR lands in the commit that implements it:

- ADR-001, repository layout (M0);
- ADR-002, voxel record and palette (M1);
- ADR-003, engine and game layout (M2), which supersedes ADR-001's project layout;
- ADR-004, PIX event runtime (M2), recorded after the owner added the package on `main`;
- ADR-005, shader toolchain — DXC through `FxCompile`, SM 6.0, embedded headers, identical flags in both configurations (M2);
- ADR-006, depth conventions (M2);
- ADR-007, Shader Model 6.7 and shader file names (M2), which amends ADR-005;
- ADR-008, lighting read from the file: the `rOBJ` values, the sun's angles and the emissive mapping (M3);
- ADR-009, explosion motion model and its defaults (M4);
- ADR-010, the canvas: DirectWrite text drawn straight by Direct3D 12, recorded when the owner asked for text on screen (2026-09-28);
- ADR-011, the engine's axes: Direct3D's, left-handed with +Y up, with the `.vox` reader the one converter from MagicaVoxel's (N-M0 of `Design/NeuronVoxelFormat.md`, 2026-09-28).

## 18. References

- A. Majercik, C. Crassin, P. Shirley, M. McGuire. *A Ray-Box Intersection Algorithm and Efficient Dynamic Voxel Rendering.* JCGT 7(3):66–81, 2018. `Majercik2018Voxel.pdf` at the repository root, distributed under CC BY-ND 3.0 as its last page states. The supplement holds the complete GLSL and UE4 code, including the hand-optimised special cases Listing 5's caption mentions: http://www.jcgt.org/published/0007/03/04/supplement.zip
- C. Sigg, T. Weyrich, M. Botsch, M. Gross. *GPU-Based Ray-Casting of Quadratic Surfaces.* SPBG 2006 — the quadric bounds of Listing 4.
- MagicaVoxel file format notes: https://github.com/ephtracy/voxel-model (`MagicaVoxel-file-format-vox.txt`, `MagicaVoxel-file-format-vox-extension.txt`).
- Direct3D hardware feature levels: https://learn.microsoft.com/windows/win32/direct3d12/hardware-feature-levels
- WARP guide: https://learn.microsoft.com/windows/win32/direct3darticles/directx-warp
- Compiling shaders; Visual Studio switches to `dxc.exe` for Shader Model 6: https://learn.microsoft.com/windows/win32/direct3dhlsl/dx-graphics-hlsl-part1
