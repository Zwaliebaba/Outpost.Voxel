#pragma once

// The splat pass (Design/SampleRenderer.md §9, §10). SplatVertex bounds each voxel's projection with a screen-space
// rectangle, and SplatPixel intersects the pixel's ray with the voxel's box, discards a miss and writes the hit's depth,
// and in the view splat its index and normal. The entry-point file sets ORIENTED and ORTHOGRAPHIC: the view splat is the
// perspective permutation, the shadow splat the orthographic one, and each draws aligned boxes while the model is intact
// and oriented ones, posed by the explosion, once it is not (§12). PLAIN_DEPTH and COUNT_OVERDRAW make the view splat's
// measurement variants (§14): one writes plain SV_Depth instead of conservative depth, so that PSInvocations shows what
// conservative depth saves, and the other counts its invocations per pixel for the overdraw view.

#ifndef ORIENTED
#   error "the entry-point file sets ORIENTED"
#endif
#ifndef ORTHOGRAPHIC
#   error "the entry-point file sets ORTHOGRAPHIC"
#endif
#ifndef PLAIN_DEPTH
#   error "the entry-point file sets PLAIN_DEPTH"
#endif
#ifndef COUNT_OVERDRAW
#   error "the entry-point file sets COUNT_OVERDRAW"
#endif
#if ORTHOGRAPHIC && (PLAIN_DEPTH || COUNT_OVERDRAW)
#   error "the measurement variants are the view splat's"
#endif
#if PLAIN_DEPTH && COUNT_OVERDRAW
#   error "a view splat is one measurement variant at a time"
#endif
// Every ray the pass casts starts outside the box it tests (§9.3).
#define CAN_START_IN_BOX 0

#include "InstanceConstants.hlsli"
#include "OrthographicView.hlsli"
#include "Packing.hlsli"
#include "PerspectiveView.hlsli"
#include "Ray.hlsli"
#include "RayBox.hlsli"
#include "ShadowViewConstants.hlsli"
#include "SplatBounds.hlsli"
#include "ViewConstants.hlsli"
#if ORIENTED
#   include "Explosion.hlsli"
#endif

#if ORTHOGRAPHIC
ConstantBuffer<ShadowViewConstants> g_view : register(b0);
#else
ConstantBuffer<ViewConstants> g_view : register(b0);
#endif
ConstantBuffer<InstanceConstants> g_instance : register(b1);
#if ORIENTED
ConstantBuffer<ExplosionConstants> g_explosion : register(b2);
#endif
StructuredBuffer<uint> g_records : register(t0);
#if COUNT_OVERDRAW
RWTexture2D<uint> g_overdraw : register(u0);
#endif

// An instance of the draw covers this many voxels: the static index buffer holds this many rectangles (§9.1). The C++
// side is NeuronClient/SplatPass.h.
static const uint RECTANGLES_PER_INSTANCE = 256;

// A pixel shader that writes conservative depth reads SV_Position at the centroid; without MSAA that is the pixel's centre.
// An oriented box travels with its voxel's pose: the center and the three axes of its rotation (§9.2, step 5).
struct SplatVaryings
{
  noperspective centroid float4 position : SV_Position;
  nointerpolation uint voxel : VOXEL;
#if ORIENTED
  nointerpolation float3 center : CENTER;
  nointerpolation float3 axisX : AXIS_X;
  nointerpolation float3 axisY : AXIS_Y;
  nointerpolation float3 axisZ : AXIS_Z;
#endif
};

#if ORTHOGRAPHIC
// The shadow splat writes depth alone, standard Z, and has no render target (§10).
struct SplatTargets
{
  float depth : SV_DepthGreaterEqual;
};
#else
struct SplatTargets
{
  uint2 visibility : SV_Target0;
#   if PLAIN_DEPTH
  float depth : SV_Depth;
#   else
  float depth : SV_DepthLessEqual;
#   endif
};
#endif

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
#if ORIENTED
  varyings.center = float3(0.0, 0.0, 0.0);
  varyings.axisX = float3(1.0, 0.0, 0.0);
  varyings.axisY = float3(0.0, 1.0, 0.0);
  varyings.axisZ = float3(0.0, 0.0, 1.0);
#endif

  uint local = _instance * RECTANGLES_PER_INSTANCE + _vertex / 4u;
  if (local >= g_instance.recordCount)
  {
    return varyings;
  }
  uint voxel = g_instance.firstRecord + local;
  Box box = VoxelBox(voxel);
#if ORIENTED
  // §12: the explosion replaces the center and supplies a rotation.
  VoxelPose pose = ExplosionPose(voxel, box.center, g_explosion);
  box = MakeOrientedBox(pose.center, box.radius, pose.axisX, pose.axisY, pose.axisZ);
#endif
#if ORTHOGRAPHIC
  SplatBounds bounds = OrthographicSplatBounds(box, g_view);
#else
  SplatBounds bounds = PerspectiveSplatBounds(box, g_view);
#endif
  if (!bounds.visible)
  {
    return varyings;
  }
  uint corner = _vertex % 4u;
  float x = (corner & 1u) != 0u ? bounds.maxNdc.x : bounds.minNdc.x;
  float y = (corner & 2u) != 0u ? bounds.maxNdc.y : bounds.minNdc.y;
  varyings.position = float4(x, y, bounds.depth, 1.0);
  varyings.voxel = voxel;
#if ORIENTED
  varyings.center = box.center;
  varyings.axisX = box.axisX;
  varyings.axisY = box.axisY;
  varyings.axisZ = box.axisZ;
#endif
  return varyings;
}

// The box the pixel's voxel is drawn as: where it lies intact, or where the vertex shader posed it.
Box SplatBox(SplatVaryings _varyings)
{
#if ORIENTED
  return MakeOrientedBox(_varyings.center, float3(0.5, 0.5, 0.5), _varyings.axisX, _varyings.axisY, _varyings.axisZ);
#else
  return VoxelBox(_varyings.voxel);
#endif
}

#if ORTHOGRAPHIC
// §10: the ray from the pixel's centre on the sun's near plane against the voxel's box. The written depth can only move
// away from the sun, which in standard Z is larger, so it stays within the promise SV_DepthGreaterEqual makes; max()
// keeps a rounding error from breaking it.
SplatTargets SplatPixel(SplatVaryings _varyings)
{
  Ray ray = OrthographicRay(g_view, _varyings.position.xy);
  float distance = 0.0;
  float3 normal = float3(0.0, 0.0, 0.0);
  bool hit = IntersectBox(SplatBox(_varyings), ray.origin, ray.direction, InverseDirection(ray), distance, normal);
  if (!hit || distance < 0.0)
  {
    discard;
  }
  SplatTargets targets;
  targets.depth = max(OrthographicDepth(g_view, distance), _varyings.position.z);
  return targets;
}
#else
// §9.3: the ray through the pixel's centre against the voxel's box. The written depth can only move away from the
// camera, so it stays within the promise SV_DepthLessEqual makes; min() keeps a rounding error from breaking it. The
// plain-depth variant writes the hit's depth as it is, and makes no promise.
SplatTargets SplatPixel(SplatVaryings _varyings)
{
#   if COUNT_OVERDRAW
  // Every invocation counts, a miss as well as a hit. A shader with a UAV side effect runs before the depth test, so this
  // counts every fragment of every rectangle over the pixel: the bounds' looseness and the depth complexity together.
  InterlockedAdd(g_overdraw[uint2(_varyings.position.xy)], 1u);
#   endif
  Ray ray = PerspectiveRay(g_view, _varyings.position.xy);
  float distance = 0.0;
  float3 normal = float3(0.0, 0.0, 0.0);
  bool hit = IntersectBox(SplatBox(_varyings), ray.origin, ray.direction, InverseDirection(ray), distance, normal);
  if (!hit || distance < g_view.nearPlane)
  {
    discard;
  }
  SplatTargets targets;
  targets.visibility = uint2(_varyings.voxel, PackOctahedralNormal(normal));
#   if PLAIN_DEPTH
  targets.depth = PerspectiveDepth(g_view, distance);
#   else
  targets.depth = min(PerspectiveDepth(g_view, distance), _varyings.position.z);
#   endif
  return targets;
}
#endif
