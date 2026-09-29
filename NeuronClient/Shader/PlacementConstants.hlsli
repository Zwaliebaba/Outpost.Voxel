#pragma once

// The HLSL mirror of NeuronClient/PlacementConstants.h (R16, Design/Archive/SpaceScene.md §7.2, §7.3): an element of the frame's
// structured buffer of placements. The C++ struct is the truth, and the layout echo in NeuronClientTests proves that the
// two agree.
struct PlacementConstants
{
  float3 axisX; // the rotation's columns: the part's axes in the world
  uint firstRecord;
  float3 axisY;
  uint recordCount;
  float3 axisZ;
  uint firstVoxel;
  float3 translation;
  uint paletteIndex;
};
