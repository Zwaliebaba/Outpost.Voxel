#pragma once

#include "Float3.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// One rectangle the canvas draws (Design/SampleRenderer.md §13, Design/ADR/ADR-010): a glyph from the atlas in its
// color, or a fill of its color, in pixels of the target with y down. This struct is the truth; Shader/CanvasQuad.hlsli
// mirrors it as the element of a structured buffer, and the layout echo in NeuronClientTests proves the two agree
// (R16). A structured buffer packs to four bytes, so the struct has no padding.
struct CanvasQuad
{
  std::int32_t pixelX; // the top-left corner in the target
  std::int32_t pixelY;
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  std::uint32_t atlasX; // the glyph bitmap's top-left texel in the atlas
  std::uint32_t atlasY;
  std::uint32_t fill;       // 1: the whole rectangle is the color; 0: the color where the glyph covers it
  NeuronCore::Float3 color; // linear
  float alpha;              // straight, not premultiplied
};

static_assert(sizeof(CanvasQuad) == 44);
static_assert(offsetof(CanvasQuad, pixelX) == 0);
static_assert(offsetof(CanvasQuad, pixelY) == 4);
static_assert(offsetof(CanvasQuad, widthPixels) == 8);
static_assert(offsetof(CanvasQuad, heightPixels) == 12);
static_assert(offsetof(CanvasQuad, atlasX) == 16);
static_assert(offsetof(CanvasQuad, atlasY) == 20);
static_assert(offsetof(CanvasQuad, fill) == 24);
static_assert(offsetof(CanvasQuad, color) == 28);
static_assert(offsetof(CanvasQuad, alpha) == 40);

} // namespace NeuronClient
