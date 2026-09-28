#pragma once

// The lighting of Design/SampleRenderer.md §10 and §11: the sun with its shadow map, a hemisphere of sky and ground, and
// emissive palette entries that light only themselves. The C++ twins are in NeuronCore/Lighting.h (R15), function for
// function.

#include "LightingConstants.hlsli"
#include "OrthographicView.hlsli"
#include "Packing.hlsli"
#include "PerspectiveView.hlsli"
#include "Ray.hlsli"
#include "ShadowViewConstants.hlsli"
#include "ViewConstants.hlsli"

// §11's hemisphere: _uni _i × lerp(ground color, sky color, ½ + ½ N.y), written out as the twin writes it.
float3 Ambient(float3 _normal, LightingConstants _lighting)
{
  float up = 0.5 + 0.5 * _normal.y;
  return (_lighting.groundAlbedo + (_lighting.skyColor - _lighting.groundAlbedo) * up) * _lighting.skyIntensity;
}

// §11: C = albedo × (E_sun × max(0, N·S) × shadow + ambient(N)) + albedo × emissive, with the emissive scale times the
// gain.
float3 ShadeSurface(float3 _albedo, float _emissiveScale, float3 _normal, float _shadow, LightingConstants _lighting)
{
  float facing = max(dot(_normal, _lighting.toSun), 0.0);
  float3 light = _lighting.sunRadiance * (facing * _shadow) + Ambient(_normal, _lighting);
  return _albedo * light + _albedo * (_emissiveScale * _lighting.emissiveGain);
}

// The fraction of the sun _position sees: nine comparison taps a texel apart around its place in the map, each filtered
// bilinearly by the sampler's linear comparison filter, LESS_EQUAL against the map and lit at the border. The depth is
// clamped to the far plane, so that a point beyond it is lit wherever the map holds nothing.
float ShadowFactor(Texture2D<float> _map, SamplerComparisonState _sampler, ShadowViewConstants _view, float3 _position)
{
  float3 offset = _position - _view.origin;
  float2 uv = float2(dot(offset, _view.right) / _view.halfWidth * 0.5 + 0.5, 0.5 - dot(offset, _view.up) / _view.halfHeight * 0.5);
  float reference = min(OrthographicDepth(_view, dot(offset, _view.forward)), ORTHOGRAPHIC_FAR_DEPTH);
  float lit = _map.SampleCmpLevelZero(_sampler, uv, reference, int2(-1, -1));
  lit += _map.SampleCmpLevelZero(_sampler, uv, reference, int2(0, -1));
  lit += _map.SampleCmpLevelZero(_sampler, uv, reference, int2(1, -1));
  lit += _map.SampleCmpLevelZero(_sampler, uv, reference, int2(-1, 0));
  lit += _map.SampleCmpLevelZero(_sampler, uv, reference, int2(0, 0));
  lit += _map.SampleCmpLevelZero(_sampler, uv, reference, int2(1, 0));
  lit += _map.SampleCmpLevelZero(_sampler, uv, reference, int2(-1, 1));
  lit += _map.SampleCmpLevelZero(_sampler, uv, reference, int2(0, 1));
  lit += _map.SampleCmpLevelZero(_sampler, uv, reference, int2(1, 1));
  return lit / 9.0;
}

// What the lighting pass writes for the pixel whose centre is _pixelCenter: a voxel at the depth the view splat wrote,
// the ground plane y = 0 where no voxel was hit and the ray meets it from above, and otherwise the background.
float3 LightPixel(ViewConstants _view, float2 _pixelCenter, uint _voxel, float3 _normal, float _depth, float3 _albedo, float _emissiveScale,
                  Texture2D<float> _shadowMap, SamplerComparisonState _shadowSampler, ShadowViewConstants _shadowView,
                  LightingConstants _lighting)
{
  Ray ray = PerspectiveRay(_view, _pixelCenter);
  if (_voxel != NO_VOXEL)
  {
    // The ray's direction has a view depth of one, so the view depth n / depth is its parameter (§9.3).
    float3 position = ray.origin + ray.direction * (_view.nearPlane / _depth);
    float shadow = ShadowFactor(_shadowMap, _shadowSampler, _shadowView, position + _normal * _lighting.shadowNormalOffset);
    return ShadeSurface(_albedo, _emissiveScale, _normal, shadow, _lighting);
  }
  if (_lighting.groundVisible != 0u && ray.origin.y > 0.0 && ray.direction.y < 0.0)
  {
    float3 up = float3(0.0, 1.0, 0.0);
    float3 position = ray.origin + ray.direction * (-ray.origin.y / ray.direction.y);
    float shadow = ShadowFactor(_shadowMap, _shadowSampler, _shadowView, position + up * _lighting.shadowNormalOffset);
    return ShadeSurface(_lighting.groundAlbedo, 0.0, up, shadow, _lighting);
  }
  return _lighting.background;
}
