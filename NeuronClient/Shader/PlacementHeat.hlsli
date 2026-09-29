#pragma once

// The HLSL mirror of NeuronCore/Blast.h's PlacementHeat (R16, Design/ADR/ADR-025): one placement's heat in the lighting
// pass's structured buffer, parallel to the placements. The C++ struct is the truth, and the layout echo in
// NeuronClientTests proves that the two agree.
struct PlacementHeat
{
  float3 blastOrigin; // in the part's space
  float timeSeconds;  // 0 for a whole placement, which heats nothing
  float shockSpeed;
  float heatDistance;
  float coolingRate;
  uint firstFragment;
};
