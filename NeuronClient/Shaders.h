#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

namespace NeuronClient
{

// The shaders in Shader/, compiled by FxCompile into headers and embedded here (Design/ADR/ADR-005).
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatAlignedVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatAlignedPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ShadowSplatAlignedVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ShadowSplatAlignedPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatOrientedVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatOrientedPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ShadowSplatOrientedVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ShadowSplatOrientedPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatAlignedPlainDepthPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatOrientedPlainDepthPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatAlignedOverdrawPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatOrientedOverdrawPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE LightingComputeShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE GasShellComputeShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE SkyVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE SkyPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE StarVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE StarPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE BloomDownComputeShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE BloomUpComputeShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ToneMapVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ToneMapPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE DebugViewVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE DebugViewPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE CoverageVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE CanvasVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE CanvasPixelShader() noexcept;

} // namespace NeuronClient
