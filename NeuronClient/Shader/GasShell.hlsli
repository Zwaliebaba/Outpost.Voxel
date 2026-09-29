#pragma once

// A detonation's shell of hot gas (Design/ADR/ADR-025): emission only, added over the lit image, so that shells need no
// order. The C++ twins are in NeuronCore/Blast.h (R15), function for function.

#include "GasShells.hlsli"
#include "Hash.hlsli"
#include "PerspectiveView.hlsli"

// NeuronCore's SHELL_STEPS, and the density's reach in thicknesses beyond the peak.
static const uint SHELL_STEPS = 24;
static const float SHELL_REACH_THICKNESSES = 3.0;
static const float SHELL_NOISE_CELLS = 5.0;

// A ray that meets no voxel goes on without end.
static const float ENDLESS = 3.402823466e+38;

// The value at a lattice point of the noise: the hash of its three coordinates and the seed, in [0, 1).
float LatticeValue(int _x, int _y, int _z, uint _seed)
{
  uint mixed = (asuint(_x) * 0x8DA6B343u) ^ (asuint(_y) * 0xD8163841u) ^ (asuint(_z) * 0xCB1AB31Fu) ^ _seed;
  return float(PcgHash(mixed) >> 8u) * (1.0 / 16777216.0);
}

// Value noise: the lattice's values, blended across each cell with smoothstep weights, x first, then y, then z.
float ValueNoise(float3 _point, uint _seed)
{
  float3 floored = floor(_point);
  int x = int(floored.x);
  int y = int(floored.y);
  int z = int(floored.z);
  float3 t = _point - floored;
  float ux = t.x * t.x * (3.0 - 2.0 * t.x);
  float uy = t.y * t.y * (3.0 - 2.0 * t.y);
  float uz = t.z * t.z * (3.0 - 2.0 * t.z);
  float low = LatticeValue(x, y, z, _seed);
  float near0 = low + (LatticeValue(x + 1, y, z, _seed) - low) * ux;
  low = LatticeValue(x, y + 1, z, _seed);
  float near1 = low + (LatticeValue(x + 1, y + 1, z, _seed) - low) * ux;
  low = LatticeValue(x, y, z + 1, _seed);
  float far0 = low + (LatticeValue(x + 1, y, z + 1, _seed) - low) * ux;
  low = LatticeValue(x, y + 1, z + 1, _seed);
  float far1 = low + (LatticeValue(x + 1, y + 1, z + 1, _seed) - low) * ux;
  float nearer = near0 + (near1 - near0) * uy;
  float farther = far0 + (far1 - far0) * uy;
  return nearer + (farther - nearer) * uz;
}

// Two octaves of value noise, with a mean near 1.
float ShellNoise(float3 _point, uint _seed)
{
  float coarse = ValueNoise(_point, _seed);
  float fine = ValueNoise(_point * 2.0 + float3(17.0, 17.0, 17.0), _seed ^ 0x5BD1E995u);
  return 0.25 + 1.5 * (coarse * (2.0 / 3.0) + fine * (1.0 / 3.0));
}

// What _shell gives off along _ray from _nearest to _farthest: SHELL_STEPS samples at the middles of equal steps through
// where the ray meets the shell's sphere.
float3 ShellRadiance(Ray _ray, float _nearest, float _farthest, GasShell _shell)
{
  // From the ray's point nearest the center, which does not cancel as b^2 - ac does for a small sphere far off.
  float3 fromCenter = _ray.origin - _shell.center;
  float outer = _shell.radius + SHELL_REACH_THICKNESSES * _shell.thickness;
  float a = dot(_ray.direction, _ray.direction);
  float nearestParameter = -dot(fromCenter, _ray.direction) / a;
  float3 closest = fromCenter + _ray.direction * nearestParameter;
  float inside = outer * outer - dot(closest, closest);
  if (inside <= 0.0)
  {
    return float3(0.0, 0.0, 0.0);
  }
  float halfChord = sqrt(inside / a);
  float enter = max(nearestParameter - halfChord, _nearest);
  float leave = min(nearestParameter + halfChord, _farthest);
  if (leave <= enter)
  {
    return float3(0.0, 0.0, 0.0);
  }
  float stepLength = (leave - enter) / float(SHELL_STEPS);
  float cells = SHELL_NOISE_CELLS / _shell.radius;
  float density = 0.0;
  for (uint i = 0u; i < SHELL_STEPS; ++i)
  {
    float3 offset = fromCenter + _ray.direction * (enter + (float(i) + 0.5) * stepLength);
    float fromPeak = (length(offset) - _shell.radius) / _shell.thickness;
    density += exp(-fromPeak * fromPeak) * ShellNoise(offset * cells, _shell.seed);
  }
  return _shell.emission * (density * stepLength * sqrt(a));
}

// What the gas shell pass adds to the pixel whose centre is _pixelCenter: every shell, up to the voxel there or without end.
float3 GasShellPixel(ViewConstants _view, float2 _pixelCenter, float _depth, GasShells _shells)
{
  Ray ray = PerspectiveRay(_view, _pixelCenter);
  float farthest = _depth == PERSPECTIVE_FAR_DEPTH ? ENDLESS : _view.nearPlane / _depth;
  float3 radiance = float3(0.0, 0.0, 0.0);
  for (uint i = 0u; i < _shells.count; ++i)
  {
    radiance = radiance + ShellRadiance(ray, _view.nearPlane, farthest, _shells.shells[i]);
  }
  return radiance;
}
