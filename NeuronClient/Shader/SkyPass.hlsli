#pragma once

// The sky pass (Design/SpaceScene.md §11.5): one triangle at the far plane that writes the galaxy and the sun into every
// pixel no voxel covers, then one quad a star, added. Both lie at the far plane and test the view's depth, bound
// read-only, for equality with it (§9), so that neither reaches a pixel a voxel covers; neither writes depth, so the
// early depth test keeps the galaxy's noise off those pixels.

#include "FullScreen.hlsli"
#include "Sky.hlsli"
#include "StarRecord.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<SkyConstants> g_sky : register(b1);
StructuredBuffer<StarRecord> g_stars : register(t0);

struct SkyTarget
{
  float4 color : SV_Target0;
};

SkyTarget ShadeSky(FullScreenVaryings _varyings)
{
  SkyTarget target;
  target.color = float4(SkyPixel(g_view, _varyings.position.xy, g_sky), 1.0);
  return target;
}

struct StarVaryings
{
  float4 position : SV_Position;
  nointerpolation float2 center : STAR_CENTER; // the star's, in pixels
  nointerpolation float3 color : STAR_COLOR;
  nointerpolation float brightness : STAR_BRIGHTNESS; // its flux times the gain
};

// Corner _vertex of star _instance's quad, a strip of two triangles: (-, -), (+, -), (-, +) and (+, +) about its centre,
// in pixels. A star that is not drawn has its four corners on one point outside the view.
StarVaryings StarVertex(uint _vertex : SV_VertexID, uint _instance : SV_InstanceID)
{
  StarRecord star = g_stars[_instance];
  StarVaryings varyings;
  varyings.color = star.color;
  varyings.brightness = star.flux * g_sky.starGain;
  float radius = StarQuadRadius(varyings.brightness);
  bool drawn = StarPosition(g_view, star.direction, varyings.center);
  float2 corner = float2((_vertex & 1u) != 0u ? 1.0 : -1.0, (_vertex & 2u) != 0u ? 1.0 : -1.0);
  float2 pixel = varyings.center + corner * radius;
  varyings.position = drawn && radius > 0.0 ? float4(2.0 * pixel.x / float(g_view.widthPixels) - 1.0,
                                                     1.0 - 2.0 * pixel.y / float(g_view.heightPixels), 0.0, 1.0)
                                            : float4(-2.0, -2.0, 0.0, 1.0);
  return varyings;
}

struct StarTarget
{
  float4 color : SV_Target0;
};

// What the star adds to the pixel: its color and brightness times the pixel's share, blended additively.
StarTarget ShadeStar(StarVaryings _varyings)
{
  StarTarget target;
  target.color = float4(_varyings.color * (_varyings.brightness * StarShare(_varyings.position.xy - _varyings.center)), 0.0);
  return target;
}
