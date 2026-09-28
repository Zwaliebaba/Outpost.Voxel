#include "pch.h"

#include "OrthographicView.h"
#include "PerspectiveView.h"

#include <algorithm>
#include <limits>

namespace NeuronCore
{
namespace
{

// How far the shadow view's depth range reaches beyond the box it holds, so that rounding cannot clip a voxel on
// either plane.
constexpr float SHADOW_DEPTH_MARGIN = 1.0f;

} // namespace

OrthographicView MakeOrthographicView(Float3 _origin, Float3 _forward, Float3 _worldUp, float _halfWidth, float _halfHeight,
                                      float _depthRange, std::uint32_t _widthPixels, std::uint32_t _heightPixels) noexcept
{
  OrthographicView view{};
  view.origin = _origin;
  view.forward = Normalize(_forward);
  MakeViewBasis(view.forward, _worldUp, view.right, view.up);
  view.halfWidth = _halfWidth;
  view.halfHeight = _halfHeight;
  view.depthRange = _depthRange;
  view.widthPixels = _widthPixels;
  view.heightPixels = _heightPixels;
  return view;
}

Ray OrthographicRay(const OrthographicView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY) noexcept
{
  const Float2 ndc = PixelCenterNdc(_pixelX, _pixelY, _view.widthPixels, _view.heightPixels);
  const Float3 origin = _view.origin + _view.right * (ndc.x * _view.halfWidth) + _view.up * (ndc.y * _view.halfHeight);
  return {origin, _view.forward};
}

OrthographicView MakeShadowView(Float3 _toSun, Float3 _center, float _halfExtent, Float3 _lower, Float3 _upper,
                                std::uint32_t _sizePixels) noexcept
{
  const Float3 forward = -Normalize(_toSun);
  float nearest = std::numeric_limits<float>::infinity();
  float farthest = -std::numeric_limits<float>::infinity();
  for (std::uint32_t corner = 0; corner < 8; ++corner)
  {
    const Float3 point{(corner & 1u) != 0u ? _upper.x : _lower.x, (corner & 2u) != 0u ? _upper.y : _lower.y,
                       (corner & 4u) != 0u ? _upper.z : _lower.z};
    const float depth = Dot(point - _center, forward);
    nearest = std::min(nearest, depth);
    farthest = std::max(farthest, depth);
  }
  nearest -= SHADOW_DEPTH_MARGIN;
  farthest += SHADOW_DEPTH_MARGIN;
  return MakeOrthographicView(_center + forward * nearest, forward, {0.0f, 0.0f, 1.0f}, _halfExtent, _halfExtent, farthest - nearest,
                              _sizePixels, _sizePixels);
}

} // namespace NeuronCore
