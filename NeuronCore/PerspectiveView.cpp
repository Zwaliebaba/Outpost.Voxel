#include "pch.h"

#include "PerspectiveView.h"

#include <cmath>

namespace NeuronCore
{

void MakeViewBasis(Float3 _forward, Float3 _worldUp, Float3& _right, Float3& _up) noexcept
{
  // Left-handed: right × up is +forward. The cross products run in this order for that; the other order would mirror
  // every image, and no test that compares the GPU with its twin could see it (Design/NeuronVoxelFormat.md §12.3).
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

} // namespace NeuronCore
