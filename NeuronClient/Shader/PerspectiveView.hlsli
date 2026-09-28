#pragma once

// A pinhole camera as the view splat pass sees it (Design/Archive/SampleRenderer.md §7.5, §9.3). The C++ twins are in
// NeuronCore/PerspectiveView.h (R15); here the view is the constant buffer itself.

#include "Ray.hlsli"
#include "ViewConstants.hlsli"

// Normalized device coordinates of a pixel's centre, x right and y up. _pixelCenter is SV_Position.xy, which is the
// integer pixel plus one half, so this is the twin's arithmetic on the same values.
float2 PixelCenterNdc(float2 _pixelCenter, uint _widthPixels, uint _heightPixels)
{
  return float2(2.0 * _pixelCenter.x / float(_widthPixels) - 1.0, 1.0 - 2.0 * _pixelCenter.y / float(_heightPixels));
}

// The ray through a pixel's centre. Its direction has a view-depth component of exactly one, so the ray parameter of a
// hit is its view depth.
Ray PerspectiveRay(ViewConstants _view, float2 _pixelCenter)
{
  float2 ndc = PixelCenterNdc(_pixelCenter, _view.widthPixels, _view.heightPixels);
  Ray ray;
  ray.origin = _view.position;
  ray.direction = _view.forward + _view.right * (ndc.x * _view.aspect * _view.tanHalfFovY) + _view.up * (ndc.y * _view.tanHalfFovY);
  return ray;
}

// Reversed-Z with an infinite far plane: depth = n / view depth.
float PerspectiveDepth(ViewConstants _view, float _viewDepth)
{
  return _view.nearPlane / _viewDepth;
}
