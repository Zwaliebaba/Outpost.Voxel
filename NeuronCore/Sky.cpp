#include "pch.h"

#include "Sky.h"

#include "Hash.h"
#include "Quaternion.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace NeuronCore
{
namespace
{

// §11.3's galaxy, tuned by eye. In the galaxy's frame, y is the sine of the latitude and x the cosine of the angle from
// the core. The disk falls off exponentially in y, over a scale that grows toward the core as its brightness does,
// from this share of the core's at the anticentre; the bulge is a lobe about the core, flattened toward the plane.
constexpr float DISK_THICKNESS_CORE = 0.10f;
constexpr float DISK_THICKNESS_ANTICENTER = 0.05f;
constexpr float DISK_ANTICENTER = 0.35f;
constexpr Float3 DISK_COLOR{1.0f, 0.97f, 0.92f};
constexpr float BULGE_BRIGHTNESS = 1.0f;
constexpr float BULGE_SHARPNESS = 60.0f;
constexpr float BULGE_FLATTENING = 2.0f;
constexpr Float3 BULGE_COLOR{1.0f, 0.85f, 0.66f};

// The clouds and the dust are noise over the direction, stretched across the plane so that both run along the band;
// they are evaluated within this sine of the latitude from the plane, fading in over the outer NOISE_FADE of it, and
// beyond it the disk is too faint for either to show.
constexpr float NOISE_LATITUDE = 0.6f;
constexpr float NOISE_FADE = 0.2f;

// The star clouds: the disk times e to the ± this contrast, by octaves of value noise.
constexpr float CLOUD_CONTRAST = 1.0f;
constexpr float CLOUD_STRETCH = 2.5f;
constexpr float CLOUD_FREQUENCY = 12.0f;
constexpr std::uint32_t CLOUD_OCTAVES = 5;

// The dust: clumps where octaves of noise rise above a threshold, in a layer a little above the disk's plane and
// thinner than it, denser toward the core, and deeper for blue than for red, so that it reddens what it dims.
constexpr float DUST_DEPTH = 1.5f;
constexpr float DUST_THICKNESS = 0.03f;
constexpr float DUST_OFFSET = 0.01f;
constexpr float DUST_ANTICENTER = 0.4f;
constexpr float DUST_STRETCH = 7.0f;
constexpr float DUST_FREQUENCY = 10.0f;
constexpr std::uint32_t DUST_OCTAVES = 5;
constexpr float DUST_THRESHOLD = 0.45f;
constexpr float DUST_SOFTNESS = 0.25f;
constexpr std::uint32_t DUST_SEED = 0x9E3779B9u;
constexpr Float3 DUST_REDDENING{1.0f, 1.35f, 1.8f};

// Each octave's lattice is moved by this much against the one before, so that no two octaves line up.
constexpr Float3 OCTAVE_SHIFT{0.37f, 0.61f, 0.83f};

// 1 / (σ √2) for the star's point-spread function, in pixels: the error function's unit.
constexpr float STAR_EDGE_SCALE = 1.0101525f;

// A star further than about 89 degrees from the view's axis is never on screen, and is not projected.
constexpr float STAR_LEAST_DEPTH = 0.01f;

[[nodiscard]] float Lerp(float _from, float _to, float _share) noexcept
{
  return _from + (_to - _from) * _share;
}

// The value at a lattice point: 24 bits of PcgHash chained over its coordinates and the seed, from 0 to 1.
[[nodiscard]] float LatticeValue(std::uint32_t _x, std::uint32_t _y, std::uint32_t _z, std::uint32_t _seed) noexcept
{
  return static_cast<float>(PcgHash(_x + PcgHash(_y + PcgHash(_z + _seed))) >> 8u) * (1.0f / 16777216.0f);
}

[[nodiscard]] std::uint32_t LatticeCoordinate(float _cell) noexcept
{
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(_cell));
}

// Value noise at _point: the lattice's values blended trilinearly, with smoothstep's weights, from 0 to 1.
[[nodiscard]] float ValueNoise(Float3 _point, std::uint32_t _seed) noexcept
{
  const Float3 cell{std::floor(_point.x), std::floor(_point.y), std::floor(_point.z)};
  const Float3 inside = _point - cell;
  const Float3 weight = inside * inside * (Float3{3.0f, 3.0f, 3.0f} - inside * 2.0f);
  const std::uint32_t x = LatticeCoordinate(cell.x);
  const std::uint32_t y = LatticeCoordinate(cell.y);
  const std::uint32_t z = LatticeCoordinate(cell.z);
  const float near00 = Lerp(LatticeValue(x, y, z, _seed), LatticeValue(x + 1u, y, z, _seed), weight.x);
  const float near10 = Lerp(LatticeValue(x, y + 1u, z, _seed), LatticeValue(x + 1u, y + 1u, z, _seed), weight.x);
  const float far00 = Lerp(LatticeValue(x, y, z + 1u, _seed), LatticeValue(x + 1u, y, z + 1u, _seed), weight.x);
  const float far10 = Lerp(LatticeValue(x, y + 1u, z + 1u, _seed), LatticeValue(x + 1u, y + 1u, z + 1u, _seed), weight.x);
  return Lerp(Lerp(near00, near10, weight.y), Lerp(far00, far10, weight.y), weight.z);
}

// Octaves of value noise at _point, at doubling frequencies and halving weights, each with a seed of its own: from 0
// to 1.
[[nodiscard]] float Fbm(Float3 _point, std::uint32_t _seed, std::uint32_t _octaves) noexcept
{
  Float3 at = _point;
  float sum = 0.0f;
  float total = 0.0f;
  float weight = 0.5f;
  for (std::uint32_t octave = 0; octave < _octaves; ++octave)
  {
    sum += weight * ValueNoise(at, _seed + octave);
    total += weight;
    weight *= 0.5f;
    at = at * 2.0f + OCTAVE_SHIFT;
  }
  return sum / total;
}

} // namespace

SkyParameters MakeSkyParameters(const WorldSettings& _settings) noexcept
{
  const float radius = _settings.sunAngularRadiusRadians;
  const float darkenedSolidAngle = std::numbers::pi_v<float> * radius * radius * (1.0f - SUN_LIMB_DARKENING / 3.0f);
  return {
    _settings.toSun, radius,   _settings.sunRadiance * (1.0f / darkenedSolidAngle), RotationOf(_settings.galacticPlane), _settings.skySeed,
    GALAXY_GAIN,     STAR_GAIN};
}

Float3 GalaxyRadiance(Float3 _direction, std::uint32_t _seed) noexcept
{
  // From 1 toward the core to 0 toward the anticentre.
  const float core = 0.5f + 0.5f * _direction.x;
  const float latitude = std::abs(_direction.y);
  float disk = Lerp(DISK_ANTICENTER, 1.0f, core) * std::exp(-latitude / Lerp(DISK_THICKNESS_ANTICENTER, DISK_THICKNESS_CORE, core));
  const float bulge =
    BULGE_BRIGHTNESS * std::exp(-BULGE_SHARPNESS * ((1.0f - _direction.x) + BULGE_FLATTENING * _direction.y * _direction.y));
  float depth = 0.0f;
  const float noise = std::clamp((NOISE_LATITUDE - latitude) / NOISE_FADE, 0.0f, 1.0f);
  if (noise > 0.0f)
  {
    const Float3 cloudPoint = Float3{_direction.x, _direction.y * CLOUD_STRETCH, _direction.z} * CLOUD_FREQUENCY;
    const Float3 dustPoint = Float3{_direction.x, _direction.y * DUST_STRETCH, _direction.z} * DUST_FREQUENCY;
    disk *= std::exp(noise * CLOUD_CONTRAST * (2.0f * Fbm(cloudPoint, _seed, CLOUD_OCTAVES) - 1.0f));
    const float clumps = std::clamp((Fbm(dustPoint, _seed + DUST_SEED, DUST_OCTAVES) - DUST_THRESHOLD) / DUST_SOFTNESS, 0.0f, 1.0f);
    depth =
      noise * DUST_DEPTH * Lerp(DUST_ANTICENTER, 1.0f, core) * std::exp(-std::abs(_direction.y - DUST_OFFSET) / DUST_THICKNESS) * clumps;
  }
  const Float3 transmission{std::exp(-depth * DUST_REDDENING.x), std::exp(-depth * DUST_REDDENING.y), std::exp(-depth * DUST_REDDENING.z)};
  return (DISK_COLOR * disk + BULGE_COLOR * bulge) * transmission;
}

Float3 SunRadiance(Float3 _direction, const SkyParameters& _sky, float _pixelRadians) noexcept
{
  // The angle from the disc's middle, taken as the chord between the two unit vectors. Within a pixel of the disc the
  // two differ by less than a millionth of the angle, its square over 24; the chord keeps its precision where an angle
  // from the cosine would not, and it needs no arcsine, which a GPU may evaluate coarsely (Design/ADR/ADR-021).
  const float angle = Length(_direction - _sky.toSun);
  const float coverage = std::clamp((_sky.sunAngularRadiusRadians - angle) / _pixelRadians + 0.5f, 0.0f, 1.0f);
  // μ, the cosine between the line of sight and the sun's surface, from how far across the disc the direction lies.
  const float across = std::min(angle / _sky.sunAngularRadiusRadians, 1.0f);
  const float mu = std::sqrt(1.0f - across * across);
  return _sky.sunRadiance * ((1.0f - SUN_LIMB_DARKENING * (1.0f - mu)) * coverage);
}

float PixelRadians(const PerspectiveView& _view) noexcept
{
  return 2.0f * _view.tanHalfFovY / static_cast<float>(_view.heightPixels);
}

Float3 SkyPixel(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY, const SkyParameters& _sky) noexcept
{
  const Float3 direction = Normalize(PerspectiveRay(_view, _pixelX, _pixelY).direction);
  const Float3 galactic{Dot(_sky.galaxy.axisX, direction), Dot(_sky.galaxy.axisY, direction), Dot(_sky.galaxy.axisZ, direction)};
  return GalaxyRadiance(galactic, _sky.seed) * _sky.galaxyGain + SunRadiance(direction, _sky, PixelRadians(_view));
}

float Erfc(float _x) noexcept
{
  const float t = 1.0f / (1.0f + 0.3275911f * _x);
  const float polynomial = t * (0.254829592f + t * (-0.284496736f + t * (1.421413741f + t * (-1.453152027f + t * 1.061405429f))));
  return polynomial * std::exp(-_x * _x);
}

float StarAxisShare(float _offsetPixels) noexcept
{
  // ½ (erf(upper) - erf(lower)) over the pixel's two edges, in the error function's unit, written from the tails
  // wherever both edges lie on one side of the star, where the two error functions are close and their difference would
  // cancel.
  const float lower = (_offsetPixels - 0.5f) * STAR_EDGE_SCALE;
  const float upper = (_offsetPixels + 0.5f) * STAR_EDGE_SCALE;
  if (lower >= 0.0f)
  {
    return 0.5f * (Erfc(lower) - Erfc(upper));
  }
  if (upper <= 0.0f)
  {
    return 0.5f * (Erfc(-upper) - Erfc(-lower));
  }
  return 1.0f - 0.5f * (Erfc(-lower) + Erfc(upper));
}

float StarShare(Float2 _offsetPixels) noexcept
{
  return StarAxisShare(_offsetPixels.x) * StarAxisShare(_offsetPixels.y);
}

std::optional<Float2> StarPosition(const PerspectiveView& _view, Float3 _direction) noexcept
{
  const float depth = Dot(_direction, _view.forward);
  if (depth < STAR_LEAST_DEPTH)
  {
    return std::nullopt;
  }
  const float ndcX = Dot(_direction, _view.right) / (depth * _view.aspect * _view.tanHalfFovY);
  const float ndcY = Dot(_direction, _view.up) / (depth * _view.tanHalfFovY);
  return Float2{(ndcX + 1.0f) * 0.5f * static_cast<float>(_view.widthPixels),
                (1.0f - ndcY) * 0.5f * static_cast<float>(_view.heightPixels)};
}

float StarQuadRadius(float _brightness) noexcept
{
  // The brightest pixel a star makes is the one its centre falls in the middle of. Outside the radius, one axis's share
  // is below ½ erfc((r - ½) / (σ √2)) <= ½ exp(-(r - ½)² / 2σ²) = ½ STAR_DARKEST_VISIBLE / brightness, and the other's
  // below one.
  const float peak = StarAxisShare(0.0f);
  if (_brightness * peak * peak < STAR_DARKEST_VISIBLE)
  {
    return 0.0f;
  }
  return 0.5f + STAR_SIGMA_PIXELS * std::sqrt(2.0f * std::log(_brightness / STAR_DARKEST_VISIBLE));
}

Float3 StarPixel(const PerspectiveView& _view, const StarRecord& _star, float _starGain, std::uint32_t _pixelX,
                 std::uint32_t _pixelY) noexcept
{
  const std::optional<Float2> position = StarPosition(_view, _star.direction);
  const float brightness = _star.flux * _starGain;
  const float radius = StarQuadRadius(brightness);
  if (!position || radius == 0.0f)
  {
    return {0.0f, 0.0f, 0.0f};
  }
  // The quad holds the pixels whose centres lie inside it, on its left and top edges but not its right and bottom
  // ones, as Direct3D's top-left rule has it.
  const Float2 offset = Float2{static_cast<float>(_pixelX) + 0.5f, static_cast<float>(_pixelY) + 0.5f} - *position;
  if (offset.x < -radius || offset.x >= radius || offset.y < -radius || offset.y >= radius)
  {
    return {0.0f, 0.0f, 0.0f};
  }
  return _star.color * (brightness * StarShare(offset));
}

} // namespace NeuronCore
