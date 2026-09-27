#pragma once

// The HLSL mirror of NeuronClient/ExplosionConstants.h (R16, Design/SampleRenderer.md §7.4, §12). The C++ struct is the
// truth, and the layout echo in NeuronClientTests proves that the two agree.
struct ExplosionConstants
{
  float3 blastOrigin;
  float timeSeconds; // since the detonation
  float gravity;
  float launchSpeed;
  float falloffDistance;
  float upwardBias;
  float directionJitter;
  float speedJitter;
  float restitution;
  float horizontalDamping;
  uint maxQuarterTurns;
};
