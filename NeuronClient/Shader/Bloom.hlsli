#pragma once

// Bloom's filters (Design/Archive/SpaceScene.md §12.2): Jimenez's 13-tap halving, with Karis's average in the first, and the
// 3 × 3 tent that carries each level up the chain and into the tone map. Every tap is bilinear, on a texel corner or a
// quarter of the way between texel centres, and is read here with explicit loads and its exact weights, so that no
// sampler's rounding stands between the GPU and the twins in NeuronCore/Bloom.h (R15).

// §12.2: the share of every pixel's light that bloom spreads.
static const float BLOOM_SHARE = 0.04;

// The luminance of a linear Rec. 709 color: the twin of NeuronCore/ColorSpace.h's (R15).
float Luminance(float3 _color)
{
  return dot(_color, float3(0.2126, 0.7152, 0.0722));
}

// A texel of an image _size texels across, its coordinates clamped to the image, as a sampler's clamp mode reads one.
float3 BloomTexel(Texture2D<float4> _image, uint2 _size, int2 _texel)
{
  return _image.Load(int3(clamp(_texel, int2(0, 0), int2(_size) - 1), 0)).rgb;
}

// A bilinear tap among texel _topLeft, the one to its right and the two below them, where _right is the weight of the
// right column and _bottom of the bottom row.
float3 Tap(Texture2D<float4> _image, uint2 _size, int2 _topLeft, float _right, float _bottom)
{
  float3 upper = BloomTexel(_image, _size, _topLeft) * (1.0 - _right) + BloomTexel(_image, _size, _topLeft + int2(1, 0)) * _right;
  float3 lower =
    BloomTexel(_image, _size, _topLeft + int2(0, 1)) * (1.0 - _right) + BloomTexel(_image, _size, _topLeft + int2(1, 1)) * _right;
  return upper * (1.0 - _bottom) + lower * _bottom;
}

// The tap on the corner at the top left of texel _texel: the average of the four texels about it.
float3 Corner(Texture2D<float4> _image, uint2 _size, int2 _texel)
{
  return Tap(_image, _size, _texel - int2(1, 1), 0.5, 0.5);
}

// The average of a box of four taps; with _karis, each weighted by 1 / (1 + its luminance), Karis's average.
float3 BoxAverage(float3 _a, float3 _b, float3 _c, float3 _d, bool _karis)
{
  if (!_karis)
  {
    return (_a + _b + _c + _d) * 0.25;
  }
  float a = 1.0 / (1.0 + Luminance(_a));
  float b = 1.0 / (1.0 + Luminance(_b));
  float c = 1.0 / (1.0 + Luminance(_c));
  float d = 1.0 / (1.0 + Luminance(_d));
  return (_a * a + _b * b + _c * c + _d * d) * (1.0 / (a + b + c + d));
}

// Texel _texel of the level below _above: 13 taps on the corners about the texel's centre, as five overlapping boxes.
float3 BloomDownsample(Texture2D<float4> _above, uint2 _aboveSize, uint2 _texel, bool _karis)
{
  // The texel's centre, on the corner at the top left of _above's texel (x, y).
  int2 center = int2(_texel * 2u + 1u);
  float3 a = Corner(_above, _aboveSize, center + int2(-2, -2));
  float3 b = Corner(_above, _aboveSize, center + int2(0, -2));
  float3 c = Corner(_above, _aboveSize, center + int2(2, -2));
  float3 d = Corner(_above, _aboveSize, center + int2(-2, 0));
  float3 e = Corner(_above, _aboveSize, center);
  float3 f = Corner(_above, _aboveSize, center + int2(2, 0));
  float3 g = Corner(_above, _aboveSize, center + int2(-2, 2));
  float3 h = Corner(_above, _aboveSize, center + int2(0, 2));
  float3 i = Corner(_above, _aboveSize, center + int2(2, 2));
  float3 j = Corner(_above, _aboveSize, center + int2(-1, -1));
  float3 k = Corner(_above, _aboveSize, center + int2(1, -1));
  float3 l = Corner(_above, _aboveSize, center + int2(-1, 1));
  float3 m = Corner(_above, _aboveSize, center + int2(1, 1));
  return BoxAverage(j, k, l, m, _karis) * 0.5 + (BoxAverage(a, b, d, e, _karis) + BoxAverage(b, c, e, f, _karis) +
                                                 BoxAverage(d, e, g, h, _karis) + BoxAverage(e, f, h, i, _karis)) *
                                                  0.125;
}

// A row of the tent: three taps a texel of _below apart.
float3 TentRow(Texture2D<float4> _below, uint2 _belowSize, int _left, int _top, float _right, float _bottom)
{
  return Tap(_below, _belowSize, int2(_left - 1, _top), _right, _bottom) +
         Tap(_below, _belowSize, int2(_left, _top), _right, _bottom) * 2.0 +
         Tap(_below, _belowSize, int2(_left + 1, _top), _right, _bottom);
}

// The 3 × 3 tent over _below at the centre of texel _texel of the level above it, which lies a quarter of the way from
// one of _below's texel centres to the next.
float3 BloomTent(Texture2D<float4> _below, uint2 _belowSize, uint2 _texel)
{
  int left = int(_texel.x >> 1u) - 1 + int(_texel.x & 1u);
  int top = int(_texel.y >> 1u) - 1 + int(_texel.y & 1u);
  float right = (_texel.x & 1u) != 0u ? 0.25 : 0.75;
  float bottom = (_texel.y & 1u) != 0u ? 0.25 : 0.75;
  return (TentRow(_below, _belowSize, left, top - 1, right, bottom) + TentRow(_below, _belowSize, left, top, right, bottom) * 2.0 +
          TentRow(_below, _belowSize, left, top + 1, right, bottom)) *
         (1.0 / 16.0);
}

// A texel of the up chain: _texel blended with the tent over the level below by _belowShare.
float3 BloomUpsample(float3 _texel, float3 _tent, float _belowShare)
{
  return _texel + (_tent - _texel) * _belowShare;
}

// What the tone map shows before its exposure and fit: the HDR color with BLOOM_SHARE of its light given to bloom.
float3 MixBloom(float3 _color, float3 _bloom)
{
  return _color + (_bloom - _color) * BLOOM_SHARE;
}
