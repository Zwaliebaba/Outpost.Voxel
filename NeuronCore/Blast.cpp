#include "pch.h"

#include "Blast.h"

#include "Hash.h"
#include "StarCatalog.h"
#include "TraceHit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace NeuronCore
{
namespace
{

// ADR-025's defaults. Heat: the distance within which a fragment starts hot, as a fraction of the model's radius and at
// least a few voxels; how fast a lone voxel cools; and the radiance of white heat.
constexpr float HEAT_DISTANCE_FRACTION = 0.3f;
constexpr float MIN_HEAT_DISTANCE = 2.0f;
constexpr float COOLING_RATE = 0.8f;
constexpr float HEAT_GAIN = 6.0f;

// The flash: how fast it fades, its irradiance at a distance of the model's radius at its peak, and the distance within
// which it stops growing, as a fraction of the radius.
constexpr float FLASH_SECONDS = 0.15f;
constexpr float FLASH_IRRADIANCE = 4.0f;
constexpr float FLASH_SOFTNESS_FRACTION = 0.15f;

// The shell: its reach as a multiple of the model's radius, how fast it grows towards it, its thickness as a fraction of
// its radius beyond one voxel, how fast it fades, the radiance of a path straight through it at its peak, and the noise's
// cells across its radius.
constexpr float SHELL_REACH = 2.0f;
constexpr float SHELL_GROWTH_SECONDS = 0.4f;
constexpr float SHELL_THICKNESS_FRACTION = 0.12f;
constexpr float SHELL_FADE_SECONDS = 0.5f;
constexpr float SHELL_GAIN = 3.0f;
constexpr float SHELL_NOISE_CELLS = 5.0f;
constexpr float SHELL_SMALLEST_RADIUS = 0.5f;

// The shell's density beyond three thicknesses from its peak, e^-9, is taken as none.
constexpr float SHELL_REACH_THICKNESSES = 3.0f;

// A flash or a shell fainter than this share of its peak is not lit.
constexpr float LIGHT_CUTOFF = 1.0e-3f;

constexpr float SQRT_PI = 1.77245385f;

// A ray that meets no voxel goes on without end.
constexpr float ENDLESS = std::numeric_limits<float>::max();

// The ramp's colors: the black body's at even steps of temperature, unit luminance, with any channel below Rec. 709
// clamped to zero.
[[nodiscard]] std::array<Float4, HEAT_COLOR_STEPS> MakeHeatColors() noexcept
{
  std::array<Float4, HEAT_COLOR_STEPS> colors{};
  for (std::uint32_t step = 0; step < HEAT_COLOR_STEPS; ++step)
  {
    const float share = static_cast<float>(step) / static_cast<float>(HEAT_COLOR_STEPS - 1u);
    const Float3 color = BlackBodyColor(COLDEST_GLOW_KELVIN + (HOTTEST_GLOW_KELVIN - COLDEST_GLOW_KELVIN) * share);
    colors[step] = {std::max(color.x, 0.0f), std::max(color.y, 0.0f), std::max(color.z, 0.0f), 0.0f};
  }
  return colors;
}

[[nodiscard]] const std::array<Float4, HEAT_COLOR_STEPS>& HeatColors() noexcept
{
  static const std::array<Float4, HEAT_COLOR_STEPS> HEAT_COLORS = MakeHeatColors();
  return HEAT_COLORS;
}

[[nodiscard]] Float3 RampColor(float _heat, const std::array<Float4, HEAT_COLOR_STEPS>& _colors) noexcept
{
  const float along = std::clamp(_heat, 0.0f, 1.0f) * static_cast<float>(HEAT_COLOR_STEPS - 1u);
  const std::uint32_t step = std::min(static_cast<std::uint32_t>(std::floor(along)), HEAT_COLOR_STEPS - 2u);
  const float share = along - static_cast<float>(step);
  const Float3 lower{_colors[step].x, _colors[step].y, _colors[step].z};
  const Float3 upper{_colors[step + 1u].x, _colors[step + 1u].y, _colors[step + 1u].z};
  return lower + (upper - lower) * share;
}

// How much of its motion a drag of _drag has let the field make by _timeSeconds, as the debris's twin has it.
[[nodiscard]] float MotionMade(float _drag, float _timeSeconds) noexcept
{
  return _timeSeconds > 0.0f ? 1.0f - std::exp(-_drag * _timeSeconds) : 0.0f;
}

// The indices of _blasts whose share of their peak, _share, is at least LIGHT_CUTOFF, the MAX_LIT_BLASTS nearest
// _viewPosition first, and among as near the earlier.
template <typename Share>
[[nodiscard]] std::vector<std::uint32_t> NearestLit(std::span<const Blast> _blasts, Float3 _viewPosition, Share _share)
{
  std::vector<std::uint32_t> lit;
  for (std::uint32_t i = 0; i < _blasts.size(); ++i)
  {
    if (_blasts[i].timeSeconds > 0.0f && _share(_blasts[i]) >= LIGHT_CUTOFF)
    {
      lit.push_back(i);
    }
  }
  const auto distance = [&](std::uint32_t _index) { return Length(BlastCenter(_blasts[_index]) - _viewPosition); };
  std::ranges::stable_sort(lit, [&](std::uint32_t _a, std::uint32_t _b) { return distance(_a) < distance(_b); });
  if (lit.size() > MAX_LIT_BLASTS)
  {
    lit.resize(MAX_LIT_BLASTS);
  }
  return lit;
}

[[nodiscard]] float FlashShare(const Blast& _blast) noexcept
{
  return std::exp(-_blast.timeSeconds / FLASH_SECONDS);
}

[[nodiscard]] float ShellShare(const Blast& _blast) noexcept
{
  return std::exp(-_blast.timeSeconds / SHELL_FADE_SECONDS);
}

// The value at a lattice point of the noise: the hash of its three coordinates and the seed, in [0, 1).
[[nodiscard]] float LatticeValue(std::int32_t _x, std::int32_t _y, std::int32_t _z, std::uint32_t _seed) noexcept
{
  const std::uint32_t mixed = (static_cast<std::uint32_t>(_x) * 0x8DA6B343u) ^ (static_cast<std::uint32_t>(_y) * 0xD8163841u) ^
                              (static_cast<std::uint32_t>(_z) * 0xCB1AB31Fu) ^ _seed;
  return static_cast<float>(PcgHash(mixed) >> 8u) * (1.0f / 16777216.0f);
}

// Value noise: the lattice's values, blended across each cell with smoothstep weights, x first, then y, then z.
[[nodiscard]] float ValueNoise(Float3 _point, std::uint32_t _seed) noexcept
{
  const float floorX = std::floor(_point.x);
  const float floorY = std::floor(_point.y);
  const float floorZ = std::floor(_point.z);
  const auto x = static_cast<std::int32_t>(floorX);
  const auto y = static_cast<std::int32_t>(floorY);
  const auto z = static_cast<std::int32_t>(floorZ);
  const float tx = _point.x - floorX;
  const float ty = _point.y - floorY;
  const float tz = _point.z - floorZ;
  const float ux = tx * tx * (3.0f - 2.0f * tx);
  const float uy = ty * ty * (3.0f - 2.0f * ty);
  const float uz = tz * tz * (3.0f - 2.0f * tz);
  const auto along = [&](std::int32_t _dy, std::int32_t _dz)
  {
    const float low = LatticeValue(x, y + _dy, z + _dz, _seed);
    const float high = LatticeValue(x + 1, y + _dy, z + _dz, _seed);
    return low + (high - low) * ux;
  };
  const float near0 = along(0, 0);
  const float near1 = along(1, 0);
  const float far0 = along(0, 1);
  const float far1 = along(1, 1);
  const float nearer = near0 + (near1 - near0) * uy;
  const float farther = far0 + (far1 - far0) * uy;
  return nearer + (farther - nearer) * uz;
}

} // namespace

HeatParameters DefaultHeatParameters(float _extent) noexcept
{
  return {.heatDistance = std::max(HEAT_DISTANCE_FRACTION * _extent, MIN_HEAT_DISTANCE), .coolingRate = COOLING_RATE};
}

Float3 BlastCenter(const Blast& _blast) noexcept
{
  return _blast.origin + _blast.drift * MotionMade(_blast.drag, _blast.timeSeconds);
}

BlastLighting MakeBlastLighting(std::span<const Blast> _blasts, Float3 _viewPosition)
{
  BlastLighting lighting{};
  lighting.heatColors = HeatColors();
  lighting.heatGain = HEAT_GAIN;
  for (const std::uint32_t index : NearestLit(_blasts, _viewPosition, FlashShare))
  {
    const Blast& blast = _blasts[index];
    const float share = FlashShare(blast);
    const float softness = FLASH_SOFTNESS_FRACTION * blast.extent;
    lighting.flashes[lighting.flashCount++] = {
      BlastCenter(blast), softness * softness,
      RampColor(share, lighting.heatColors) * (FLASH_IRRADIANCE * blast.extent * blast.extent * share), 0.0f};
  }
  return lighting;
}

GasShells MakeGasShells(std::span<const Blast> _blasts, Float3 _viewPosition)
{
  GasShells shells{};
  for (const std::uint32_t index : NearestLit(_blasts, _viewPosition, ShellShare))
  {
    const Blast& blast = _blasts[index];
    const float share = ShellShare(blast);
    const float radius =
      std::max(SHELL_REACH * blast.extent * (1.0f - std::exp(-blast.timeSeconds / SHELL_GROWTH_SECONDS)), SHELL_SMALLEST_RADIUS);
    const float thickness = SHELL_THICKNESS_FRACTION * radius + 1.0f;
    // A path straight through the peak integrates the density to thickness × √π, so the emission is divided by it.
    const Float3 emission = RampColor(share, HeatColors()) * (SHELL_GAIN * share / (thickness * SQRT_PI));
    shells.shells[shells.count++] = {BlastCenter(blast), radius, emission, thickness, blast.seed, 0u, 0u, 0u};
  }
  return shells;
}

float FragmentHeat(const Fragment& _shape, const PlacementHeat& _heat) noexcept
{
  if (_heat.timeSeconds <= 0.0f || _heat.heatDistance <= 0.0f)
  {
    return 0.0f;
  }
  // The fragment heats when the blast reaches its pivot, and cools from then on.
  const float distance = Length(_shape.pivot - _heat.blastOrigin);
  const float since = _heat.timeSeconds - distance / _heat.shockSpeed;
  if (since <= 0.0f)
  {
    return 0.0f;
  }
  const float ratio = distance / _heat.heatDistance;
  return 1.0f / (1.0f + ratio * ratio) * std::exp(-_heat.coolingRate * _shape.sizeScale * since);
}

Float3 HeatColor(float _heat, const BlastLighting& _lighting) noexcept
{
  return RampColor(_heat, _lighting.heatColors);
}

Float3 HeatRadiance(float _heat, const BlastLighting& _lighting) noexcept
{
  if (_heat <= 0.0f)
  {
    return {0.0f, 0.0f, 0.0f};
  }
  return HeatColor(_heat, _lighting) * (_lighting.heatGain * _heat * _heat);
}

Float3 FlashLight(Float3 _position, Float3 _normal, Float3 _albedo, const BlastLighting& _lighting) noexcept
{
  Float3 light{0.0f, 0.0f, 0.0f};
  for (std::uint32_t i = 0; i < _lighting.flashCount; ++i)
  {
    const Flash& flash = _lighting.flashes[i];
    const Float3 toFlash = flash.position - _position;
    const float squared = Dot(toFlash, toFlash) + flash.softness;
    const float facing = std::max(Dot(_normal, toFlash), 0.0f) / std::sqrt(squared);
    light = light + flash.intensity * (facing / squared);
  }
  return _albedo * light;
}

Float3 BlastPixel(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY, std::uint32_t _voxel, Float3 _normal,
                  float _depth, Float3 _albedo, float _heat, const BlastLighting& _lighting) noexcept
{
  if (_voxel == NO_VOXEL)
  {
    return {0.0f, 0.0f, 0.0f};
  }
  // The ray's direction has a view depth of one, so the view depth n / depth is its parameter, as LightPixel has it.
  const Ray ray = PerspectiveRay(_view, _pixelX, _pixelY);
  const Float3 position = ray.origin + ray.direction * (_view.nearPlane / _depth);
  return HeatRadiance(_heat, _lighting) + FlashLight(position, _normal, _albedo, _lighting);
}

float ShellNoise(Float3 _point, std::uint32_t _seed) noexcept
{
  const float coarse = ValueNoise(_point, _seed);
  const float fine = ValueNoise(_point * 2.0f + Float3{17.0f, 17.0f, 17.0f}, _seed ^ 0x5BD1E995u);
  return 0.25f + 1.5f * (coarse * (2.0f / 3.0f) + fine * (1.0f / 3.0f));
}

Float3 ShellRadiance(const Ray& _ray, float _nearest, float _farthest, const GasShell& _shell) noexcept
{
  // Where the ray meets the sphere beyond which the density is none, from the ray's point nearest the center, which
  // does not cancel as b² - ac does for a small sphere far off (Haines et al., Ray Tracing Gems, ch. 7). The direction
  // is not a unit vector.
  const Float3 fromCenter = _ray.origin - _shell.center;
  const float outer = _shell.radius + SHELL_REACH_THICKNESSES * _shell.thickness;
  const float a = Dot(_ray.direction, _ray.direction);
  const float nearestParameter = -Dot(fromCenter, _ray.direction) / a;
  const Float3 closest = fromCenter + _ray.direction * nearestParameter;
  const float inside = outer * outer - Dot(closest, closest);
  if (inside <= 0.0f)
  {
    return {0.0f, 0.0f, 0.0f};
  }
  const float halfChord = std::sqrt(inside / a);
  const float enter = std::max(nearestParameter - halfChord, _nearest);
  const float leave = std::min(nearestParameter + halfChord, _farthest);
  if (leave <= enter)
  {
    return {0.0f, 0.0f, 0.0f};
  }
  const float step = (leave - enter) / static_cast<float>(SHELL_STEPS);
  const float cells = SHELL_NOISE_CELLS / _shell.radius;
  float density = 0.0f;
  for (std::uint32_t i = 0; i < SHELL_STEPS; ++i)
  {
    const Float3 offset = fromCenter + _ray.direction * (enter + (static_cast<float>(i) + 0.5f) * step);
    const float fromPeak = (Length(offset) - _shell.radius) / _shell.thickness;
    density += std::exp(-fromPeak * fromPeak) * ShellNoise(offset * cells, _shell.seed);
  }
  return _shell.emission * (density * step * std::sqrt(a));
}

Float3 GasShellPixel(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY, float _depth,
                     const GasShells& _shells) noexcept
{
  const Ray ray = PerspectiveRay(_view, _pixelX, _pixelY);
  const float farthest = IsFarPerspectiveDepth(_depth) ? ENDLESS : _view.nearPlane / _depth;
  Float3 radiance{0.0f, 0.0f, 0.0f};
  for (std::uint32_t i = 0; i < _shells.count; ++i)
  {
    radiance = radiance + ShellRadiance(ray, _view.nearPlane, farthest, _shells.shells[i]);
  }
  return radiance;
}

} // namespace NeuronCore
