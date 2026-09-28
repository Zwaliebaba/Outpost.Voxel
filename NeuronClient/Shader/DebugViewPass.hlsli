#pragma once

// The debug view pass (Design/Archive/SampleRenderer.md §11): one triangle over the viewport, and for each pixel the color the
// chosen view gives it: DebugViewColor of its entry in the visibility buffer, the shadow map, or the overdraw count.

#include "DebugView.hlsli"
#include "FullScreen.hlsli"
#include "Packing.hlsli"
#include "PaletteConstants.hlsli"
#include "ViewConstants.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<PaletteConstants> g_palette : register(b1);
StructuredBuffer<uint> g_records : register(t0);
Texture2D<uint2> g_visibility : register(t1);
Texture2D<float> g_shadowMap : register(t2);
Texture2D<uint> g_overdraw : register(t3);

// One 32-bit root constant: the DEBUG_VIEW_* value to show.
cbuffer DebugViewSelection : register(b2)
{
  uint g_debugView;
};

struct DebugViewTarget
{
  float4 color : SV_Target0;
};

DebugViewTarget DebugViewPixel(FullScreenVaryings _varyings)
{
  uint2 pixel = uint2(_varyings.position.xy);
  float3 color = float3(0.0, 0.0, 0.0);
  if (g_debugView == DEBUG_VIEW_SHADOW_MAP)
  {
    uint mapWidthPixels = 0u;
    uint mapHeightPixels = 0u;
    g_shadowMap.GetDimensions(mapWidthPixels, mapHeightPixels);
    uint2 texel = uint2(0u, 0u);
    if (ShadowMapViewTexel(pixel, g_view.widthPixels, g_view.heightPixels, mapWidthPixels, mapHeightPixels, texel))
    {
      color = ShadowMapViewColor(g_shadowMap.Load(int3(int2(texel), 0)));
    }
  }
  else if (g_debugView == DEBUG_VIEW_OVERDRAW)
  {
    color = OverdrawViewColor(g_overdraw.Load(int3(int2(pixel), 0)));
  }
  else
  {
    uint2 visibility = g_visibility.Load(int3(int2(pixel), 0));
    float3 albedo = float3(0.0, 0.0, 0.0);
    if (visibility.x != NO_VOXEL)
    {
      albedo = g_palette.materials[UnpackVoxelRecord(g_records[visibility.x]).color].albedo;
    }
    color = DebugViewColor(g_debugView, visibility.x, UnpackOctahedralNormal(visibility.y), albedo);
  }
  DebugViewTarget target;
  target.color = float4(color, 1.0);
  return target;
}
