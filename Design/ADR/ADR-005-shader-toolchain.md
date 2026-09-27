# ADR-005 — Shader toolchain

**Status:** accepted, 2026-09-27 · **Lands with:** M2 of [`Design/SampleRenderer.md`](../SampleRenderer.md) (§15) · **Amended by:** [ADR-007](ADR-007-shader-model-and-names.md), Shader Model 6.7

## Context

M2 brings the first shaders: the view splat and the debug view in `NeuronClient`, and the layout echo in `NeuronClientTests`. The design fixes the outline in §5 and §6.2: `FxCompile`, Shader Model 6.0 through DXC, bytecode embedded in headers, one entry point per `.hlsl` file (R17), and flags that read the same in Debug and Release (`AGENTS.md` §3). It leaves the flags themselves open, and two questions it names: whether the compiler keeps the IEEE behavior Listing 5 depends on (§16), and whether clang-format can lay out HLSL.

## Decision

**Headers, not files beside the executable.** Each `.hlsl` is an `FxCompile` item naming its `ShaderType`, `EntryPointName` and `VariableName`, the last in `UPPER_CASE` because the header defines a constant array (R3). `ShaderModel` 6.0 makes MSBuild use the Windows SDK's `dxc.exe`. The output is a header at `$(IntDir)Shaders\<File>.h` and no `.cso` (`ObjectFileOutput` is empty). One translation unit per project includes the headers: `NeuronClient/Shaders.cpp`, and the layout echo test. DXC does not create the folder, so a `MakeShaderHeaderFolder` target does, before `FxCompile`. `$(IntDir)` is on the C++ include path, `Build/RunClangTidy.py` expands it from the project's own properties, and the generated headers lie outside `.clang-tidy`'s `HeaderFilterRegex`.

**One set of flags, in both configurations:** optimization on, `-WX`, and `-Zi -Qembed_debug -Gis`.

- `-Zi -Qembed_debug` puts the debug information PIX reads inside the shader. Without `-Qembed_debug`, DXC prints `warning: no output provided for debug - embedding PDB in shader container`. It exits with 0, but MSBuild's `/warnaserror` promotes the line.
- `-Gis` is IEEE strictness. Without it, DXC marks every floating-point operation `fast`, which lets a driver assume no infinity and no NaN. Listing 5 is correct only because of both: a zero direction component makes an infinity, and the NaNs that follow must fail every test. A driver may rewrite `abs(x) <= r` as `!(abs(x) > r)`, which holds for a NaN. The face test for the zero axis then passes, and because that axis's normal sign is zero, the hit it reports is no hit at all, so it masks the true hit on another face. WARP may well keep IEEE behavior regardless, so a green CI would not have shown this; the flag is the defense.
- Measured with the Linux build of DXC 1.8.2405, by reading the DXIL (`-Fc`) of `ViewSplatAlignedPixel` as it lands in M2. Without `-Gis`, 87 operations carry the `fast` flag, `fdiv fast` and `fcmp fast` among them; with it, none do. The embedded debug information grows the same shader from 6,440 to 41,172 bytes of bytecode.
- The cost is that a driver may no longer approximate a division. M5 measures it.

**`Build/CheckProjectFiles.py` enforces this** for every project with an `FxCompile` item: the settings above, in both configurations, and a stage, an entry point and an `UPPER_CASE` variable for every shader.

**The language version is not pinned.** The SDK's DXC defaults to HLSL 2021. The code means the same under 2018: no function that writes an `out` parameter stands on the right of `&&` or `||`, which 2018 evaluates in full.

**HLSL is formatted by `.clang-format`, as C++, and `Build/CheckFormat.py` checks it.** Measured with clang-format 18.1.3 on the M2 shaders, it lays out HLSL correctly: register bindings, semantics on struct members and parameters, `[unroll]`, `#if` blocks. The one exception is a semantic after a parameter list, `float4 F(...) : SV_Position`, which it breaks across lines as if it were a constructor's initializer list. So an entry point returns a struct whose members carry the output semantics (`AGENTS.md` §4).

**One rule DXIL validation adds.** A pixel shader that writes conservative depth must read `SV_Position` with `noperspective centroid` interpolation. `Shader/Splat.hlsli` declares it so; without MSAA, the centroid is the pixel's centre.

## What this forecloses

- Compiling shaders at run time, and `.cso` files beside the executable.
- `fast` floating point in any shader, until an ADR measures a gain and shows Listing 5 still correct under it.
- A shader built with flags other than these, or differently in Debug and Release.
- A semantic after a function's parameter list.
