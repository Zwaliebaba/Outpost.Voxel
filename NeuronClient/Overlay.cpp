#include "pch.h"

#include "Overlay.h"

#include "Pick.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace NeuronClient
{
namespace
{

using NeuronCore::Float2;
using NeuronCore::Float3;

// A line cut at the near plane keeps this share of the plane's distance in front of it, so that its end projects.
constexpr float NEAR_MARGIN = 1.001f;

// Cuts the segment from _from to _to to the part of it in front of _view's near plane; false when none of it is.
[[nodiscard]] bool CutAtNearPlane(const NeuronCore::PerspectiveView& _view, Float3& _from, Float3& _to) noexcept
{
  const float nearDepth = _view.nearPlane * NEAR_MARGIN;
  const float fromDepth = NeuronCore::Dot(_from - _view.position, _view.forward);
  const float toDepth = NeuronCore::Dot(_to - _view.position, _view.forward);
  if (fromDepth < nearDepth && toDepth < nearDepth)
  {
    return false;
  }
  if (fromDepth < nearDepth)
  {
    _from = _from + (_to - _from) * ((nearDepth - fromDepth) / (toDepth - fromDepth));
  }
  else if (toDepth < nearDepth)
  {
    _to = _to + (_from - _to) * ((nearDepth - toDepth) / (fromDepth - toDepth));
  }
  return true;
}

} // namespace

void DrawRing(Surface& _surface, const NeuronCore::PerspectiveView& _view, Float3 _center, float _radius, float _widthPixels, Float3 _color,
              float _alpha)
{
  const auto at = [&_center, _radius](std::uint32_t _segment)
  {
    const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(_segment % RING_SEGMENTS) / static_cast<float>(RING_SEGMENTS);
    return Float3{_center.x + _radius * std::cos(angle), _center.y, _center.z + _radius * std::sin(angle)};
  };
  for (std::uint32_t segment = 0; segment < RING_SEGMENTS; ++segment)
  {
    DrawLine(_surface, _view, at(segment), at(segment + 1), _widthPixels, _color, _alpha);
  }
}

void DrawLine(Surface& _surface, const NeuronCore::PerspectiveView& _view, Float3 _from, Float3 _to, float _widthPixels, Float3 _color,
              float _alpha)
{
  Float3 from = _from;
  Float3 to = _to;
  if (!CutAtNearPlane(_view, from, to))
  {
    return;
  }
  const std::optional<ScreenPoint> start = ProjectPoint(_view, from);
  const std::optional<ScreenPoint> end = ProjectPoint(_view, to);
  if (start.has_value() && end.has_value())
  {
    _surface.DrawSegment(start->pixels, end->pixels, _widthPixels, _color, _alpha);
  }
}

void DrawMarker(Surface& _surface, const NeuronCore::PerspectiveView& _view, Float3 _at, float _sizePixels, float _widthPixels,
                Float3 _color, float _alpha)
{
  const std::optional<ScreenPoint> at = ProjectPoint(_view, _at);
  if (!at.has_value())
  {
    return;
  }
  const float half = 0.5f * _sizePixels;
  const Float2 top{at->pixels.x, at->pixels.y - half};
  const Float2 right{at->pixels.x + half, at->pixels.y};
  const Float2 bottom{at->pixels.x, at->pixels.y + half};
  const Float2 left{at->pixels.x - half, at->pixels.y};
  _surface.DrawSegment(top, right, _widthPixels, _color, _alpha);
  _surface.DrawSegment(right, bottom, _widthPixels, _color, _alpha);
  _surface.DrawSegment(bottom, left, _widthPixels, _color, _alpha);
  _surface.DrawSegment(left, top, _widthPixels, _color, _alpha);
}

void DrawBar(Surface& _surface, const NeuronCore::PerspectiveView& _view, Float3 _at, float _raisePixels, float _widthPixels,
             float _heightPixels, float _fraction, Float3 _color, Float3 _backColor, float _alpha)
{
  const std::optional<ScreenPoint> at = ProjectPoint(_view, _at);
  if (!at.has_value())
  {
    return;
  }
  const long left = std::lround(at->pixels.x - 0.5f * _widthPixels);
  const long top = std::lround(at->pixels.y - _raisePixels - 0.5f * _heightPixels);
  const long width = std::lround(_widthPixels);
  const long height = std::lround(_heightPixels);
  const long full = std::lround(_widthPixels * std::clamp(_fraction, 0.0f, 1.0f));
  if (width <= 0 || height <= 0)
  {
    return;
  }
  if (full > 0)
  {
    _surface.FillRectangle(static_cast<std::int32_t>(left), static_cast<std::int32_t>(top), static_cast<std::uint32_t>(full),
                           static_cast<std::uint32_t>(height), _color, _alpha);
  }
  if (full < width)
  {
    _surface.FillRectangle(static_cast<std::int32_t>(left + full), static_cast<std::int32_t>(top), static_cast<std::uint32_t>(width - full),
                           static_cast<std::uint32_t>(height), _backColor, _alpha);
  }
}

} // namespace NeuronClient
