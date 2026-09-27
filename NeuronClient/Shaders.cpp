#include "pch.h"

#include "Shaders.h"

namespace NeuronClient
{
namespace
{

// Each header defines one array of bytecode, named by its FxCompile item's VariableName.
#include "Shaders/DebugViewPS.h"
#include "Shaders/DebugViewVS.h"
#include "Shaders/LightingCS.h"
#include "Shaders/ShadowSplatAlignedPS.h"
#include "Shaders/ShadowSplatAlignedVS.h"
#include "Shaders/ToneMapPS.h"
#include "Shaders/ToneMapVS.h"
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

D3D12_SHADER_BYTECODE ShadowSplatAlignedVertexShader() noexcept
{
  return {SHADOW_SPLAT_ALIGNED_VS, sizeof(SHADOW_SPLAT_ALIGNED_VS)};
}

D3D12_SHADER_BYTECODE ShadowSplatAlignedPixelShader() noexcept
{
  return {SHADOW_SPLAT_ALIGNED_PS, sizeof(SHADOW_SPLAT_ALIGNED_PS)};
}

D3D12_SHADER_BYTECODE LightingComputeShader() noexcept
{
  return {LIGHTING_CS, sizeof(LIGHTING_CS)};
}

D3D12_SHADER_BYTECODE ToneMapVertexShader() noexcept
{
  return {TONE_MAP_VS, sizeof(TONE_MAP_VS)};
}

D3D12_SHADER_BYTECODE ToneMapPixelShader() noexcept
{
  return {TONE_MAP_PS, sizeof(TONE_MAP_PS)};
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
