#pragma once

// The splat pass (Design/SampleRenderer.md §9). SplatVertex bounds each voxel's projection with a screen-space rectangle,
// and SplatPixel intersects the pixel's ray with the voxel's box, discards a miss and writes the hit's depth, index and
// normal. The entry-point file sets ORIENTED and ORTHOGRAPHIC; the view splat is the aligned perspective permutation.

#ifndef ORIENTED
#   error "the entry-point file sets ORIENTED"
#endif
#ifndef ORTHOGRAPHIC
#   error "the entry-point file sets ORTHOGRAPHIC"
#endif
#if ORIENTED
#   error "the oriented permutation lands with the explosion (M4)"
#endif
#if ORTHOGRAPHIC
#   error "the orthographic permutation lands with the shadow pass (M3)"
#endif

// Every ray the pass casts starts outside the box it tests (§9.3).
#define CAN_START_IN_BOX 0

#include "InstanceConstants.hlsli"
#include "Packing.hlsli"
#include "PerspectiveView.hlsli"
#include "Ray.hlsli"
#include "RayBox.hlsli"
#include "SplatBounds.hlsli"
#include "ViewConstants.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<InstanceConstants> g_instance : register(b1);
StructuredBuffer<uint> g_records : register(t0);

// An instance of the draw covers this many voxels: the static index buffer holds this many rectangles (§9.1). The C++
// side is NeuronClient/ViewSplatPass.h.
static const uint RECTANGLES_PER_INSTANCE = 256;

// A pixel shader that writes conservative depth reads SV_Position at the centroid; without MSAA that is the pixel's centre.
struct SplatVaryings
{
  noperspective centroid float4 position : SV_Position;
  nointerpolation uint voxel : VOXEL;
};

struct SplatTargets
{
  uint2 visibility : SV_Target0;
  float depth : SV_DepthLessEqual;
};

// The box an intact voxel is drawn as. The C++ twin is VoxelBox in NeuronCore/VoxModel.h (R15).
Box VoxelBox(uint _voxel)
{
  VoxelRecord record = UnpackVoxelRecord(g_records[_voxel]);
  int3 minCorner = g_instance.modelOrigin + int3(record.x, record.y, record.z);
  return MakeAxisAlignedBox(float3(minCorner) + 0.5, float3(0.5, 0.5, 0.5));
}

// Vertex v of instance i is corner v mod 4 of voxel 256 i + v / 4 (§9.1). A voxel past the end of the model, or one the
// bounds cull, becomes a degenerate rectangle outside the viewport, which draws nothing.
SplatVaryings SplatVertex(uint _vertex : SV_VertexID, uint _instance : SV_InstanceID)
{
  SplatVaryings varyings;
  varyings.position = float4(-2.0, -2.0, 0.0, 1.0);
  varyings.voxel = NO_VOXEL;

  uint local = _instance * RECTANGLES_PER_INSTANCE + _vertex / 4u;
  if (local >= g_instance.recordCount)
  {
    return varyings;
  }
  uint voxel = g_instance.firstRecord + local;
  SplatBounds bounds = PerspectiveSplatBounds(VoxelBox(voxel), g_view);
  if (!bounds.visible)
  {
    return varyings;
  }
  uint corner = _vertex % 4u;
  float x = (corner & 1u) != 0u ? bounds.maxNdc.x : bounds.minNdc.x;
  float y = (corner & 2u) != 0u ? bounds.maxNdc.y : bounds.minNdc.y;
  varyings.position = float4(x, y, bounds.depth, 1.0);
  varyings.voxel = voxel;
  return varyings;
}

// §9.3: the ray through the pixel's centre against the voxel's box. The written depth can only move away from the
// camera, so it stays within the promise SV_DepthLessEqual makes; min() keeps a rounding error from breaking it.
SplatTargets SplatPixel(SplatVaryings _varyings)
{
  Ray ray = PerspectiveRay(g_view, _varyings.position.xy);
  float distance = 0.0;
  float3 normal = float3(0.0, 0.0, 0.0);
  bool hit = IntersectBox(VoxelBox(_varyings.voxel), ray.origin, ray.direction, InverseDirection(ray), distance, normal);
  if (!hit || distance < g_view.nearPlane)
  {
    discard;
  }
  SplatTargets targets;
  targets.visibility = uint2(_varyings.voxel, PackOctahedralNormal(normal));
  targets.depth = min(PerspectiveDepth(g_view, distance), _varyings.position.z);
  return targets;
}
