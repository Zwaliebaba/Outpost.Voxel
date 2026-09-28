#pragma once

// The sun's view for the shadow map (Design/Archive/SampleRenderer.md §7.5, §10): every ray travels along forward from the near
// plane, and depth is standard Z. The C++ twins are in NeuronCore/OrthographicView.h (R15); here the view is the
// constant buffer itself.

#include "PerspectiveView.hlsli"
#include "Ray.hlsli"
#include "ShadowViewConstants.hlsli"

// Standard Z: the far plane, which a shadow map is cleared to, has depth 1, and nearer is smaller.
static const float ORTHOGRAPHIC_FAR_DEPTH = 1.0;

// The ray from a pixel's centre on the near plane, travelling along forward with unit speed: the ray parameter of a hit
// is its distance from the near plane.
Ray OrthographicRay(ShadowViewConstants _view, float2 _pixelCenter)
{
  float2 ndc = PixelCenterNdc(_pixelCenter, _view.widthPixels, _view.heightPixels);
  Ray ray;
  ray.origin = _view.origin + _view.right * (ndc.x * _view.halfWidth) + _view.up * (ndc.y * _view.halfHeight);
  ray.direction = _view.forward;
  return ray;
}

float OrthographicDepth(ShadowViewConstants _view, float _distance)
{
  return _distance / _view.depthRange;
}
