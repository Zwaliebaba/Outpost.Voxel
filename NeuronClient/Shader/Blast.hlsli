#pragma once

// A detonation's hot debris and flash in the lighting pass (Design/ADR/ADR-025). The C++ twins are in NeuronCore/Blast.h
// (R15), function for function.

#include "BlastLighting.hlsli"
#include "Fragment.hlsli"
#include "Packing.hlsli"
#include "PerspectiveView.hlsli"
#include "PlacementHeat.hlsli"

// The heat of fragment _shape of a placement heating as _heat has it, from 0 to 1: cold until the blast reaches its pivot,
// then 1 / (1 + (d / heatDistance)^2), cooling as e^(-coolingRate x size scale x t).
float FragmentHeat(Fragment _shape, PlacementHeat _heat)
{
  if (_heat.timeSeconds <= 0.0 || _heat.heatDistance <= 0.0)
  {
    return 0.0;
  }
  float distance = length(_shape.pivot - _heat.blastOrigin);
  float since = _heat.timeSeconds - distance / _heat.shockSpeed;
  if (since <= 0.0)
  {
    return 0.0;
  }
  float ratio = distance / _heat.heatDistance;
  return 1.0 / (1.0 + ratio * ratio) * exp(-_heat.coolingRate * _shape.sizeScale * since);
}

// The ramp's color at _heat, between its two nearest steps.
float3 HeatColor(float _heat, BlastLighting _lighting)
{
  float along = clamp(_heat, 0.0, 1.0) * float(HEAT_COLOR_STEPS - 1u);
  uint lowerStep = min(uint(floor(along)), HEAT_COLOR_STEPS - 2u);
  float share = along - float(lowerStep);
  float3 lower = _lighting.heatColors[lowerStep].xyz;
  float3 upper = _lighting.heatColors[lowerStep + 1u].xyz;
  return lower + (upper - lower) * share;
}

// What a surface at _heat gives off: the ramp's color times the gain times the heat squared.
float3 HeatRadiance(float _heat, BlastLighting _lighting)
{
  if (_heat <= 0.0)
  {
    return float3(0.0, 0.0, 0.0);
  }
  return HeatColor(_heat, _lighting) * (_lighting.heatGain * _heat * _heat);
}

// What the flashes give a surface at _position with _normal and _albedo: each a point light without a shadow.
float3 FlashLight(float3 _position, float3 _normal, float3 _albedo, BlastLighting _lighting)
{
  float3 light = float3(0.0, 0.0, 0.0);
  for (uint i = 0u; i < _lighting.flashCount; ++i)
  {
    Flash flash = _lighting.flashes[i];
    float3 toFlash = flash.position - _position;
    float squared = dot(toFlash, toFlash) + flash.softness;
    float facing = max(dot(_normal, toFlash), 0.0) / sqrt(squared);
    light = light + flash.intensity * (facing / squared);
  }
  return _albedo * light;
}

// What the lighting pass adds to what LightPixel gives: the voxel's glow at _heat and the flashes' light on it.
float3 BlastPixel(ViewConstants _view, float2 _pixelCenter, uint _voxel, float3 _normal, float _depth, float3 _albedo, float _heat,
                  BlastLighting _lighting)
{
  if (_voxel == NO_VOXEL)
  {
    return float3(0.0, 0.0, 0.0);
  }
  Ray ray = PerspectiveRay(_view, _pixelCenter);
  float3 position = ray.origin + ray.direction * (_view.nearPlane / _depth);
  return HeatRadiance(_heat, _lighting) + FlashLight(position, _normal, _albedo, _lighting);
}
