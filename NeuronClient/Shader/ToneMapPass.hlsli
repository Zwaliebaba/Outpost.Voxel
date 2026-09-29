#pragma once

// The tone map pass (Design/Archive/SampleRenderer.md §8, §11): one triangle over the render target the caller binds, which
// shows the lighting's HDR color, with bloom's share of it spread (Design/Archive/SpaceScene.md §12.2), through the exposure and
// the ACES fit. The application's target is an sRGB view, which encodes the result.

#include "Bloom.hlsli"
#include "FullScreen.hlsli"
#include "ToneMap.hlsli"

Texture2D<float4> g_hdrColor : register(t0);
Texture2D<float4> g_bloom : register(t1); // the first level of bloom's up chain

// One 32-bit root constant: the exposure, a multiplier (_film _expo).
cbuffer ToneMapExposure : register(b0)
{
  float g_exposure;
};

struct ToneMapTarget
{
  float4 color : SV_Target0;
};

ToneMapTarget ToneMapPixel(FullScreenVaryings _varyings)
{
  uint2 pixel = uint2(_varyings.position.xy);
  uint2 bloomSize;
  g_bloom.GetDimensions(bloomSize.x, bloomSize.y);
  float3 color = MixBloom(g_hdrColor.Load(int3(pixel, 0)).rgb, BloomTent(g_bloom, bloomSize, pixel));
  ToneMapTarget target;
  target.color = float4(ToneMap(color, g_exposure), 1.0);
  return target;
}
