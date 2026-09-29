#include "pch.h"

#include "Bloom.h"

#include "ColorSpace.h"
#include "Half.h"

#include <algorithm>
#include <cstddef>

namespace NeuronCore
{
namespace
{

// A texel, its coordinates clamped to the image, as a sampler's clamp mode reads one.
[[nodiscard]] Float3 Texel(const BloomImage& _image, std::int32_t _x, std::int32_t _y) noexcept
{
  const auto x = static_cast<std::uint32_t>(std::clamp(_x, 0, static_cast<std::int32_t>(_image.widthPixels) - 1));
  const auto y = static_cast<std::uint32_t>(std::clamp(_y, 0, static_cast<std::int32_t>(_image.heightPixels) - 1));
  return _image.texels[static_cast<std::size_t>(y) * _image.widthPixels + x];
}

// A bilinear tap among texel (_left, _top), the one to its right and the two below them, where _right is the weight of
// the right column and _bottom of the bottom row.
[[nodiscard]] Float3 Tap(const BloomImage& _image, std::int32_t _left, std::int32_t _top, float _right, float _bottom) noexcept
{
  const Float3 upper = Texel(_image, _left, _top) * (1.0f - _right) + Texel(_image, _left + 1, _top) * _right;
  const Float3 lower = Texel(_image, _left, _top + 1) * (1.0f - _right) + Texel(_image, _left + 1, _top + 1) * _right;
  return upper * (1.0f - _bottom) + lower * _bottom;
}

// The tap on the corner at the top left of texel (_x, _y): the average of the four texels about it.
[[nodiscard]] Float3 Corner(const BloomImage& _image, std::int32_t _x, std::int32_t _y) noexcept
{
  return Tap(_image, _x - 1, _y - 1, 0.5f, 0.5f);
}

// The average of a box of four taps; with _karis, each weighted by 1 / (1 + its luminance), Karis's average, so that a
// tap far brighter than the others counts for about as much as one of them.
[[nodiscard]] Float3 BoxAverage(Float3 _a, Float3 _b, Float3 _c, Float3 _d, bool _karis) noexcept
{
  if (!_karis)
  {
    return (_a + _b + _c + _d) * 0.25f;
  }
  const float a = 1.0f / (1.0f + Luminance(_a));
  const float b = 1.0f / (1.0f + Luminance(_b));
  const float c = 1.0f / (1.0f + Luminance(_c));
  const float d = 1.0f / (1.0f + Luminance(_d));
  return (_a * a + _b * b + _c * c + _d * d) * (1.0f / (a + b + c + d));
}

// _color as a half-precision target stores it.
[[nodiscard]] Float3 StoredAsHalf(Float3 _color) noexcept
{
  return {RoundToHalf(_color.x), RoundToHalf(_color.y), RoundToHalf(_color.z)};
}

} // namespace

std::uint32_t BloomLevelCount(std::uint32_t _widthPixels, std::uint32_t _heightPixels) noexcept
{
  std::uint32_t count = 1;
  std::uint32_t smaller = BloomLevelPixels(std::min(_widthPixels, _heightPixels));
  while (count < BLOOM_MAX_LEVELS && BloomLevelPixels(smaller) >= BLOOM_LEAST_LEVEL_PIXELS)
  {
    smaller = BloomLevelPixels(smaller);
    ++count;
  }
  return count;
}

Float3 BloomDownsample(const BloomImage& _above, std::uint32_t _x, std::uint32_t _y, bool _karis) noexcept
{
  // The texel's centre, on the corner at the top left of _above's texel (x, y).
  const auto x = static_cast<std::int32_t>(2u * _x + 1u);
  const auto y = static_cast<std::int32_t>(2u * _y + 1u);
  const Float3 a = Corner(_above, x - 2, y - 2);
  const Float3 b = Corner(_above, x, y - 2);
  const Float3 c = Corner(_above, x + 2, y - 2);
  const Float3 d = Corner(_above, x - 2, y);
  const Float3 e = Corner(_above, x, y);
  const Float3 f = Corner(_above, x + 2, y);
  const Float3 g = Corner(_above, x - 2, y + 2);
  const Float3 h = Corner(_above, x, y + 2);
  const Float3 i = Corner(_above, x + 2, y + 2);
  const Float3 j = Corner(_above, x - 1, y - 1);
  const Float3 k = Corner(_above, x + 1, y - 1);
  const Float3 l = Corner(_above, x - 1, y + 1);
  const Float3 m = Corner(_above, x + 1, y + 1);
  return BoxAverage(j, k, l, m, _karis) * 0.5f + (BoxAverage(a, b, d, e, _karis) + BoxAverage(b, c, e, f, _karis) +
                                                  BoxAverage(d, e, g, h, _karis) + BoxAverage(e, f, h, i, _karis)) *
                                                   0.125f;
}

Float3 BloomTent(const BloomImage& _below, std::uint32_t _x, std::uint32_t _y) noexcept
{
  // The centre of texel (x, y) lies a quarter of the way from one of _below's texel centres to the next: between the
  // column _left and the one to its right, whose weight is _right, and likewise for the rows.
  const std::int32_t left = static_cast<std::int32_t>(_x >> 1u) - 1 + static_cast<std::int32_t>(_x & 1u);
  const std::int32_t top = static_cast<std::int32_t>(_y >> 1u) - 1 + static_cast<std::int32_t>(_y & 1u);
  const float right = (_x & 1u) != 0u ? 0.25f : 0.75f;
  const float bottom = (_y & 1u) != 0u ? 0.25f : 0.75f;
  const auto row = [&_below, left, right, bottom](std::int32_t _top)
  {
    return Tap(_below, left - 1, _top, right, bottom) + Tap(_below, left, _top, right, bottom) * 2.0f +
           Tap(_below, left + 1, _top, right, bottom);
  };
  return (row(top - 1) + row(top) * 2.0f + row(top + 1)) * (1.0f / 16.0f);
}

float BloomBelowShare(std::uint32_t _levelsBelow) noexcept
{
  return static_cast<float>(_levelsBelow) / static_cast<float>(_levelsBelow + 1u);
}

Float3 BloomUpsample(Float3 _texel, Float3 _tent, float _belowShare) noexcept
{
  return _texel + (_tent - _texel) * _belowShare;
}

std::vector<BloomImage> BloomDownChain(const BloomImage& _hdr)
{
  const std::uint32_t count = BloomLevelCount(_hdr.widthPixels, _hdr.heightPixels);
  std::vector<BloomImage> levels;
  levels.reserve(count);
  for (std::uint32_t level = 0; level < count; ++level)
  {
    const BloomImage& above = level == 0 ? _hdr : levels.back();
    BloomImage image{BloomLevelPixels(above.widthPixels), BloomLevelPixels(above.heightPixels), {}};
    image.texels.reserve(static_cast<std::size_t>(image.widthPixels) * image.heightPixels);
    for (std::uint32_t y = 0; y < image.heightPixels; ++y)
    {
      for (std::uint32_t x = 0; x < image.widthPixels; ++x)
      {
        image.texels.push_back(StoredAsHalf(BloomDownsample(above, x, y, level == 0)));
      }
    }
    levels.push_back(std::move(image));
  }
  return levels;
}

void BloomUpChain(std::vector<BloomImage>& _levels)
{
  for (std::size_t level = _levels.size(); level-- > 1;)
  {
    const BloomImage& below = _levels[level];
    BloomImage& image = _levels[level - 1];
    const float share = BloomBelowShare(static_cast<std::uint32_t>(_levels.size() - level));
    for (std::uint32_t y = 0; y < image.heightPixels; ++y)
    {
      for (std::uint32_t x = 0; x < image.widthPixels; ++x)
      {
        Float3& texel = image.texels[static_cast<std::size_t>(y) * image.widthPixels + x];
        texel = StoredAsHalf(BloomUpsample(texel, BloomTent(below, x, y), share));
      }
    }
  }
}

Float3 MixBloom(Float3 _color, Float3 _bloom) noexcept
{
  return _color + (_bloom - _color) * BLOOM_SHARE;
}

} // namespace NeuronCore
