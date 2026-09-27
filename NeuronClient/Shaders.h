#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

namespace NeuronClient
{

// The shaders in Shader/, compiled by FxCompile into headers and embedded here (Design/ADR/ADR-005).
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatAlignedVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE ViewSplatAlignedPixelShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE DebugViewVertexShader() noexcept;
[[nodiscard]] D3D12_SHADER_BYTECODE DebugViewPixelShader() noexcept;

} // namespace NeuronClient
