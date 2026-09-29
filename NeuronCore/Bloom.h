#pragma once

#include "Float3.h"

#include <cstdint>
#include <vector>

namespace NeuronCore
{

// Bloom (Design/SpaceScene.md §12.2): a fixed share of every pixel's light, spread over a wide, smooth kernel built as
// Jimenez built it for Call of Duty: Advanced Warfare. The view's HDR color is halved level by level, each texel of a
// level a 13-tap filter over the level above it. Then each level, from the second smallest up, is blended with a 3 × 3
// tent over the level below it, so that it ends as the average of its own blur and every smaller one's; and the tone
// map mixes a tent over the first level into the image. Every tap is bilinear, on a texel corner or a quarter of the way
// between texel centres, where its weights are exact: halves, or a quarter and three quarters. These are the twins of
// Shader/Bloom.hlsli (R15).

// §12.2: the share of every pixel's light that bloom spreads, tuned by eye.
inline constexpr float BLOOM_SHARE = 0.04f;

// The halvings go on while the next level is at least this many texels in its smaller dimension: six at 1080p, and one
// more each time the view doubles, so that the kernel keeps its size on the screen. At most BLOOM_MAX_LEVELS, enough for
// a view of 8K.
inline constexpr std::uint32_t BLOOM_LEAST_LEVEL_PIXELS = 16;
inline constexpr std::uint32_t BLOOM_MAX_LEVELS = 8;

// An image as bloom reads and writes it: RGB per texel, row by row from the top. The GPU's targets hold RGBA at half
// precision, and bloom leaves alpha alone.
struct BloomImage
{
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  std::vector<Float3> texels;
};

// The levels bloom makes for a view of this size: at least one.
[[nodiscard]] std::uint32_t BloomLevelCount(std::uint32_t _widthPixels, std::uint32_t _heightPixels) noexcept;

// A level's width or height from the one above it: half, rounded up, so that the level covers every texel above it.
[[nodiscard]] constexpr std::uint32_t BloomLevelPixels(std::uint32_t _abovePixels) noexcept
{
  return (_abovePixels + 1u) / 2u;
}

// Texel (x, y) of the level below _above. Its centre lies on the corner between four of _above's texels, and 13 taps
// lie on the corners about it, weighted as five overlapping boxes of four: the middle box a half and the four others an
// eighth each. With _karis, as in the first halving, the taps within each box are weighted by 1 / (1 + luminance),
// Karis's average, so that one brilliant texel cannot flare.
[[nodiscard]] Float3 BloomDownsample(const BloomImage& _above, std::uint32_t _x, std::uint32_t _y, bool _karis) noexcept;

// The 3 × 3 tent over _below at the centre of texel (x, y) of the level above it: nine taps a texel of _below apart,
// weighted 1, 2, 1 along each axis.
[[nodiscard]] Float3 BloomTent(const BloomImage& _below, std::uint32_t _x, std::uint32_t _y) noexcept;

// The share of the tent in a level of the up chain with _levelsBelow levels below it, whose average the tent holds.
[[nodiscard]] float BloomBelowShare(std::uint32_t _levelsBelow) noexcept;

// A texel of the up chain: _texel blended with _tent, the tent over the level below, by _belowShare.
[[nodiscard]] Float3 BloomUpsample(Float3 _texel, Float3 _tent, float _belowShare) noexcept;

// The levels the halvings make from _hdr, the first the largest, BloomLevelCount of them, each texel rounded to half
// precision as the GPU stores it.
[[nodiscard]] std::vector<BloomImage> BloomDownChain(const BloomImage& _hdr);

// _levels, from the second smallest up, each blended with the tent over the level below it, rounded as the GPU stores
// them. The tent over the first is the bloom the tone map mixes in.
void BloomUpChain(std::vector<BloomImage>& _levels);

// What the tone map shows, before its exposure and fit: the HDR color with BLOOM_SHARE of its light given to bloom.
[[nodiscard]] Float3 MixBloom(Float3 _color, Float3 _bloom) noexcept;

} // namespace NeuronCore
