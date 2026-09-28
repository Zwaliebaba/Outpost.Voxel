#include "pch.h"

#include "CanvasShading.h"

namespace NeuronClient
{

using NeuronCore::Float2;
using NeuronCore::Float3;
using NeuronCore::Float4;

Float2 CanvasCornerNdc(std::int32_t _pixelX, std::int32_t _pixelY, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                       std::uint32_t _corner, std::uint32_t _targetWidthPixels, std::uint32_t _targetHeightPixels) noexcept
{
  const Float2 corner{static_cast<float>(_pixelX) + static_cast<float>(_widthPixels) * static_cast<float>(_corner & 1u),
                      static_cast<float>(_pixelY) + static_cast<float>(_heightPixels) * static_cast<float>((_corner >> 1u) & 1u)};
  return {2.0f * corner.x / static_cast<float>(_targetWidthPixels) - 1.0f,
          1.0f - 2.0f * corner.y / static_cast<float>(_targetHeightPixels)};
}

void CanvasAtlasTexel(std::int32_t _pixelX, std::int32_t _pixelY, std::int32_t _originX, std::int32_t _originY, std::uint32_t _atlasX,
                      std::uint32_t _atlasY, std::int32_t& _texelX, std::int32_t& _texelY) noexcept
{
  _texelX = static_cast<std::int32_t>(_atlasX) + (_pixelX - _originX);
  _texelY = static_cast<std::int32_t>(_atlasY) + (_pixelY - _originY);
}

Float4 CanvasPremultiply(Float3 _color, float _alpha, float _coverage) noexcept
{
  const float alpha = _alpha * _coverage;
  return {_color.x * alpha, _color.y * alpha, _color.z * alpha, alpha};
}

Float4 CanvasBlend(Float4 _source, Float4 _destination) noexcept
{
  const float keep = 1.0f - _source.w;
  return {_source.x + _destination.x * keep, _source.y + _destination.y * keep, _source.z + _destination.z * keep,
          _source.w + _destination.w * keep};
}

} // namespace NeuronClient
