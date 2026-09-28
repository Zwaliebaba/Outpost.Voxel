#pragma once

// The HLSL mirror of NeuronClient/ExplosionConstants.h (R16, Design/Archive/SampleRenderer.md §7.4, Design/SpaceScene.md
// §5.5, §7.7). The C++ struct is the truth, and the layout echo in NeuronClientTests proves that the two agree.
struct ExplosionConstants
{
  float3 blastOrigin;
  float timeSeconds; // since the detonation; at 0 or before, every voxel is at rest
  float3 inheritedVelocity;
  float launchSpeed;
  float falloffDistance;
  float directionJitter;
  float speedJitter;
  float drag;
  uint maxQuarterTurns;
  uint seed;
  uint hashBase; // the model-local index of the placement's first record, from which its voxels' hashes count
};
