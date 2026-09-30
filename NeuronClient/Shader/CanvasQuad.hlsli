#pragma once

// The HLSL mirror of NeuronClient/CanvasQuad.h (R16, Design/Archive/SampleRenderer.md §13, Design/ADR/ADR-034): one quad of
// the canvas, as the element of a structured buffer, and what it draws. The C++ struct is the truth, and the layout echo
// in NeuronClientTests proves that the two agree.

// CanvasQuadKind's values.
static const uint CANVAS_GLYPH = 0u;   // its color where the glyph covers the rectangle
static const uint CANVAS_FILL = 1u;    // its color over the whole rectangle
static const uint CANVAS_SEGMENT = 2u; // its color along the segment from its start to its end, in a quad sloped along it

struct CanvasQuad
{
  int pixelX; // a glyph's or a fill's rectangle: its top-left corner in the target
  int pixelY;
  uint widthPixels;
  uint heightPixels;
  uint atlasX; // a glyph's bitmap's top-left texel in the atlas
  uint atlasY;
  uint kind;    // one of the values above
  float3 color; // linear
  float alpha;  // straight, not premultiplied
  float startX; // a segment's ends, and half its width
  float startY;
  float endX;
  float endY;
  float halfWidthPixels;
};
