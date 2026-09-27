#pragma once

// The screen-space rectangle a voxel is splatted as, and the depth it is placed at (Design/SampleRenderer.md §9.2). The
// C++ twins are in NeuronCore/SplatBounds.h and .cpp (R15), function for function.
//
// A call whose out parameters matter is never the right-hand side of && or ||: HLSL 2018 evaluates both sides, and the
// port must mean the same under 2018 and 2021.

#include "PerspectiveView.hlsli"
#include "RayBox.hlsli"
#include "ViewConstants.hlsli"

struct SplatBounds
{
  float2 minNdc; // normalized device coordinates: x right, y up
  float2 maxNdc;
  float depth;  // the voxel's nearest point, reversed-Z
  bool visible; // false: the vertex shader emits a degenerate rectangle
};

// The paper switches to the precise bounds above this size (§4 of the paper).
static const float QUADRIC_LIMIT_PIXELS = 20.0;

// Every rectangle grows by this much, so that fixed-point snapping and the top-left rule can never exclude a pixel centre
// the box covers (§9.2, step 3).
static const float BOUNDS_MARGIN_PIXELS = 1.0 / 64.0;

static const float UNBOUNDED = asfloat(0x7F800000u); // +infinity

// A point in view coordinates: x right, y up, depth along forward.
struct ViewPoint
{
  float x;
  float y;
  float depth;
};

ViewPoint ToView(float3 _position, ViewConstants _view)
{
  float3 offset = _position - _view.position;
  ViewPoint viewPoint;
  viewPoint.x = dot(offset, _view.right);
  viewPoint.y = dot(offset, _view.up);
  viewPoint.depth = dot(offset, _view.forward);
  return viewPoint;
}

// Clamps a rectangle to the viewport and grows it by the margin. Returns false when nothing is left to draw.
bool ClampAndGrow(inout float2 _minNdc, inout float2 _maxNdc, uint _widthPixels, uint _heightPixels)
{
  _minNdc = max(_minNdc, float2(-1.0, -1.0));
  _maxNdc = min(_maxNdc, float2(1.0, 1.0));
  if (_minNdc.x > _maxNdc.x || _minNdc.y > _maxNdc.y)
  {
    return false;
  }
  float2 margin = float2(2.0 * BOUNDS_MARGIN_PIXELS / float(_widthPixels), 2.0 * BOUNDS_MARGIN_PIXELS / float(_heightPixels));
  _minNdc -= margin;
  _maxNdc += margin;
  return true;
}

// Listing 4 of the paper (after Sigg et al. 2006): the bounds of the projection of a sphere lying wholly beyond the near
// plane. Returns false when the sphere reaches the camera plane.
bool QuadricBounds(float3 _center, float _radius, ViewConstants _view, out float2 _minNdc, out float2 _maxNdc)
{
  _minNdc = float2(0.0, 0.0);
  _maxNdc = float2(0.0, 0.0);
  float3 offset = _center - _view.position;
  float x = dot(offset, _view.right);
  float y = dot(offset, _view.up);
  float depth = dot(offset, _view.forward);
  if (!(depth > _radius))
  {
    return false;
  }

  // Listing 4's quadric, written out for an orthonormal view basis and relative to the camera. With a = depth² - r², the
  // sphere's projection spans x·depth/a ± r·sqrt(x² + a)/a horizontally, before the field-of-view scale.
  float a = (depth - _radius) * (depth + _radius);
  float tanX = _view.tanHalfFovY * _view.aspect;
  float tanY = _view.tanHalfFovY;
  float centerX = x * depth / a;
  float centerY = y * depth / a;
  float halfX = _radius * sqrt(x * x + a) / a;
  float halfY = _radius * sqrt(y * y + a) / a;
  _minNdc = float2((centerX - halfX) / tanX, (centerY - halfY) / tanY);
  _maxNdc = float2((centerX + halfX) / tanX, (centerY + halfY) / tanY);
  return true;
}

void AddBoundedPoint(ViewPoint _viewPoint, float _tanX, float _tanY, inout float2 _minNdc, inout float2 _maxNdc, inout float _nearest)
{
  float2 ndc = float2(_viewPoint.x / (_viewPoint.depth * _tanX), _viewPoint.y / (_viewPoint.depth * _tanY));
  _minNdc = min(_minNdc, ndc);
  _maxNdc = max(_maxNdc, ndc);
  _nearest = min(_nearest, _viewPoint.depth);
}

// The precise path: the bounds of the box's corners, or of its edges clipped against the near plane when it crosses it,
// with the least view depth among the points bounded. Returns false when nothing of the box lies beyond the near plane.
bool PreciseBounds(Box _box, ViewConstants _view, out float2 _minNdc, out float2 _maxNdc, out float _nearestViewDepth)
{
  _minNdc = float2(0.0, 0.0);
  _maxNdc = float2(0.0, 0.0);
  _nearestViewDepth = 0.0;

  ViewPoint corners[8];
  [unroll] for (uint corner = 0; corner < 8; ++corner)
  {
    float signX = (corner & 1u) != 0u ? 1.0 : -1.0;
    float signY = (corner & 2u) != 0u ? 1.0 : -1.0;
    float signZ = (corner & 4u) != 0u ? 1.0 : -1.0;
    float3 position =
      _box.center + _box.axisX * (signX * _box.radius.x) + _box.axisY * (signY * _box.radius.y) + _box.axisZ * (signZ * _box.radius.z);
    corners[corner] = ToView(position, _view);
  }

  float tanX = _view.tanHalfFovY * _view.aspect;
  float tanY = _view.tanHalfFovY;
  float nearPlane = _view.nearPlane;
  float2 minNdc = float2(UNBOUNDED, UNBOUNDED);
  float2 maxNdc = float2(-UNBOUNDED, -UNBOUNDED);
  float nearest = UNBOUNDED;

  // Every corner beyond the near plane, and where an edge crosses it, the crossing. With the whole box beyond the plane
  // no edge crosses, and this is simply the bounds of the eight corners.
  [unroll] for (uint i = 0; i < 8; ++i)
  {
    ViewPoint from = corners[i];
    if (from.depth >= nearPlane)
    {
      AddBoundedPoint(from, tanX, tanY, minNdc, maxNdc, nearest);
    }
    [unroll] for (uint bit = 1u; bit < 8u; bit <<= 1u)
    {
      ViewPoint to = corners[i | bit];
      if ((i & bit) == 0u && (from.depth >= nearPlane) != (to.depth >= nearPlane))
      {
        float t = (nearPlane - from.depth) / (to.depth - from.depth);
        ViewPoint crossing;
        crossing.x = from.x + (to.x - from.x) * t;
        crossing.y = from.y + (to.y - from.y) * t;
        crossing.depth = nearPlane;
        AddBoundedPoint(crossing, tanX, tanY, minNdc, maxNdc, nearest);
      }
    }
  }
  if (!(nearest < UNBOUNDED))
  {
    return false;
  }
  _minNdc = minNdc;
  _maxNdc = maxNdc;
  _nearestViewDepth = nearest;
  return true;
}

// Steps 2 to 4 of §9.2 for a perspective view: culls, bounds and places one box.
SplatBounds PerspectiveSplatBounds(Box _box, ViewConstants _view)
{
  SplatBounds bounds;
  bounds.minNdc = float2(0.0, 0.0);
  bounds.maxNdc = float2(0.0, 0.0);
  bounds.depth = 0.0;
  bounds.visible = false;

  float radius = length(_box.radius);
  float3 offset = _box.center - _view.position;
  float x = dot(offset, _view.right);
  float y = dot(offset, _view.up);
  float depth = dot(offset, _view.forward);
  float tanX = _view.tanHalfFovY * _view.aspect;
  float tanY = _view.tanHalfFovY;

  // Step 2: cull a bounding sphere that lies wholly nearer than the near plane, or wholly outside a side plane.
  float reachX = radius * sqrt(1.0 + tanX * tanX);
  float reachY = radius * sqrt(1.0 + tanY * tanY);
  if (depth + radius < _view.nearPlane || x - tanX * depth > reachX || -x - tanX * depth > reachX || y - tanY * depth > reachY ||
      -y - tanY * depth > reachY)
  {
    return bounds;
  }

  // Step 3: a sphere wholly beyond the near plane whose projection stays within the limit takes the quadric; anything
  // else, including every box that crosses the near plane, takes the precise path.
  float2 minNdc = float2(0.0, 0.0);
  float2 maxNdc = float2(0.0, 0.0);
  float nearestViewDepth = 0.0;
  bool bounded = false;
  if (depth - radius >= _view.nearPlane)
  {
    if (QuadricBounds(_box.center, radius, _view, minNdc, maxNdc))
    {
      float widthPixels = (maxNdc.x - minNdc.x) * 0.5 * float(_view.widthPixels);
      float heightPixels = (maxNdc.y - minNdc.y) * 0.5 * float(_view.heightPixels);
      bounded = widthPixels <= QUADRIC_LIMIT_PIXELS && heightPixels <= QUADRIC_LIMIT_PIXELS;
      nearestViewDepth = depth - radius;
    }
  }
  if (!bounded)
  {
    if (!PreciseBounds(_box, _view, minNdc, maxNdc, nearestViewDepth))
    {
      return bounds;
    }
  }
  if (!ClampAndGrow(minNdc, maxNdc, _view.widthPixels, _view.heightPixels))
  {
    return bounds;
  }

  // Step 4: the rectangle sits at the voxel's nearest point, never nearer than the near plane.
  bounds.minNdc = minNdc;
  bounds.maxNdc = maxNdc;
  bounds.depth = PerspectiveDepth(_view, max(nearestViewDepth, _view.nearPlane));
  bounds.visible = true;
  return bounds;
}
