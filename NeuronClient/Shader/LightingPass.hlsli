#pragma once

// The lighting pass (Design/Archive/SampleRenderer.md §8, §11): a thread per pixel in 8 × 8 groups, from the view splat's depth
// and visibility and the shadow map into the HDR color target. A pixel's voxel id leads through its placement to its
// record and its model's palette (Design/Archive/SpaceScene.md §7.3).

#include "Lighting.hlsli"
#include "PaletteConstants.hlsli"
#include "Placement.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<ShadowViewConstants> g_shadowView : register(b1);
ConstantBuffer<LightingConstants> g_lighting : register(b2);
StructuredBuffer<uint> g_records : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D<uint2> g_visibility : register(t2);
Texture2D<float> g_shadowMap : register(t3);
StructuredBuffer<PlacementConstants> g_placements : register(t4);
StructuredBuffer<PaletteConstants> g_palettes : register(t5);
SamplerComparisonState g_shadowSampler : register(s0);
RWTexture2D<float4> g_color : register(u0);

// §8's group size; NeuronClient/LightingPass.h relies on the same number.
static const uint LIGHTING_GROUP_PIXELS = 8;

// The input's semantic is on a struct member: clang-format breaks one on a parameter of an entry point that carries an
// attribute (Design/ADR/ADR-005).
struct LightingThread
{
  uint3 pixel : SV_DispatchThreadID;
};

[numthreads(LIGHTING_GROUP_PIXELS, LIGHTING_GROUP_PIXELS, 1)] void LightPixels(LightingThread _thread)
{
  uint2 pixel = _thread.pixel.xy;
  if (pixel.x >= g_view.widthPixels || pixel.y >= g_view.heightPixels)
  {
    return;
  }
  int3 location = int3(pixel, 0);
  uint2 visibility = g_visibility.Load(location);
  float3 albedo = float3(0.0, 0.0, 0.0);
  float emissiveScale = 0.0;
  uint record = 0u;
  uint paletteIndex = 0u;
  if (visibility.x != NO_VOXEL && FindVoxel(g_placements, g_lighting.placementCount, visibility.x, record, paletteIndex))
  {
    PaletteMaterial material = g_palettes[paletteIndex].materials[UnpackVoxelRecord(g_records[record]).color];
    albedo = material.albedo;
    emissiveScale = material.emissiveScale;
  }
  float3 color = LightPixel(g_view, float2(pixel) + 0.5, visibility.x, UnpackOctahedralNormal(visibility.y), g_depth.Load(location), albedo,
                            emissiveScale, g_shadowMap, g_shadowSampler, g_shadowView, g_lighting);
  g_color[pixel] = float4(color, 1.0);
}
