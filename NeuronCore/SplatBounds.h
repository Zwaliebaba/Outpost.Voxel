#pragma once

#include "Box.h"
#include "Float3.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"

namespace NeuronCore
{

// The screen-space rectangle a voxel is splatted as, and the depth it is placed at (Design/SampleRenderer.md §9.2).
struct SplatBounds
{
  Float2 minNdc; // normalized device coordinates: x right, y up
  Float2 maxNdc;
  float depth;  // the voxel's nearest point: reversed-Z in a perspective view, standard Z in an orthographic one
  bool visible; // false: the vertex shader emits a degenerate rectangle
};

// The paper switches to the precise bounds above this size (§4 of the paper).
inline constexpr float QUADRIC_LIMIT_PIXELS = 20.0f;

// Every rectangle grows by this much, so that fixed-point snapping and the top-left rule can never exclude a pixel
// centre the box covers (§9.2, step 3).
inline constexpr float BOUNDS_MARGIN_PIXELS = 1.0f / 64.0f;

// Steps 2 to 4 of §9.2 for a perspective view: culls, bounds and places one box. The twin of the view splat vertex
// shader's bounds (R15).
[[nodiscard]] SplatBounds PerspectiveSplatBounds(const Box& _box, const PerspectiveView& _view) noexcept;

// The orthographic case of §9.2: exact extents, with the rectangle at the box's nearest point along the view. The twin
// of the shadow splat vertex shader's bounds (R15).
[[nodiscard]] SplatBounds OrthographicSplatBounds(const Box& _box, const OrthographicView& _view) noexcept;

// Listing 4 of the paper (after Sigg et al. 2006): the bounds of the projection of a sphere lying wholly beyond the
// near plane, as a non-square rectangle. Returns false when the sphere reaches the camera plane.
[[nodiscard]] bool QuadricBounds(Float3 _center, float _radius, const PerspectiveView& _view, Float2& _minNdc, Float2& _maxNdc) noexcept;

// The precise path: the bounds of the box's corners, or of its edges clipped against the near plane when it crosses
// it, with the least view depth among the points bounded. Returns false when nothing of the box lies beyond the near
// plane.
[[nodiscard]] bool PreciseBounds(const Box& _box, const PerspectiveView& _view, Float2& _minNdc, Float2& _maxNdc,
                                 float& _nearestViewDepth) noexcept;

} // namespace NeuronCore
