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
[[nodiscard]] D3D12_SHADER_BYTECODE LightingComputeShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ToneMapVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ToneMapPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE DebugViewVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE DebugViewPixelShader() noexcept;

} // namespace NeuronClient
