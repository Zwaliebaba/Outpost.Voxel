#pragma once

// What the debug view pass shows in place of the lit image (Design/SampleRenderer.md §11). The C++ twins are in
// NeuronCore/DebugView.h (R15), and these values are NeuronCore::DebugView's.

#include "Hash.hlsli"
#include "Packing.hlsli"

static const uint DEBUG_VIEW_ALBEDO = 0;
static const uint DEBUG_VIEW_NORMAL = 1;
static const uint DEBUG_VIEW_VOXEL_INDEX = 2;
static const uint DEBUG_VIEW_SHADOW_MAP = 3;

// A linear color for a pixel of the visibility buffer in the views that show it: albedo, normal and voxel index. A pixel
// no voxel covers is black.
float3 DebugViewColor(uint _view, uint _voxel, float3 _normal, float3 _albedo)
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
  return float3(0.0, 0.0, 0.0);
}

// The shadow-map view shows the map as a square as tall as the view's shorter side, in the middle of the view. This finds
// the texel under a pixel, in integers as the twin finds it, and returns false outside the square.
bool ShadowMapViewTexel(uint2 _pixel, uint _widthPixels, uint _heightPixels, uint _mapWidthPixels, uint _mapHeightPixels, out uint2 _texel)
{
  _texel = uint2(0u, 0u);
  uint side = min(_widthPixels, _heightPixels);
  uint left = (_widthPixels - side) / 2u;
  uint top = (_heightPixels - side) / 2u;
  if (_pixel.x < left || _pixel.y < top || _pixel.x >= left + side || _pixel.y >= top + side)
  {
    return false;
  }
  // The texel under the pixel's centre, (pixel + ½) × map / side, doubled so that it stays in integers.
  _texel =
    uint2(((_pixel.x - left) * 2u + 1u) * _mapWidthPixels / (side * 2u), ((_pixel.y - top) * 2u + 1u) * _mapHeightPixels / (side * 2u));
  return true;
}

// The gray the shadow-map view shows for a depth: the depth itself, black at the sun's near plane and white at the far
// one, which is where the map holds nothing.
float3 ShadowMapViewColor(float _depth)
{
  return float3(_depth, _depth, _depth);
}
