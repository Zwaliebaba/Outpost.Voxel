#pragma once

// The tone map pass (Design/SampleRenderer.md §8, §11): one triangle over the render target the caller binds, which
// shows the lighting's HDR color through the exposure and the ACES fit. The application's target is an sRGB view,
// which encodes the result.

#include "FullScreen.hlsli"
#include "ToneMap.hlsli"

Texture2D<float4> g_hdrColor : register(t0);

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
  ToneMapTarget target;
  target.color = float4(ToneMap(g_hdrColor.Load(int3(int2(_varyings.position.xy), 0)).rgb, g_exposure), 1.0);
  return target;
}
