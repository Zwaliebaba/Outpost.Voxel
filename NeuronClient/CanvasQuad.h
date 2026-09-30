#pragma once

#include "Float3.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// What a canvas quad draws (Design/ADR/ADR-034). Shader/CanvasQuad.hlsli spells the same values.
enum class CanvasQuadKind : std::uint32_t
{
  Glyph = 0,  // its color where the glyph covers the rectangle
  Fill = 1,   // its color over the whole rectangle
  Segment = 2 // its color along the segment from its start to its end, in a quad sloped along it
};

// One quad the canvas draws (Design/Archive/SampleRenderer.md §13, Design/ADR/ADR-010, ADR-034): a glyph from the atlas in
// its color, or a fill of its color, over a rectangle; or a segment in its color, antialiased, in a quad sloped along it.
// Everything is in pixels of the target with y down. This struct is the truth; Shader/CanvasQuad.hlsli mirrors it as the
// element of a structured buffer, and the layout echo in NeuronClientTests proves the two agree (R16). A structured
// buffer packs to four bytes, so the struct has no padding.
struct CanvasQuad
{
  std::int32_t pixelX; // a glyph's or a fill's rectangle: its top-left corner in the target
  std::int32_t pixelY;
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  std::uint32_t atlasX; // a glyph's bitmap's top-left texel in the atlas
  std::uint32_t atlasY;
  std::uint32_t kind;       // a CanvasQuadKind
  NeuronCore::Float3 color; // linear
  float alpha;              // straight, not premultiplied
  float startX;             // a segment's ends, and half its width
  float startY;
  float endX;
  float endY;
  float halfWidthPixels;
};

static_assert(sizeof(CanvasQuad) == 64);
static_assert(offsetof(CanvasQuad, pixelX) == 0);
static_assert(offsetof(CanvasQuad, pixelY) == 4);
static_assert(offsetof(CanvasQuad, widthPixels) == 8);
static_assert(offsetof(CanvasQuad, heightPixels) == 12);
static_assert(offsetof(CanvasQuad, atlasX) == 16);
static_assert(offsetof(CanvasQuad, atlasY) == 20);
static_assert(offsetof(CanvasQuad, kind) == 24);
static_assert(offsetof(CanvasQuad, color) == 28);
static_assert(offsetof(CanvasQuad, alpha) == 40);
static_assert(offsetof(CanvasQuad, startX) == 44);
static_assert(offsetof(CanvasQuad, startY) == 48);
static_assert(offsetof(CanvasQuad, endX) == 52);
static_assert(offsetof(CanvasQuad, endY) == 56);
static_assert(offsetof(CanvasQuad, halfWidthPixels) == 60);

} // namespace NeuronClient
