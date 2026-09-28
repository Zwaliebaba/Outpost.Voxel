#include "pch.h"

#include "RenderSettings.h"

#include "ColorSpace.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

namespace NeuronCore
{
namespace
{

constexpr float RADIANS_PER_DEGREE = 0.0174532925f;

// The station's values (§3).
constexpr float DEFAULT_SUN_DEGREES = 50.0f;
constexpr float DEFAULT_INTENSITY = 0.7f;
constexpr std::uint8_t DEFAULT_GROUND_BYTE = 80;

// The render object whose _type is _type, or null.
[[nodiscard]] const VoxAttributes* FindObject(std::span<const VoxAttributes> _objects, std::string_view _type) noexcept
{
  for (const VoxAttributes& object : _objects)
  {
    const auto type = object.find("_type");
    if (type != object.end() && type->second == _type)
    {
      return &object;
    }
  }
  return nullptr;
}

// Exactly _values.size() finite numbers separated by spaces, and nothing else.
template <std::size_t Count> [[nodiscard]] bool ParseNumbers(std::string_view _text, std::array<float, Count>& _values) noexcept
{
  std::size_t at = 0;
  for (float& value : _values)
  {
    while (at < _text.size() && _text[at] == ' ')
    {
      ++at;
    }
    const std::from_chars_result result = std::from_chars(_text.data() + at, _text.data() + _text.size(), value);
    if (result.ec != std::errc{} || !std::isfinite(value))
    {
      return false;
    }
    at = static_cast<std::size_t>(result.ptr - _text.data());
  }
  while (at < _text.size() && _text[at] == ' ')
  {
    ++at;
  }
  return at == _text.size();
}

template <std::size_t Count>
[[nodiscard]] bool ReadNumbers(const VoxAttributes* _object, std::string_view _key, std::array<float, Count>& _values) noexcept
{
  if (_object == nullptr)
  {
    return false;
  }
  const auto found = _object->find(_key);
  return found != _object->end() && ParseNumbers(found->second, _values);
}

// A number that may not be negative: an intensity or the exposure.
[[nodiscard]] float ReadAmount(const VoxAttributes* _object, std::string_view _key, float _fallback) noexcept
{
  std::array<float, 1> value{};
  return ReadNumbers(_object, _key, value) && value[0] >= 0.0f ? value[0] : _fallback;
}

// Three bytes, sRGB-encoded as the palette's are, as a linear color.
[[nodiscard]] Float3 ReadColor(const VoxAttributes* _object, std::string_view _key, Float3 _fallback) noexcept
{
  std::array<float, 3> bytes{};
  if (!ReadNumbers(_object, _key, bytes))
  {
    return _fallback;
  }
  for (const float byte : bytes)
  {
    if (!(byte >= 0.0f && byte <= 255.0f) || byte != std::floor(byte))
    {
      return _fallback;
    }
  }
  return {SrgbToLinear(static_cast<std::uint8_t>(bytes[0])), SrgbToLinear(static_cast<std::uint8_t>(bytes[1])),
          SrgbToLinear(static_cast<std::uint8_t>(bytes[2]))};
}

} // namespace

RenderSettings DefaultRenderSettings() noexcept
{
  const float ground = SrgbToLinear(DEFAULT_GROUND_BYTE);
  return {DEFAULT_SUN_DEGREES * RADIANS_PER_DEGREE,
          DEFAULT_SUN_DEGREES * RADIANS_PER_DEGREE,
          {1.0f, 1.0f, 1.0f},
          DEFAULT_INTENSITY,
          {1.0f, 1.0f, 1.0f},
          DEFAULT_INTENSITY,
          {ground, ground, ground},
          {0.0f, 0.0f, 0.0f},
          1.0f};
}

RenderSettings ReadRenderSettings(std::span<const VoxAttributes> _renderObjects)
{
  RenderSettings settings = DefaultRenderSettings();

  const VoxAttributes* sun = FindObject(_renderObjects, "_inf");
  std::array<float, 2> degrees{};
  if (ReadNumbers(sun, "_angle", degrees))
  {
    settings.sunElevationRadians = degrees[0] * RADIANS_PER_DEGREE;
    settings.sunAzimuthRadians = degrees[1] * RADIANS_PER_DEGREE;
  }
  settings.sunColor = ReadColor(sun, "_k", settings.sunColor);
  settings.sunIntensity = ReadAmount(sun, "_i", settings.sunIntensity);

  const VoxAttributes* sky = FindObject(_renderObjects, "_uni");
  settings.skyColor = ReadColor(sky, "_k", settings.skyColor);
  settings.skyIntensity = ReadAmount(sky, "_i", settings.skyIntensity);

  settings.groundColor = ReadColor(FindObject(_renderObjects, "_ground"), "_color", settings.groundColor);
  settings.backgroundColor = ReadColor(FindObject(_renderObjects, "_bg"), "_color", settings.backgroundColor);
  settings.exposure = ReadAmount(FindObject(_renderObjects, "_film"), "_expo", settings.exposure);
  return settings;
}

} // namespace NeuronCore
