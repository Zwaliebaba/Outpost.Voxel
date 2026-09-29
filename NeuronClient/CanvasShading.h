#pragma once

#include "Float3.h"

#include <cstdint>

namespace NeuronClient
{

// The canvas's arithmetic (Design/Archive/SampleRenderer.md §13, Design/ADR/ADR-010, ADR-034): where a rectangle's or a
// segment's corners land, which atlas texel a glyph shows at a pixel, how much of a pixel a segment covers, and the color
// a pixel of it blends over the target with. Rectangles and segments are in pixels of the target, y down. These are the
// twins of the functions of the same names in Shader/CanvasShading.hlsli (R15). They live here rather than in NeuronCore,
// where R15 puts every other twin, because only the client draws a canvas (ADR-010).

// Corner _corner of the rectangle whose top-left corner is (_pixelX, _pixelY), in normalized device coordinates of a
// target _targetWidthPixels by _targetHeightPixels. Bit 0 of the corner picks the right edge and bit 1 the bottom one.
[[nodiscard]] NeuronCore::Float2 CanvasCornerNdc(std::int32_t _pixelX, std::int32_t _pixelY, std::uint32_t _widthPixels,
                                                 std::uint32_t _heightPixels, std::uint32_t _corner, std::uint32_t _targetWidthPixels,
                                                 std::uint32_t _targetHeightPixels) noexcept;

// The atlas texel a glyph shows at pixel (_pixelX, _pixelY), when its bitmap starts at texel (_atlasX, _atlasY) and is
// drawn with its top-left corner at (_originX, _originY).
void CanvasAtlasTexel(std::int32_t _pixelX, std::int32_t _pixelY, std::int32_t _originX, std::int32_t _originY, std::uint32_t _atlasX,
                      std::uint32_t _atlasY, std::int32_t& _texelX, std::int32_t& _texelY) noexcept;

// Corner _corner of the quad sloped along the segment from _start to _end (Design/ADR/ADR-034), in normalized device
// coordinates of a target _targetWidthPixels by _targetHeightPixels: the rectangle around the segment, _halfWidthPixels
// and a pixel more to either side and past either end, the pixel for the antialiased edge. Bit 0 of the corner picks
// the end over the start, and bit 1 the side the way from start to end turns to by a quarter turn from +x toward +y. A
// segment of no length lies along +x.
[[nodiscard]] NeuronCore::Float2 CanvasSegmentCornerNdc(NeuronCore::Float2 _start, NeuronCore::Float2 _end, float _halfWidthPixels,
                                                        std::uint32_t _corner, std::uint32_t _targetWidthPixels,
                                                        std::uint32_t _targetHeightPixels) noexcept;

// How much of the pixel whose centre is _pixel the segment from _start to _end covers, _halfWidthPixels to either side
// with round ends: all of it within half a pixel less than that, none beyond half a pixel more, and in proportion
// between.
[[nodiscard]] float CanvasSegmentCoverage(NeuronCore::Float2 _pixel, NeuronCore::Float2 _start, NeuronCore::Float2 _end,
                                          float _halfWidthPixels) noexcept;

// A color with straight alpha where a glyph covers _coverage of the pixel, as the premultiplied color the pixel shader
// writes: the color scaled by the alpha times the coverage, and that product as the alpha.
[[nodiscard]] NeuronCore::Float4 CanvasPremultiply(NeuronCore::Float3 _color, float _alpha, float _coverage) noexcept;

// The coverage an atlas texel holds: the R8_UNORM byte as the pixel shader loads it.
[[nodiscard]] constexpr float AtlasCoverage(std::uint8_t _texel) noexcept
{
  return static_cast<float>(_texel) / 255.0f;
}

// The canvas's blend state: the premultiplied source over the destination, which keeps one minus the source's alpha of
// itself, in every component. The pipeline's fixed function, not a shader, so no HLSL has this name.
[[nodiscard]] NeuronCore::Float4 CanvasBlend(NeuronCore::Float4 _source, NeuronCore::Float4 _destination) noexcept;

} // namespace NeuronClient
