#pragma once

// The canvas pass (Design/Archive/SampleRenderer.md §13, Design/ADR/ADR-010): each instance is one CanvasQuad, a strip of four
// vertices over the render target the caller binds. A glyph's pixels take the quad's color times the glyph's coverage in
// the atlas, a fill's the color alone, and the blend state lays them over the target with premultiplied alpha.

#include "CanvasQuad.hlsli"
#include "CanvasShading.hlsli"

StructuredBuffer<CanvasQuad> g_quads : register(t0);
Texture2D<float> g_atlas : register(t1);

// Two 32-bit root constants: the target's size.
cbuffer CanvasTargetSize : register(b0)
{
  uint g_targetWidthPixels;
  uint g_targetHeightPixels;
};

struct CanvasVaryings
{
  float4 position : SV_Position;
  nointerpolation uint quad : QUAD;
};

struct CanvasTarget
{
  float4 color : SV_Target0;
};

CanvasVaryings CanvasVertex(uint _vertex : SV_VertexID, uint _instance : SV_InstanceID)
{
  CanvasQuad quad = g_quads[_instance];
  CanvasVaryings varyings;
  varyings.position = float4(CanvasCornerNdc(int2(quad.pixelX, quad.pixelY), uint2(quad.widthPixels, quad.heightPixels), _vertex,
                                             uint2(g_targetWidthPixels, g_targetHeightPixels)),
                             0.0, 1.0);
  varyings.quad = _instance;
  return varyings;
}

// SV_Position holds the pixel's centre, so its integer part is the pixel.
CanvasTarget CanvasPixel(CanvasVaryings _varyings)
{
  CanvasQuad quad = g_quads[_varyings.quad];
  float coverage = 1.0;
  if (quad.fill == 0u)
  {
    int2 texel = CanvasAtlasTexel(int2(_varyings.position.xy), int2(quad.pixelX, quad.pixelY), uint2(quad.atlasX, quad.atlasY));
    coverage = g_atlas.Load(int3(texel, 0));
  }
  CanvasTarget target;
  target.color = CanvasPremultiply(quad.color, quad.alpha, coverage);
  return target;
}
