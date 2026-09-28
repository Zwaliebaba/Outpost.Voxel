#pragma once

// The HLSL mirror of NeuronClient/CanvasQuad.h (R16, Design/SampleRenderer.md §13): one rectangle of the canvas, as the
// element of a structured buffer. The C++ struct is the truth, and the layout echo in NeuronClientTests proves that the
// two agree.
struct CanvasQuad
{
  int pixelX; // the top-left corner in the target
  int pixelY;
  uint widthPixels;
  uint heightPixels;
  uint atlasX; // the glyph bitmap's top-left texel in the atlas
  uint atlasY;
  uint fill;    // 1: the whole rectangle is the color; 0: the color where the glyph covers it
  float3 color; // linear
  float alpha;  // straight, not premultiplied
};
