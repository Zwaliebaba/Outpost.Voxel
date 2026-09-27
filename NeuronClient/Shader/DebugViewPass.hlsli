#pragma once

// The debug view pass (Design/SampleRenderer.md §11): one triangle over the viewport, and for each pixel the color
// DebugViewColor gives its entry in the visibility buffer.

#include "DebugView.hlsli"
#include "Packing.hlsli"
#include "PaletteConstants.hlsli"
#include "ViewConstants.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<PaletteConstants> g_palette : register(b1);
StructuredBuffer<uint> g_records : register(t0);
Texture2D<uint2> g_visibility : register(t1);

// One 32-bit root constant: the DEBUG_VIEW_* value to show.
cbuffer DebugViewSelection : register(b2)
{
  uint g_debugView;
};

struct FullScreenVaryings
{
  float4 position : SV_Position;
};

struct DebugViewTarget
{
  float4 color : SV_Target0;
};

// Vertices 0, 1 and 2 at (-1, -1), (3, -1) and (-1, 3) cover the viewport with one triangle.
FullScreenVaryings FullScreenVertex(uint _vertex : SV_VertexID)
{
  FullScreenVaryings varyings;
  varyings.position = float4(_vertex == 1u ? 3.0 : -1.0, _vertex == 2u ? 3.0 : -1.0, 0.0, 1.0);
  return varyings;
}

DebugViewTarget DebugViewPixel(FullScreenVaryings _varyings)
{
  uint2 visibility = g_visibility.Load(int3(int2(_varyings.position.xy), 0));
  float3 albedo = float3(0.0, 0.0, 0.0);
  if (visibility.x != NO_VOXEL)
  {
    albedo = g_palette.materials[UnpackVoxelRecord(g_records[visibility.x]).color].albedo;
  }
  DebugViewTarget target;
  target.color = float4(DebugViewColor(g_debugView, visibility.x, UnpackOctahedralNormal(visibility.y), albedo, g_view.forward), 1.0);
  return target;
}
