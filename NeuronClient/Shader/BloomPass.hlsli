#pragma once

// Bloom's chain (Design/Archive/SpaceScene.md §12.2): a thread per texel of the level written, in 8 × 8 groups. On the way down,
// a level is the halving of the one above it, the HDR color for the first; on the way up, a level is blended in place
// with the tent over the one below it.

#include "Bloom.hlsli"
#include "BloomConstants.hlsli"

ConstantBuffer<BloomConstants> g_bloom : register(b0);
Texture2D<float4> g_source : register(t0); // down, the level above; up, the level below
RWTexture2D<float4> g_level : register(u0);

// NeuronClient/BloomPass.h relies on the same group size.
static const uint BLOOM_GROUP_PIXELS = 8;

// The input's semantic is on a struct member: clang-format breaks one on a parameter of an entry point that carries an
// attribute (Design/ADR/ADR-005).
struct BloomThread
{
  uint3 texel : SV_DispatchThreadID;
};

[numthreads(BLOOM_GROUP_PIXELS, BLOOM_GROUP_PIXELS, 1)] void BloomDown(BloomThread _thread)
{
  uint2 texel = _thread.texel.xy;
  if (texel.x >= g_bloom.widthPixels || texel.y >= g_bloom.heightPixels)
  {
    return;
  }
  uint2 aboveSize;
  g_source.GetDimensions(aboveSize.x, aboveSize.y);
  g_level[texel] = float4(BloomDownsample(g_source, aboveSize, texel, g_bloom.karis != 0u), 1.0);
}

  [numthreads(BLOOM_GROUP_PIXELS, BLOOM_GROUP_PIXELS, 1)] void BloomUp(BloomThread _thread)
{
  uint2 texel = _thread.texel.xy;
  if (texel.x >= g_bloom.widthPixels || texel.y >= g_bloom.heightPixels)
  {
    return;
  }
  uint2 belowSize;
  g_source.GetDimensions(belowSize.x, belowSize.y);
  g_level[texel] = float4(BloomUpsample(g_level[texel].rgb, BloomTent(g_source, belowSize, texel), g_bloom.belowShare), 1.0);
}
