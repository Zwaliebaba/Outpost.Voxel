#include "pch.h"

#include "OrthographicView.h"
#include "PerspectiveView.h"

namespace NeuronCore
{

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

} // namespace NeuronCore
