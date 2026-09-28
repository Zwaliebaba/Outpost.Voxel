#include "pch.h"

#include "StarCatalog.h"

#include "Hash.h"
#include "RigidTransform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace NeuronCore
{
namespace
{

// Where the stars lie (§11.2), tuned by eye: the share of the faintest stars that belong to the disk, a share that falls
// with the square of the faintness to none at the brightest; the disk's thickness, the scale of an exponential in the
// sine of the latitude; and the share of the disk's stars gathered about the core, in a normal spread over this many
// radians of longitude.
constexpr double DISK_SHARE = 0.6;
constexpr double DISK_THICKNESS = 0.12;
constexpr double CORE_SHARE = 0.4;
constexpr double CORE_SPREAD_RADIANS = 0.9;

// The temperatures (§11.2): most stars near a sun's, log-normal about this many kelvin with this spread in the
// logarithm, and the rest evenly in the logarithm over the whole range.
constexpr double TYPICAL_SHARE = 0.8;
constexpr double TYPICAL_KELVIN = 5200.0;
constexpr double TYPICAL_SPREAD = 0.18;

// The spectrum is sampled from 380 to 780 nm, every 5 nm.
constexpr double FIRST_NANOMETERS = 380.0;
constexpr double STEP_NANOMETERS = 5.0;
constexpr std::size_t SAMPLE_COUNT = 81;

// Planck's second radiation constant, hc / k, in nanometer kelvins.
constexpr double SECOND_RADIATION_CONSTANT = 1.438776877e7;

// The streams of one star's randomness.
enum class Stream : std::uint32_t
{
  Magnitude,
  Population,
  Height,
  HeightSign,
  Longitude,
  CoreChoice,
  CoreRadius,
  CoreAngle,
  TemperatureChoice,
  TemperatureRadius,
  TemperatureAngle,
  Count
};

// A number in (0, 1): 24 bits of PcgHash of the star's index and the stream, offset by the seed and hashed again, and
// half a step up, so that neither end is reached.
[[nodiscard]] double Uniform(std::uint32_t _seed, std::uint32_t _index, Stream _stream) noexcept
{
  const std::uint32_t streams = static_cast<std::uint32_t>(Stream::Count);
  const std::uint32_t bits = PcgHash(PcgHash(_index * streams + static_cast<std::uint32_t>(_stream)) + _seed);
  return (static_cast<double>(bits >> 8u) + 0.5) / 16777216.0;
}

// A standard normal number, by Box and Muller's transform of two uniform ones.
[[nodiscard]] double Normal(double _radius, double _angle) noexcept
{
  return std::sqrt(-2.0 * std::log(_radius)) * std::cos(2.0 * std::numbers::pi * _angle);
}

// One lobe of Wyman, Sloan and Shirley's fit: a Gaussian about _peak, with one width below it and another above.
[[nodiscard]] double Lobe(double _nanometers, double _peak, double _below, double _above) noexcept
{
  const double spread = (_nanometers - _peak) / (_nanometers < _peak ? _below : _above);
  return std::exp(-0.5 * spread * spread);
}

// The CIE 1931 color-matching functions at every sample, by the multi-lobe fit of Wyman, Sloan and Shirley, "Simple
// Analytic Approximations to the CIE XYZ Color Matching Functions", JCGT 2(2), 2013.
struct ColorMatching
{
  std::array<double, SAMPLE_COUNT> x;
  std::array<double, SAMPLE_COUNT> y;
  std::array<double, SAMPLE_COUNT> z;
};

[[nodiscard]] double Nanometers(std::size_t _sample) noexcept
{
  return FIRST_NANOMETERS + STEP_NANOMETERS * static_cast<double>(_sample);
}

[[nodiscard]] ColorMatching MakeColorMatching() noexcept
{
  ColorMatching matching{};
  for (std::size_t i = 0; i < SAMPLE_COUNT; ++i)
  {
    const double nanometers = Nanometers(i);
    matching.x[i] = 1.056 * Lobe(nanometers, 599.8, 37.9, 31.0) + 0.362 * Lobe(nanometers, 442.0, 16.0, 26.7) -
                    0.065 * Lobe(nanometers, 501.1, 20.4, 26.2);
    matching.y[i] = 0.821 * Lobe(nanometers, 568.8, 46.9, 40.5) + 0.286 * Lobe(nanometers, 530.9, 16.3, 31.1);
    matching.z[i] = 1.217 * Lobe(nanometers, 437.0, 11.8, 36.0) + 0.681 * Lobe(nanometers, 459.0, 26.0, 13.8);
  }
  return matching;
}

[[nodiscard]] Float3 BlackBodyColor(const ColorMatching& _matching, double _kelvin) noexcept
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  for (std::size_t i = 0; i < SAMPLE_COUNT; ++i)
  {
    // Planck's law without its constant factors, which the normalization removes.
    const double nanometers = Nanometers(i);
    const double radiance = 1.0 / (std::pow(nanometers, 5.0) * std::expm1(SECOND_RADIATION_CONSTANT / (nanometers * _kelvin)));
    x += radiance * _matching.x[i];
    y += radiance * _matching.y[i];
    z += radiance * _matching.z[i];
  }
  // XYZ to linear Rec. 709, under D65.
  const double red = 3.2404542 * x - 1.5371385 * y - 0.4985314 * z;
  const double green = -0.9692660 * x + 1.8760108 * y + 0.0415560 * z;
  const double blue = 0.0556434 * x - 0.2040259 * y + 1.0572252 * z;
  // Unit luminance, then toward white, which has unit luminance too.
  const double luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
  const auto channel = [luminance](double _value) { return static_cast<float>(1.0 + STAR_SATURATION * (_value / luminance - 1.0)); };
  return {channel(red), channel(green), channel(blue)};
}

} // namespace

double FaintestStarMagnitude(std::uint32_t _count) noexcept
{
  return STAR_REFERENCE_MAGNITUDE + std::log10(static_cast<double>(_count) / STAR_REFERENCE_COUNT) / STAR_COUNT_SLOPE;
}

Float3 StarColor(float _kelvin) noexcept
{
  return BlackBodyColor(MakeColorMatching(), _kelvin);
}

std::vector<StarRecord> MakeStarCatalog(std::uint32_t _seed, Quaternion _galacticPlane, std::uint32_t _count)
{
  const ColorMatching matching = MakeColorMatching();
  const Rotation galaxy = RotationOf(_galacticPlane);
  const double faintest = FaintestStarMagnitude(_count);
  std::vector<StarRecord> stars;
  stars.reserve(_count);
  for (std::uint32_t i = 0; i < _count; ++i)
  {
    const auto uniform = [_seed, i](Stream _stream) { return Uniform(_seed, i, _stream); };
    // The law's distribution, inverted, and cut off at the brightest magnitude.
    const double magnitude = std::max(faintest + std::log10(uniform(Stream::Magnitude)) / STAR_COUNT_SLOPE, STAR_BRIGHTEST_MAGNITUDE);

    // The sine of the latitude, and the longitude from the core toward +Z.
    double height = 0.0;
    double longitude = 2.0 * std::numbers::pi * uniform(Stream::Longitude);
    const double faintness = std::clamp((magnitude - STAR_BRIGHTEST_MAGNITUDE) / (faintest - STAR_BRIGHTEST_MAGNITUDE), 0.0, 1.0);
    if (uniform(Stream::Population) < DISK_SHARE * faintness * faintness)
    {
      // The disk: an exponential in the sine of the latitude, cut off at the poles, and in part gathered about the core.
      height = -DISK_THICKNESS * std::log(1.0 + uniform(Stream::Height) * std::expm1(-1.0 / DISK_THICKNESS));
      if (uniform(Stream::HeightSign) < 0.5)
      {
        height = -height;
      }
      if (uniform(Stream::CoreChoice) < CORE_SHARE)
      {
        longitude = CORE_SPREAD_RADIANS * Normal(uniform(Stream::CoreRadius), uniform(Stream::CoreAngle));
      }
    }
    else
    {
      // Evenly over the sphere.
      height = 2.0 * uniform(Stream::Height) - 1.0;
    }
    const double across = std::sqrt(std::max(1.0 - height * height, 0.0));
    const Float3 local{static_cast<float>(across * std::cos(longitude)), static_cast<float>(height),
                       static_cast<float>(across * std::sin(longitude))};

    const double logKelvin =
      uniform(Stream::TemperatureChoice) < TYPICAL_SHARE
        ? std::log(TYPICAL_KELVIN) + TYPICAL_SPREAD * Normal(uniform(Stream::TemperatureRadius), uniform(Stream::TemperatureAngle))
        : std::lerp(std::log(double{STAR_COOLEST_KELVIN}), std::log(double{STAR_HOTTEST_KELVIN}), uniform(Stream::TemperatureRadius));
    const double kelvin = std::clamp(std::exp(logKelvin), double{STAR_COOLEST_KELVIN}, double{STAR_HOTTEST_KELVIN});

    stars.push_back(
      {Normalize(RotateVector(galaxy, local)), static_cast<float>(std::pow(10.0, -0.4 * magnitude)), BlackBodyColor(matching, kelvin)});
  }
  return stars;
}

} // namespace NeuronCore
