#pragma once

// One triangle over the viewport, at the far plane of reversed Z, for the passes that write every pixel of their target:
// the tone map and the debug views (Design/Archive/SampleRenderer.md §8, §11); for the coverage count, which writes none
// but tests every one against the view's depth (§14); and for the sky, which writes the pixels still at the far plane
// (Design/Archive/SpaceScene.md §9, §11.5).

struct FullScreenVaryings
{
  float4 position : SV_Position;
};

// Vertices 0, 1 and 2 at (-1, -1), (3, -1) and (-1, 3) cover the viewport with one triangle.
FullScreenVaryings FullScreenVertex(uint _vertex : SV_VertexID)
{
  FullScreenVaryings varyings;
  varyings.position = float4(_vertex == 1u ? 3.0 : -1.0, _vertex == 2u ? 3.0 : -1.0, 0.0, 1.0);
  return varyings;
}
