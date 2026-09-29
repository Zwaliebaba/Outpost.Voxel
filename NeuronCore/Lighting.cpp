#include "pch.h"

#include "Lighting.h"

#include "Ray.h"
#include "TraceHit.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace NeuronCore
{
namespace
{

// The depth the border of the map holds: the far plane, which every point's clamped depth passes against.
constexpr float BORDER_DEPTH = ORTHOGRAPHIC_FAR_DEPTH;

constexpr float SHADOW_TAP_COUNT = 9.0f;

// SampleCmpLevelZero at (_u, _v), moved by the whole-texel offset (_offsetX, _offsetY), with a linear comparison filter:
// the four texels around the sample point are each compared with _reference, and the results weighted bilinearly.
[[nodiscard]] float CompareBilinear(const ShadowMapImage& _map, float _u, float _v, std::int32_t _offsetX, std::int32_t _offsetY,
                                    float _reference) noexcept
{
  const float x = _u * static_cast<float>(_map.widthPixels) - 0.5f + static_cast<float>(_offsetX);
  const float y = _v * static_cast<float>(_map.heightPixels) - 0.5f + static_cast<float>(_offsetY);
  const float left = std::floor(x);
  const float top = std::floor(y);
  const float fractionX = x - left;
  const float fractionY = y - top;
  const auto passes = [&_map, _reference](float _texelX, float _texelY)
  {
    float depth = BORDER_DEPTH;
    if (_texelX >= 0.0f && _texelY >= 0.0f && _texelX < static_cast<float>(_map.widthPixels) &&
        _texelY < static_cast<float>(_map.heightPixels))
    {
      depth = _map.depth[static_cast<std::size_t>(_texelY) * _map.widthPixels + static_cast<std::size_t>(_texelX)];
    }
    return _reference <= depth ? 1.0f : 0.0f;
  };
  return (1.0f - fractionX) * (1.0f - fractionY) * passes(left, top) + fractionX * (1.0f - fractionY) * passes(left + 1.0f, top) +
         (1.0f - fractionX) * fractionY * passes(left, top + 1.0f) + fractionX * fractionY * passes(left + 1.0f, top + 1.0f);
}

} // namespace

Float3 SunDirection(float _elevationRadians, float _azimuthRadians) noexcept
{
  const float horizontal = std::cos(_elevationRadians);
  return {std::sin(_azimuthRadians) * horizontal, std::sin(_elevationRadians), -std::cos(_azimuthRadians) * horizontal};
}

float EmissiveScale(const PaletteEntry& _entry) noexcept
{
  return _entry.emissive ? _entry.emit * std::exp2(_entry.flux) : 0.0f;
}

LightingParameters MakeLightingParameters(const WorldSettings& _settings, float _emissiveGain) noexcept
{
  return {_settings.toSun, _settings.sunRadiance, _settings.ambientUpper, 1.0f, _settings.ambientLower, {0.0f, 0.0f, 0.0f}, _emissiveGain};
}

Float3 Ambient(Float3 _normal, const LightingParameters& _lighting) noexcept
{
  const float up = 0.5f + 0.5f * _normal.y;
  return (_lighting.groundColor + (_lighting.skyColor - _lighting.groundColor) * up) * _lighting.skyIntensity;
}

Float3 ShadeSurface(Float3 _albedo, float _emissiveScale, Float3 _normal, float _shadow, const LightingParameters& _lighting) noexcept
{
  const float facing = std::max(Dot(_normal, _lighting.toSun), 0.0f);
  const Float3 light = _lighting.sunRadiance * (facing * _shadow) + Ambient(_normal, _lighting);
  return _albedo * light + _albedo * (_emissiveScale * _lighting.emissiveGain);
}

float ShadowNormalOffset(const OrthographicView& _view) noexcept
{
  return SHADOW_NORMAL_OFFSET_TEXELS * 2.0f * _view.halfWidth / static_cast<float>(_view.widthPixels);
}

float ShadowFactor(const ShadowMapImage& _map, const OrthographicView& _view, Float3 _position) noexcept
{
  const Float3 offset = _position - _view.origin;
  const float u = Dot(offset, _view.right) / _view.halfWidth * 0.5f + 0.5f;
  const float v = 0.5f - Dot(offset, _view.up) / _view.halfHeight * 0.5f;
  const float reference = std::min(OrthographicDepth(_view, Dot(offset, _view.forward)), ORTHOGRAPHIC_FAR_DEPTH);
  float lit = 0.0f;
  for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY)
  {
    for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX)
    {
      lit += CompareBilinear(_map, u, v, offsetX, offsetY, reference);
    }
  }
  return lit / SHADOW_TAP_COUNT;
}

Float3 LightPixel(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY, std::uint32_t _voxel, Float3 _normal,
                  float _depth, Float3 _albedo, float _emissiveScale, const ShadowMapImage& _shadowMap, const OrthographicView& _shadowView,
                  const LightingParameters& _lighting) noexcept
{
  if (_voxel == NO_VOXEL)
  {
    return _lighting.background;
  }
  // The ray's direction has a view depth of one, so the view depth n / depth is its parameter (§9.3).
  const Ray ray = PerspectiveRay(_view, _pixelX, _pixelY);
  const Float3 position = ray.origin + ray.direction * (_view.nearPlane / _depth);
  const float shadow = ShadowFactor(_shadowMap, _shadowView, position + _normal * ShadowNormalOffset(_shadowView));
  return ShadeSurface(_albedo, _emissiveScale, _normal, shadow, _lighting);
}

} // namespace NeuronCore
