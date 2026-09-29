#pragma once

// The HLSL mirror of NeuronClient/ExplosionConstants.h (R16, Design/Archive/SampleRenderer.md §7.4, Design/Archive/SpaceScene.md
// §5.5, §7.7, Design/ADR/ADR-024). The C++ struct is the truth, and the layout echo in NeuronClientTests proves that the two
// agree.
struct ExplosionConstants
{
  float3 blastOrigin;
  float timeSeconds; // since the detonation; at 0 or before, every voxel is at rest
  float3 inheritedVelocity;
  float launchSpeed;
  float falloffDistance;
  float directionJitter;
  float speedSpread;
  float drag;
  float minDrag;
  float maxSpinRadians;
  float shockSpeed;
  uint seed;
  uint firstFragment; // where the placement's model's first fragment lies in the scene's fragment buffer
};
