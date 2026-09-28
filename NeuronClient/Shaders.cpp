#include "pch.h"

#include "Shaders.h"

namespace NeuronClient
{
namespace
{

// Each header defines one array of bytecode, named by its FxCompile item's VariableName.
#include "Shaders/BloomDownCS.h"
#include "Shaders/BloomUpCS.h"
#include "Shaders/CanvasPS.h"
#include "Shaders/CanvasVS.h"
#include "Shaders/CoverageVS.h"
#include "Shaders/DebugViewPS.h"
#include "Shaders/DebugViewVS.h"
#include "Shaders/LightingCS.h"
#include "Shaders/ShadowSplatAlignedPS.h"
#include "Shaders/ShadowSplatAlignedVS.h"
#include "Shaders/ShadowSplatOrientedPS.h"
#include "Shaders/ShadowSplatOrientedVS.h"
#include "Shaders/SkyPS.h"
#include "Shaders/SkyVS.h"
#include "Shaders/StarPS.h"
#include "Shaders/StarVS.h"
#include "Shaders/ToneMapPS.h"
#include "Shaders/ToneMapVS.h"
#include "Shaders/ViewSplatAlignedOverdrawPS.h"
#include "Shaders/ViewSplatAlignedPS.h"
#include "Shaders/ViewSplatAlignedPlainDepthPS.h"
#include "Shaders/ViewSplatAlignedVS.h"
#include "Shaders/ViewSplatOrientedOverdrawPS.h"
#include "Shaders/ViewSplatOrientedPS.h"
#include "Shaders/ViewSplatOrientedPlainDepthPS.h"
#include "Shaders/ViewSplatOrientedVS.h"

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

D3D12_SHADER_BYTECODE ViewSplatOrientedVertexShader() noexcept
{
  return {VIEW_SPLAT_ORIENTED_VS, sizeof(VIEW_SPLAT_ORIENTED_VS)};
}

D3D12_SHADER_BYTECODE ViewSplatOrientedPixelShader() noexcept
{
  return {VIEW_SPLAT_ORIENTED_PS, sizeof(VIEW_SPLAT_ORIENTED_PS)};
}

D3D12_SHADER_BYTECODE ShadowSplatOrientedVertexShader() noexcept
{
  return {SHADOW_SPLAT_ORIENTED_VS, sizeof(SHADOW_SPLAT_ORIENTED_VS)};
}

D3D12_SHADER_BYTECODE ShadowSplatOrientedPixelShader() noexcept
{
  return {SHADOW_SPLAT_ORIENTED_PS, sizeof(SHADOW_SPLAT_ORIENTED_PS)};
}

D3D12_SHADER_BYTECODE ViewSplatAlignedPlainDepthPixelShader() noexcept
{
  return {VIEW_SPLAT_ALIGNED_PLAIN_DEPTH_PS, sizeof(VIEW_SPLAT_ALIGNED_PLAIN_DEPTH_PS)};
}

D3D12_SHADER_BYTECODE ViewSplatOrientedPlainDepthPixelShader() noexcept
{
  return {VIEW_SPLAT_ORIENTED_PLAIN_DEPTH_PS, sizeof(VIEW_SPLAT_ORIENTED_PLAIN_DEPTH_PS)};
}

D3D12_SHADER_BYTECODE ViewSplatAlignedOverdrawPixelShader() noexcept
{
  return {VIEW_SPLAT_ALIGNED_OVERDRAW_PS, sizeof(VIEW_SPLAT_ALIGNED_OVERDRAW_PS)};
}

D3D12_SHADER_BYTECODE ViewSplatOrientedOverdrawPixelShader() noexcept
{
  return {VIEW_SPLAT_ORIENTED_OVERDRAW_PS, sizeof(VIEW_SPLAT_ORIENTED_OVERDRAW_PS)};
}

D3D12_SHADER_BYTECODE LightingComputeShader() noexcept
{
  return {LIGHTING_CS, sizeof(LIGHTING_CS)};
}

D3D12_SHADER_BYTECODE SkyVertexShader() noexcept
{
  return {SKY_VS, sizeof(SKY_VS)};
}

D3D12_SHADER_BYTECODE SkyPixelShader() noexcept
{
  return {SKY_PS, sizeof(SKY_PS)};
}

D3D12_SHADER_BYTECODE StarVertexShader() noexcept
{
  return {STAR_VS, sizeof(STAR_VS)};
}

D3D12_SHADER_BYTECODE StarPixelShader() noexcept
{
  return {STAR_PS, sizeof(STAR_PS)};
}

D3D12_SHADER_BYTECODE BloomDownComputeShader() noexcept
{
  return {BLOOM_DOWN_CS, sizeof(BLOOM_DOWN_CS)};
}

D3D12_SHADER_BYTECODE BloomUpComputeShader() noexcept
{
  return {BLOOM_UP_CS, sizeof(BLOOM_UP_CS)};
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

D3D12_SHADER_BYTECODE CoverageVertexShader() noexcept
{
  return {COVERAGE_VS, sizeof(COVERAGE_VS)};
}

D3D12_SHADER_BYTECODE CanvasVertexShader() noexcept
{
  return {CANVAS_VS, sizeof(CANVAS_VS)};
}

D3D12_SHADER_BYTECODE CanvasPixelShader() noexcept
{
  return {CANVAS_PS, sizeof(CANVAS_PS)};
}

} // namespace NeuronClient
