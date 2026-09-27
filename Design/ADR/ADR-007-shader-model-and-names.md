# ADR-007 — Shader Model 6.7 and shader file names

**Status:** accepted, 2026-09-27 · **Amends:** [ADR-005](ADR-005-shader-toolchain.md) · **Design:** [`Design/SampleRenderer.md`](../SampleRenderer.md) D9, §5

## Context

ADR-005 compiled every shader for Shader Model 6.0, the floor design D9 set, and left the names of `.hlsl` files to R17's PascalCase alone, so a stage was spelled out in full: `ViewSplatAlignedPixel.hlsl`, `LayoutEchoCompute.hlsl`. On 2026-09-27 the owner asked for two changes: every shader compiled for Shader Model 6.7, and every `.hlsl` named for its stage with the suffix its profile uses.

## Decision

**Every shader is compiled for Shader Model 6.7.** `FxCompile`'s `ShaderModel` is `6.7` in every project that compiles HLSL, in both configurations, so DXC builds with the `vs_6_7`, `ps_6_7` and `cs_6_7` profiles. The runtime floor rises with it: `GraphicsDevice` takes an adapter only if `D3D12_FEATURE_SHADER_MODEL` reports at least `D3D_SHADER_MODEL_6_7`, and names what an adapter lacks otherwise. A pipeline built from 6.7 bytecode on a device that reports less would fail at creation, so the two numbers move together. Like FL 12_1, the floor is the owner's contract rather than something the code needs: no shader uses a 6.1–6.7 feature yet.

- Measured on the owner's machine (Windows 11 build 26200, Windows SDK 10.0.26100's `dxc.exe`, in-box D3D12 runtime, no Agility SDK): the generated headers record `-T ps_6_7`, `vs_6_7` and `cs_6_7`, and all 71 tests pass, including the `NeuronClientTests` suite that creates the device and runs the pipelines on WARP. That answers design §16's question about WARP's shader model for this machine; CI's runner answers it for CI.

**A `.hlsl` file is named `<Shader><Stage>.hlsl`**, the stage spelled as its profile spells it: `VS` for a vertex shader, `PS` for a pixel shader, `CS` for a compute shader. The generated header takes the file's name, and its `VariableName` is the same name in `UPPER_CASE`: `ViewSplatAlignedPS.hlsl` produces `Shaders\ViewSplatAlignedPS.h` defining `VIEW_SPLAT_ALIGNED_PS`. `.hlsli` files are not stage-specific and keep plain names. Entry-point names are not governed by this rule.

The five shaders were renamed: `DebugViewPixel` → `DebugViewPS`, `DebugViewVertex` → `DebugViewVS`, `ViewSplatAlignedPixel` → `ViewSplatAlignedPS`, `ViewSplatAlignedVertex` → `ViewSplatAlignedVS`, `LayoutEchoCompute` → `LayoutEchoCS`.

**`Build/CheckProjectFiles.py` enforces both**: `ShaderModel` stated as `6.7`, and every `FxCompile` item's file name ending in the suffix of its `ShaderType`.

## What this forecloses

- Adapters and drivers that report Shader Model 6.0 through 6.6, which the sample could otherwise have run on.
- A shader built for any other Shader Model, or one project on a different model from another.
- A stage spelled out in full in a `.hlsl` file name, or a suffix that disagrees with the file's `ShaderType`.
