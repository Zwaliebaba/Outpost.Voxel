#pragma once

// What the debug view pass shows for one pixel (Design/SampleRenderer.md §11). The C++ twins are in
// NeuronCore/DebugView.h (R15), and these values are NeuronCore::DebugView's. Until the lighting lands in M3, the first
// view is a headlight: albedo lit from the camera, enough to read the shape.

#include "Packing.hlsli"

static const uint DEBUG_VIEW_HEADLIGHT = 0;
static const uint DEBUG_VIEW_ALBEDO = 1;
static const uint DEBUG_VIEW_NORMAL = 2;
static const uint DEBUG_VIEW_VOXEL_INDEX = 3;

// The PCG hash of Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 9(3), 2020.
uint PcgHash(uint _value)
{
  uint state = _value * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

// A linear color. _forward is the camera's view direction; a pixel no voxel covers is black in every view.
float3 DebugViewColor(uint _view, uint _voxel, float3 _normal, float3 _albedo, float3 _forward)
{
  if (_voxel == NO_VOXEL)
  {
    return float3(0.0, 0.0, 0.0);
  }
  if (_view == DEBUG_VIEW_ALBEDO)
  {
    return _albedo;
  }
  if (_view == DEBUG_VIEW_NORMAL)
  {
    return _normal * 0.5 + 0.5;
  }
  if (_view == DEBUG_VIEW_VOXEL_INDEX)
  {
    uint hash = PcgHash(_voxel);
    return float3(float(hash & 0xFFu), float((hash >> 8u) & 0xFFu), float((hash >> 16u) & 0xFFu)) / 255.0;
  }
  return _albedo * (0.25 + 0.75 * saturate(dot(_normal, -_forward)));
}
