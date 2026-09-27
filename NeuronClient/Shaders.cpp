#include "pch.h"

#include "Shaders.h"

namespace NeuronClient
{
namespace
{

// Each header defines one array of bytecode, named by its FxCompile item's VariableName.
#include "Shaders/DebugViewPS.h"
#include "Shaders/DebugViewVS.h"
#include "Shaders/ViewSplatAlignedPS.h"
#include "Shaders/ViewSplatAlignedVS.h"

} // namespace

D3D12_SHADER_BYTECODE ViewSplatAlignedVertexShader() noexcept
{
  return {VIEW_SPLAT_ALIGNED_VS, sizeof(VIEW_SPLAT_ALIGNED_VS)};
}

D3D12_SHADER_BYTECODE ViewSplatAlignedPixelShader() noexcept
{
  return {VIEW_SPLAT_ALIGNED_PS, sizeof(VIEW_SPLAT_ALIGNED_PS)};
}

D3D12_SHADER_BYTECODE DebugViewVertexShader() noexcept
{
  return {DEBUG_VIEW_VS, sizeof(DEBUG_VIEW_VS)};
}

D3D12_SHADER_BYTECODE DebugViewPixelShader() noexcept
{
  return {DEBUG_VIEW_PS, sizeof(DEBUG_VIEW_PS)};
}

} // namespace NeuronClient
