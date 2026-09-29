#pragma once

// The HLSL mirror of NeuronCore/Fragmentation.h's Fragment (R16, Design/ADR/ADR-024): one fragment in the scene's structured
// buffer of fragments, 16 bytes. The C++ struct is the truth, and the layout echo in NeuronClientTests proves that the two
// agree.
struct Fragment
{
  float3 pivot;    // the mean of its voxels' centers, in its part's space: what it turns about
  float sizeScale; // its voxel count to the power -1/3: 1 for a lone voxel, smaller for a larger fragment
};
