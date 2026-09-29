#pragma once

// Placements (Design/Archive/SpaceScene.md §7): how the splat takes a part's voxels into the world, and how the lighting pass and
// the debug views find the record and palette of a pixel's voxel from its id. The C++ twins are in
// NeuronCore/RigidTransform.h and NeuronCore/Placement.h (R15).

#include "PlacementConstants.hlsli"

// What FindPlacement gives for an id that no placement holds: NeuronCore's NO_PLACEMENT.
static const uint NO_PLACEMENT = 0xFFFFFFFFu;

// _vector turned by the placement's rotation: axisX x + axisY y + axisZ z, summed in that order.
float3 RotateVector(PlacementConstants _placement, float3 _vector)
{
  return _placement.axisX * _vector.x + _placement.axisY * _vector.y + _placement.axisZ * _vector.z;
}

// _point taken into the world: the translation plus the turned point.
float3 TransformPoint(PlacementConstants _placement, float3 _point)
{
  return _placement.translation + RotateVector(_placement, _point);
}

// The index of the placement that holds voxel id _voxel among the first _count of _placements, or NO_PLACEMENT: a binary
// search over their first voxels, which rise with their order (§7.3).
uint FindPlacement(StructuredBuffer<PlacementConstants> _placements, uint _count, uint _voxel)
{
  if (_count == 0u || _voxel < _placements[0].firstVoxel)
  {
    return NO_PLACEMENT;
  }
  uint lower = 0u;
  uint upper = _count;
  while (upper - lower > 1u)
  {
    uint middle = lower + (upper - lower) / 2u;
    if (_placements[middle].firstVoxel <= _voxel)
    {
      lower = middle;
    }
    else
    {
      upper = middle;
    }
  }
  return _voxel - _placements[lower].firstVoxel < _placements[lower].recordCount ? lower : NO_PLACEMENT;
}

// The record of voxel id _voxel in the scene's record buffer, and its placement's palette, through the placement
// FindPlacement finds; false for an id that no placement holds.
bool FindVoxel(StructuredBuffer<PlacementConstants> _placements, uint _count, uint _voxel, out uint _record, out uint _paletteIndex)
{
  _record = 0u;
  _paletteIndex = 0u;
  uint found = FindPlacement(_placements, _count, _voxel);
  if (found == NO_PLACEMENT)
  {
    return false;
  }
  PlacementConstants placement = _placements[found];
  _record = placement.firstRecord + (_voxel - placement.firstVoxel);
  _paletteIndex = placement.paletteIndex;
  return true;
}
