#pragma once

// The canvas's arithmetic (Design/Archive/SampleRenderer.md §13, Design/ADR/ADR-010, ADR-034): where a rectangle's or a
// segment's corners land, which atlas texel a glyph shows at a pixel, how much of a pixel a segment covers, and the color
// a pixel of it blends over the target with. Rectangles are in pixels
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

// Corner _corner of the quad sloped along the segment from _start to _end (Design/ADR/ADR-034), in normalized device
// coordinates of a target _targetPixels wide and tall: the rectangle around the segment, _halfWidthPixels and a pixel
// more to either side and past either end, the pixel for the antialiased edge. Bit 0 of the corner picks the end over
// the start, and bit 1 the side the way from start to end turns to by a quarter turn from +x toward +y. A segment of no
// length lies along +x.
float2 CanvasSegmentCornerNdc(float2 _start, float2 _end, float _halfWidthPixels, uint _corner, uint2 _targetPixels)
{
  float2 along = _end - _start;
  float lengthPixels = sqrt(along.x * along.x + along.y * along.y);
  float2 direction = lengthPixels > 0.0 ? float2(along.x / lengthPixels, along.y / lengthPixels) : float2(1.0, 0.0);
  float reach = _halfWidthPixels + 1.0;
  float2 end = (_corner & 1u) != 0u ? float2(_end.x + direction.x * reach, _end.y + direction.y * reach)
                                    : float2(_start.x - direction.x * reach, _start.y - direction.y * reach);
  float side = ((_corner >> 1u) & 1u) != 0u ? reach : -reach;
  float2 corner = float2(end.x - direction.y * side, end.y + direction.x * side);
  return float2(2.0 * corner.x / float(_targetPixels.x) - 1.0, 1.0 - 2.0 * corner.y / float(_targetPixels.y));
}

// How much of the pixel whose centre is _pixel the segment from _start to _end covers, _halfWidthPixels to either side with
// round ends: all of it within half a pixel less than that, none beyond half a pixel more, and in proportion between.
float CanvasSegmentCoverage(float2 _pixel, float2 _start, float2 _end, float _halfWidthPixels)
{
  float2 along = _end - _start;
  float2 from = _pixel - _start;
  float lengthSquared = along.x * along.x + along.y * along.y;
  float nearest = lengthSquared > 0.0 ? clamp((from.x * along.x + from.y * along.y) / lengthSquared, 0.0, 1.0) : 0.0;
  float2 apart = float2(from.x - along.x * nearest, from.y - along.y * nearest);
  float apartPixels = sqrt(apart.x * apart.x + apart.y * apart.y);
  return clamp(_halfWidthPixels + 0.5 - apartPixels, 0.0, 1.0);
}

// A color with straight alpha where a glyph covers _coverage of the pixel, as premultiplied color: the color scaled by the
// alpha times the coverage, and that product as the alpha.
float4 CanvasPremultiply(float3 _color, float _alpha, float _coverage)
{
  float alpha = _alpha * _coverage;
  return float4(_color * alpha, alpha);
}
