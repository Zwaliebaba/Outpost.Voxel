#include "pch.h"

#include "SplatBounds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace VoxelCore
{
namespace
{

// A point in view coordinates: x right, y up, depth along forward.
struct ViewPoint
{
  float x;
  float y;
  float depth;
};

[[nodiscard]] ViewPoint ToView(Float3 _point, const PerspectiveView& _view) noexcept
{
  const Float3 offset = _point - _view.position;
  return {Dot(offset, _view.right), Dot(offset, _view.up), Dot(offset, _view.forward)};
}

// Clamps a rectangle to the viewport and grows it by the margin. Returns false when nothing is left to draw.
[[nodiscard]] bool ClampAndGrow(Float2& _minNdc, Float2& _maxNdc, std::uint32_t _widthPixels, std::uint32_t _heightPixels) noexcept
{
  _minNdc = Max(_minNdc, {-1.0f, -1.0f});
  _maxNdc = Min(_maxNdc, {1.0f, 1.0f});
  if (_minNdc.x > _maxNdc.x || _minNdc.y > _maxNdc.y)
  {
    return false;
  }
  const Float2 margin{2.0f * BOUNDS_MARGIN_PIXELS / static_cast<float>(_widthPixels),
                      2.0f * BOUNDS_MARGIN_PIXELS / static_cast<float>(_heightPixels)};
  _minNdc = _minNdc - margin;
  _maxNdc = _maxNdc + margin;
  return true;
}

} // namespace

bool QuadricBounds(Float3 _center, float _radius, const PerspectiveView& _view, Float2& _minNdc, Float2& _maxNdc) noexcept
{
  const Float3 offset = _center - _view.position;
  const float x = Dot(offset, _view.right);
  const float y = Dot(offset, _view.up);
  const float depth = Dot(offset, _view.forward);
  if (!(depth > _radius))
  {
    return false;
  }

  // Listing 4's quadric, written out for an orthonormal view basis and relative to the camera, which saves the
  // cancellation a world-space matrix suffers far from the origin. With a = depth² - r², the sphere's projection spans
  // x·depth/a ± r·sqrt(x² + a)/a horizontally, before the field-of-view scale, and likewise vertically.
  const float a = (depth - _radius) * (depth + _radius);
  const float tanX = _view.tanHalfFovY * _view.aspect;
  const float tanY = _view.tanHalfFovY;
  const float centerX = x * depth / a;
  const float centerY = y * depth / a;
  const float halfX = _radius * std::sqrt(x * x + a) / a;
  const float halfY = _radius * std::sqrt(y * y + a) / a;
  _minNdc = {(centerX - halfX) / tanX, (centerY - halfY) / tanY};
  _maxNdc = {(centerX + halfX) / tanX, (centerY + halfY) / tanY};
  return true;
}

bool PreciseBounds(const Box& _box, const PerspectiveView& _view, Float2& _minNdc, Float2& _maxNdc, float& _nearestViewDepth) noexcept
{
  std::array<ViewPoint, 8> corners{};
  for (std::uint32_t i = 0; i < corners.size(); ++i)
  {
    const float signX = (i & 1u) != 0u ? 1.0f : -1.0f;
    const float signY = (i & 2u) != 0u ? 1.0f : -1.0f;
    const float signZ = (i & 4u) != 0u ? 1.0f : -1.0f;
    const Float3 corner =
      _box.center + _box.axisX * (signX * _box.radius.x) + _box.axisY * (signY * _box.radius.y) + _box.axisZ * (signZ * _box.radius.z);
    corners[i] = ToView(corner, _view);
  }

  constexpr float UNBOUNDED = std::numeric_limits<float>::infinity();
  const float tanX = _view.tanHalfFovY * _view.aspect;
  const float tanY = _view.tanHalfFovY;
  const float nearPlane = _view.nearPlane;
  Float2 minNdc{UNBOUNDED, UNBOUNDED};
  Float2 maxNdc{-UNBOUNDED, -UNBOUNDED};
  float nearest = UNBOUNDED;
  const auto add = [&](const ViewPoint& _point)
  {
    const Float2 ndc{_point.x / (_point.depth * tanX), _point.y / (_point.depth * tanY)};
    minNdc = Min(minNdc, ndc);
    maxNdc = Max(maxNdc, ndc);
    nearest = std::min(nearest, _point.depth);
  };

  // Every corner beyond the near plane, and where an edge crosses it, the crossing. With the whole box beyond the
  // plane no edge crosses, and this is simply the bounds of the eight corners.
  for (std::uint32_t i = 0; i < corners.size(); ++i)
  {
    const ViewPoint& from = corners[i];
    if (from.depth >= nearPlane)
    {
      add(from);
    }
    for (std::uint32_t bit = 1u; bit < corners.size(); bit <<= 1u)
    {
      const ViewPoint& to = corners[i | bit];
      if ((i & bit) == 0u && (from.depth >= nearPlane) != (to.depth >= nearPlane))
      {
        const float t = (nearPlane - from.depth) / (to.depth - from.depth);
        add({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, nearPlane});
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

SplatBounds PerspectiveSplatBounds(const Box& _box, const PerspectiveView& _view) noexcept
{
  SplatBounds bounds{};
  const float radius = Length(_box.radius);
  const Float3 offset = _box.center - _view.position;
  const float x = Dot(offset, _view.right);
  const float y = Dot(offset, _view.up);
  const float depth = Dot(offset, _view.forward);
  const float tanX = _view.tanHalfFovY * _view.aspect;
  const float tanY = _view.tanHalfFovY;

  // Step 2: cull a bounding sphere that lies wholly nearer than the near plane, or wholly outside a side plane.
  const float reachX = radius * std::sqrt(1.0f + tanX * tanX);
  const float reachY = radius * std::sqrt(1.0f + tanY * tanY);
  if (depth + radius < _view.nearPlane || x - tanX * depth > reachX || -x - tanX * depth > reachX || y - tanY * depth > reachY ||
      -y - tanY * depth > reachY)
  {
    return bounds;
  }

  // Step 3: a sphere wholly beyond the near plane whose projection stays within the limit takes the quadric; anything
  // else, including every box that crosses the near plane, takes the precise path.
  Float2 minNdc{};
  Float2 maxNdc{};
  float nearestViewDepth = 0.0f;
  bool bounded = false;
  if (depth - radius >= _view.nearPlane && QuadricBounds(_box.center, radius, _view, minNdc, maxNdc))
  {
    const float widthPixels = (maxNdc.x - minNdc.x) * 0.5f * static_cast<float>(_view.widthPixels);
    const float heightPixels = (maxNdc.y - minNdc.y) * 0.5f * static_cast<float>(_view.heightPixels);
    bounded = widthPixels <= QUADRIC_LIMIT_PIXELS && heightPixels <= QUADRIC_LIMIT_PIXELS;
    nearestViewDepth = depth - radius;
  }
  if (!bounded && !PreciseBounds(_box, _view, minNdc, maxNdc, nearestViewDepth))
  {
    return bounds;
  }
  if (!ClampAndGrow(minNdc, maxNdc, _view.widthPixels, _view.heightPixels))
  {
    return bounds;
  }

  // Step 4: the rectangle sits at the voxel's nearest point, never nearer than the near plane.
  bounds.minNdc = minNdc;
  bounds.maxNdc = maxNdc;
  bounds.depth = PerspectiveDepth(_view, std::max(nearestViewDepth, _view.nearPlane));
  bounds.visible = true;
  return bounds;
}

SplatBounds OrthographicSplatBounds(const Box& _box, const OrthographicView& _view) noexcept
{
  SplatBounds bounds{};
  const Float3 offset = _box.center - _view.origin;
  const float x = Dot(offset, _view.right);
  const float y = Dot(offset, _view.up);
  const float depth = Dot(offset, _view.forward);

  // The box's exact half-extent along a view axis: |R^T| times the radius.
  const auto extent = [&_box](Float3 _axis)
  {
    return std::abs(Dot(_box.axisX, _axis)) * _box.radius.x + std::abs(Dot(_box.axisY, _axis)) * _box.radius.y +
           std::abs(Dot(_box.axisZ, _axis)) * _box.radius.z;
  };
  const float extentX = extent(_view.right);
  const float extentY = extent(_view.up);
  const float extentDepth = extent(_view.forward);
  if (depth + extentDepth < 0.0f || depth - extentDepth > _view.depthRange)
  {
    return bounds;
  }

  Float2 minNdc{(x - extentX) / _view.halfWidth, (y - extentY) / _view.halfHeight};
  Float2 maxNdc{(x + extentX) / _view.halfWidth, (y + extentY) / _view.halfHeight};
  if (!ClampAndGrow(minNdc, maxNdc, _view.widthPixels, _view.heightPixels))
  {
    return bounds;
  }
  bounds.minNdc = minNdc;
  bounds.maxNdc = maxNdc;
  bounds.depth = OrthographicDepth(_view, std::max(depth - extentDepth, 0.0f));
  bounds.visible = true;
  return bounds;
}

} // namespace VoxelCore
