#pragma once

// The sky's functions (Design/SpaceScene.md §11): the galaxy and the sun as functions of a direction, and a star's
// point-spread function over a pixel. The C++ twins are in NeuronCore/Sky.h (R15), and hold the same constants.

#include "Hash.hlsli"
#include "PerspectiveView.hlsli"
#include "SkyConstants.hlsli"

// §11.4 and §11.2.
static const float SUN_LIMB_DARKENING = 0.6;
static const float STAR_SIGMA_PIXELS = 0.7;
static const float STAR_DARKEST_VISIBLE = 0.004;

// §11.3's galaxy, in its own frame, where y is the sine of the latitude and x the cosine of the angle from the core.
static const float DISK_THICKNESS_CORE = 0.10;
static const float DISK_THICKNESS_ANTICENTER = 0.05;
static const float DISK_ANTICENTER = 0.35;
static const float3 DISK_COLOR = float3(1.0, 0.97, 0.92);
static const float BULGE_BRIGHTNESS = 1.0;
static const float BULGE_SHARPNESS = 60.0;
static const float BULGE_FLATTENING = 2.0;
static const float3 BULGE_COLOR = float3(1.0, 0.85, 0.66);
static const float NOISE_LATITUDE = 0.6;
static const float NOISE_FADE = 0.2;
static const float CLOUD_CONTRAST = 1.0;
static const float CLOUD_STRETCH = 2.5;
static const float CLOUD_FREQUENCY = 12.0;
static const uint CLOUD_OCTAVES = 5u;
static const float DUST_DEPTH = 1.5;
static const float DUST_THICKNESS = 0.03;
static const float DUST_OFFSET = 0.01;
static const float DUST_ANTICENTER = 0.4;
static const float DUST_STRETCH = 7.0;
static const float DUST_FREQUENCY = 10.0;
static const uint DUST_OCTAVES = 5u;
static const float DUST_THRESHOLD = 0.45;
static const float DUST_SOFTNESS = 0.25;
static const uint DUST_SEED = 0x9E3779B9u;
static const float3 DUST_REDDENING = float3(1.0, 1.35, 1.8);
static const float3 OCTAVE_SHIFT = float3(0.37, 0.61, 0.83);

// 1 / (σ √2) for the star's point-spread function, in pixels; and the least view depth a projected star may have.
static const float STAR_EDGE_SCALE = 1.0101525;
static const float STAR_LEAST_DEPTH = 0.01;

// The value at a lattice point: 24 bits of PcgHash chained over its coordinates and the seed, from 0 to 1.
float LatticeValue(uint3 _cell, uint _seed)
{
  return float(PcgHash(_cell.x + PcgHash(_cell.y + PcgHash(_cell.z + _seed))) >> 8u) * (1.0 / 16777216.0);
}

// Value noise at _point: the lattice's values blended trilinearly, with smoothstep's weights, from 0 to 1.
float ValueNoise(float3 _point, uint _seed)
{
  float3 cell = floor(_point);
  float3 inside = _point - cell;
  float3 weight = inside * inside * (float3(3.0, 3.0, 3.0) - inside * 2.0);
  uint3 lattice = uint3(int3(cell));
  float near00 = lerp(LatticeValue(lattice, _seed), LatticeValue(lattice + uint3(1u, 0u, 0u), _seed), weight.x);
  float near10 = lerp(LatticeValue(lattice + uint3(0u, 1u, 0u), _seed), LatticeValue(lattice + uint3(1u, 1u, 0u), _seed), weight.x);
  float far00 = lerp(LatticeValue(lattice + uint3(0u, 0u, 1u), _seed), LatticeValue(lattice + uint3(1u, 0u, 1u), _seed), weight.x);
  float far10 = lerp(LatticeValue(lattice + uint3(0u, 1u, 1u), _seed), LatticeValue(lattice + uint3(1u, 1u, 1u), _seed), weight.x);
  return lerp(lerp(near00, near10, weight.y), lerp(far00, far10, weight.y), weight.z);
}

// Octaves of value noise at _point, at doubling frequencies and halving weights, each with a seed of its own: from 0
// to 1.
float Fbm(float3 _point, uint _seed, uint _octaves)
{
  float3 at = _point;
  float sum = 0.0;
  float total = 0.0;
  float weight = 0.5;
  for (uint octave = 0u; octave < _octaves; ++octave)
  {
    sum += weight * ValueNoise(at, _seed + octave);
    total += weight;
    weight *= 0.5;
    at = at * 2.0 + OCTAVE_SHIFT;
  }
  return sum / total;
}

// §11.3: the galaxy's radiance toward _direction, a unit vector in the galaxy's frame, before the gain.
float3 GalaxyRadiance(float3 _direction, uint _seed)
{
  // From 1 toward the core to 0 toward the anticentre.
  float core = 0.5 + 0.5 * _direction.x;
  float latitude = abs(_direction.y);
  float disk = lerp(DISK_ANTICENTER, 1.0, core) * exp(-latitude / lerp(DISK_THICKNESS_ANTICENTER, DISK_THICKNESS_CORE, core));
  float bulge = BULGE_BRIGHTNESS * exp(-BULGE_SHARPNESS * ((1.0 - _direction.x) + BULGE_FLATTENING * _direction.y * _direction.y));
  float depth = 0.0;
  float noise = saturate((NOISE_LATITUDE - latitude) / NOISE_FADE);
  [branch] if (noise > 0.0)
  {
    float3 cloudPoint = float3(_direction.x, _direction.y * CLOUD_STRETCH, _direction.z) * CLOUD_FREQUENCY;
    float3 dustPoint = float3(_direction.x, _direction.y * DUST_STRETCH, _direction.z) * DUST_FREQUENCY;
    disk *= exp(noise * CLOUD_CONTRAST * (2.0 * Fbm(cloudPoint, _seed, CLOUD_OCTAVES) - 1.0));
    float clumps = saturate((Fbm(dustPoint, _seed + DUST_SEED, DUST_OCTAVES) - DUST_THRESHOLD) / DUST_SOFTNESS);
    depth = noise * DUST_DEPTH * lerp(DUST_ANTICENTER, 1.0, core) * exp(-abs(_direction.y - DUST_OFFSET) / DUST_THICKNESS) * clumps;
  }
  float3 transmission = exp(-depth * DUST_REDDENING);
  return (DISK_COLOR * disk + BULGE_COLOR * bulge) * transmission;
}

// §11.4: the sun's radiance toward _direction, a unit vector in the world, with its edge spread over _pixelRadians.
float3 SunRadiance(float3 _direction, SkyConstants _sky, float _pixelRadians)
{
  // The angle from the disc's middle, through the chord between the two unit vectors.
  float angle = 2.0 * asin(min(0.5 * length(_direction - _sky.toSun), 1.0));
  float coverage = saturate((_sky.sunAngularRadiusRadians - angle) / _pixelRadians + 0.5);
  float across = min(angle / _sky.sunAngularRadiusRadians, 1.0);
  float mu = sqrt(1.0 - across * across);
  return _sky.sunRadiance * ((1.0 - SUN_LIMB_DARKENING * (1.0 - mu)) * coverage);
}

// The angle a pixel spans at the middle of _view, over which the sun's edge is spread.
float PixelRadians(ViewConstants _view)
{
  return 2.0 * _view.tanHalfFovY / float(_view.heightPixels);
}

// What the sky's triangle writes where no voxel is (§11.5): the galaxy, then the sun, toward the ray through the pixel's
// centre. _pixelCenter is SV_Position.xy.
float3 SkyPixel(ViewConstants _view, float2 _pixelCenter, SkyConstants _sky)
{
  float3 direction = normalize(PerspectiveRay(_view, _pixelCenter).direction);
  float3 galactic = float3(dot(_sky.galaxyX, direction), dot(_sky.galaxyY, direction), dot(_sky.galaxyZ, direction));
  return GalaxyRadiance(galactic, _sky.seed) * _sky.galaxyGain + SunRadiance(direction, _sky, PixelRadians(_view));
}

// §11.2: the complementary error function at _x >= 0, by Abramowitz and Stegun's 7.1.26.
float Erfc(float _x)
{
  float t = 1.0 / (1.0 + 0.3275911 * _x);
  float polynomial = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 + t * (-1.453152027 + t * 1.061405429))));
  return polynomial * exp(-_x * _x);
}

// The share of a star's light that falls on a pixel along one axis, the pixel's centre _offsetPixels from the star's:
// written from the tails wherever both of the pixel's edges lie on one side of the star.
float StarAxisShare(float _offsetPixels)
{
  float lower = (_offsetPixels - 0.5) * STAR_EDGE_SCALE;
  float upper = (_offsetPixels + 0.5) * STAR_EDGE_SCALE;
  if (lower >= 0.0)
  {
    return 0.5 * (Erfc(lower) - Erfc(upper));
  }
  if (upper <= 0.0)
  {
    return 0.5 * (Erfc(-upper) - Erfc(-lower));
  }
  return 1.0 - 0.5 * (Erfc(-lower) + Erfc(upper));
}

// The share over the pixel's square.
float StarShare(float2 _offsetPixels)
{
  return StarAxisShare(_offsetPixels.x) * StarAxisShare(_offsetPixels.y);
}

// Where a star toward _direction lies in _view's image, in pixels with y down, into _position; false for a star behind
// the camera or beside it.
bool StarPosition(ViewConstants _view, float3 _direction, out float2 _position)
{
  _position = float2(0.0, 0.0);
  float depth = dot(_direction, _view.forward);
  if (depth < STAR_LEAST_DEPTH)
  {
    return false;
  }
  float ndcX = dot(_direction, _view.right) / (depth * _view.aspect * _view.tanHalfFovY);
  float ndcY = dot(_direction, _view.up) / (depth * _view.tanHalfFovY);
  _position = float2((ndcX + 1.0) * 0.5 * float(_view.widthPixels), (1.0 - ndcY) * 0.5 * float(_view.heightPixels));
  return true;
}

// Half the side of a star's quad for _brightness, its flux times the gain; 0 for a star that is not drawn.
float StarQuadRadius(float _brightness)
{
  float peak = StarAxisShare(0.0);
  if (_brightness * peak * peak < STAR_DARKEST_VISIBLE)
  {
    return 0.0;
  }
  return 0.5 + STAR_SIGMA_PIXELS * sqrt(2.0 * log(_brightness / STAR_DARKEST_VISIBLE));
}
