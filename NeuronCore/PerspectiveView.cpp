#include "pch.h"

#include "PerspectiveView.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace NeuronCore
{

void MakeViewBasis(Float3 _forward, Float3 _worldUp, Float3& _right, Float3& _up) noexcept
{
  // Left-handed: right × up is +forward. The cross products run in this order for that; the other order would mirror
  // every image, and no test that compares the GPU with its twin could see it (Design/Archive/NeuronVoxelFormat.md §12.3).
  Float3 side = Cross(_worldUp, _forward);
  if (Dot(side, side) < 1.0e-12f)
  {
    side = Cross({0.0f, 0.0f, 1.0f}, _forward);
  }
  _right = Normalize(side);
  _up = Cross(_forward, _right);
}

PerspectiveView MakePerspectiveView(Float3 _position, Float3 _target, Float3 _worldUp, float _fovYRadians, float _nearPlane,
                                    std::uint32_t _widthPixels, std::uint32_t _heightPixels) noexcept
{
  PerspectiveView view{};
  view.position = _position;
  view.forward = Normalize(_target - _position);
  MakeViewBasis(view.forward, _worldUp, view.right, view.up);
  view.tanHalfFovY = std::tan(0.5f * _fovYRadians);
  view.aspect = static_cast<float>(_widthPixels) / static_cast<float>(_heightPixels);
  view.nearPlane = _nearPlane;
  view.widthPixels = _widthPixels;
  view.heightPixels = _heightPixels;
  return view;
}

Float2 PixelCenterNdc(std::uint32_t _pixelX, std::uint32_t _pixelY, std::uint32_t _widthPixels, std::uint32_t _heightPixels) noexcept
{
  // Written so that the centre column and row of an odd-sized image come out as exactly zero.
  const float x = 2.0f * (static_cast<float>(_pixelX) + 0.5f) / static_cast<float>(_widthPixels) - 1.0f;
  const float y = 1.0f - 2.0f * (static_cast<float>(_pixelY) + 0.5f) / static_cast<float>(_heightPixels);
  return {x, y};
}

Ray PerspectiveRay(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY) noexcept
{
  const Float2 ndc = PixelCenterNdc(_pixelX, _pixelY, _view.widthPixels, _view.heightPixels);
  const Float3 direction =
    _view.forward + _view.right * (ndc.x * _view.aspect * _view.tanHalfFovY) + _view.up * (ndc.y * _view.tanHalfFovY);
  return {_view.position, direction};
}

bool IsInView(const PerspectiveView& _view, const Sphere& _sphere) noexcept
{
  // The center in the view's axes. Each side is a plane through the eye where |lateral| = tan × depth, and the center
  // lies (|lateral| - tan × depth) / √(1 + tan²) beyond it.
  const Float3 offset = _sphere.center - _view.position;
  const float lateralX = Dot(offset, _view.right);
  const float lateralY = Dot(offset, _view.up);
  const float depth = Dot(offset, _view.forward);
  const float reach = _sphere.radius + CULL_MARGIN;
  const auto beyondSide = [depth, reach](float _lateral, float _tan) noexcept
  { return std::abs(_lateral) - _tan * depth > reach * std::sqrt(1.0f + _tan * _tan); };
  return depth >= _view.nearPlane - reach && !beyondSide(lateralX, _view.tanHalfFovY * _view.aspect) &&
         !beyondSide(lateralY, _view.tanHalfFovY);
}

std::vector<std::uint32_t> ListViewDraws(const PerspectiveView& _view, std::span<const Sphere> _spheres)
{
  // Each kept sphere, with how far its nearest point is from the eye: negative when the eye is inside it. Sorted as
  // pairs, two spheres as near keep their order.
  std::vector<std::pair<float, std::uint32_t>> kept;
  for (std::uint32_t i = 0; i < _spheres.size(); ++i)
  {
    if (IsInView(_view, _spheres[i]))
    {
      kept.emplace_back(Length(_spheres[i].center - _view.position) - _spheres[i].radius, i);
    }
  }
  std::sort(kept.begin(), kept.end());
  std::vector<std::uint32_t> draws;
  draws.reserve(kept.size());
  for (const std::pair<float, std::uint32_t>& sphere : kept)
  {
    draws.push_back(sphere.second);
  }
  return draws;
}

} // namespace NeuronCore
