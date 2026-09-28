#pragma once

// The HLSL mirror of NeuronClient/ExplosionConstants.h (R16, Design/Archive/SampleRenderer.md §7.4, Design/SpaceScene.md
// §5.5). The C++ struct is the truth, and the layout echo in NeuronClientTests proves that the two agree.
struct ExplosionConstants
{
  float3 blastOrigin;
  float timeSeconds; // since the detonation
  float launchSpeed;
  float falloffDistance;
  float directionJitter;
  float speedJitter;
  float drag;
  uint maxQuarterTurns;
};
