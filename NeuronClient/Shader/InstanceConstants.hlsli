#pragma once

// The HLSL mirror of NeuronClient/InstanceConstants.h (R16, Design/SampleRenderer.md §7.4). The C++ struct is the truth,
// and the layout echo in NeuronClientTests proves that the two agree.
struct InstanceConstants
{
  int3 modelOrigin; // world position of the minimum corner of voxel (0, 0, 0)
  uint firstRecord;
  uint recordCount;
};
