#include "pch.h"

#include "Shaders.h"

namespace NeuronClient
{
namespace
{

// Each header defines one array of bytecode, named by its FxCompile item's VariableName.
#include "Shaders/DebugViewPixel.h"
#include "Shaders/DebugViewVertex.h"
#include "Shaders/ViewSplatAlignedPixel.h"
#include "Shaders/ViewSplatAlignedVertex.h"

} // namespace

D3D12_SHADER_BYTECODE ViewSplatAlignedVertexShader() noexcept
{
  return {VIEW_SPLAT_ALIGNED_VERTEX, sizeof(VIEW_SPLAT_ALIGNED_VERTEX)};
}

D3D12_SHADER_BYTECODE ViewSplatAlignedPixelShader() noexcept
{
  return {VIEW_SPLAT_ALIGNED_PIXEL, sizeof(VIEW_SPLAT_ALIGNED_PIXEL)};
}

D3D12_SHADER_BYTECODE DebugViewVertexShader() noexcept
{
  return {DEBUG_VIEW_VERTEX, sizeof(DEBUG_VIEW_VERTEX)};
}

D3D12_SHADER_BYTECODE DebugViewPixelShader() noexcept
{
  return {DEBUG_VIEW_PIXEL, sizeof(DEBUG_VIEW_PIXEL)};
}

} // namespace NeuronClient
