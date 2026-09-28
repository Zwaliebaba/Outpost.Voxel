#pragma once

// The canvas's arithmetic (Design/Archive/SampleRenderer.md §13, Design/ADR/ADR-010): where a rectangle's corners land, which
// atlas texel a glyph shows at a pixel, and the color a pixel of it blends over the target with. Rectangles are in pixels
// of the target, y down. The C++ twins are in NeuronClient/CanvasShading.h (R15).

// Corner _corner of the rectangle whose top-left corner is _pixel, in normalized device coordinates of a target
// _targetPixels wide and tall. Bit 0 of the corner picks the right edge and bit 1 the bottom one.
float2 CanvasCornerNdc(int2 _pixel, uint2 _sizePixels, uint _corner, uint2 _targetPixels)
{
  float2 corner = float2(_pixel) + float2(_sizePixels) * float2(float(_corner & 1u), float((_corner >> 1u) & 1u));
  return float2(2.0 * corner.x / float(_targetPixels.x) - 1.0, 1.0 - 2.0 * corner.y / float(_targetPixels.y));
}

// The atlas texel a glyph shows at _pixel, when its bitmap starts at texel _atlas and is drawn with its top-left corner at
// _origin.
int2 CanvasAtlasTexel(int2 _pixel, int2 _origin, uint2 _atlas)
{
  return int2(_atlas) + (_pixel - _origin);
}

// A color with straight alpha where a glyph covers _coverage of the pixel, as premultiplied color: the color scaled by the
// alpha times the coverage, and that product as the alpha.
float4 CanvasPremultiply(float3 _color, float _alpha, float _coverage)
{
  float alpha = _alpha * _coverage;
  return float4(_color * alpha, alpha);
}
