#pragma once

// The lighting pass (Design/SampleRenderer.md §8, §11): a thread per pixel in 8 × 8 groups, from the view splat's depth
// and visibility and the shadow map into the HDR color target.

#include "Lighting.hlsli"
#include "PaletteConstants.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<ShadowViewConstants> g_shadowView : register(b1);
ConstantBuffer<LightingConstants> g_lighting : register(b2);
ConstantBuffer<PaletteConstants> g_palette : register(b3);
StructuredBuffer<uint> g_records : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D<uint2> g_visibility : register(t2);
Texture2D<float> g_shadowMap : register(t3);
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
  if (visibility.x != NO_VOXEL)
  {
    PaletteMaterial material = g_palette.materials[UnpackVoxelRecord(g_records[visibility.x]).color];
    albedo = material.albedo;
    emissiveScale = material.emissiveScale;
  }
  float3 color = LightPixel(g_view, float2(pixel) + 0.5, visibility.x, UnpackOctahedralNormal(visibility.y), g_depth.Load(location), albedo,
                            emissiveScale, g_shadowMap, g_shadowSampler, g_shadowView, g_lighting);
  g_color[pixel] = float4(color, 1.0);
}
