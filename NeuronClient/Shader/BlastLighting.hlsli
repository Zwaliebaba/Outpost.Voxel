#pragma once

// The HLSL mirror of NeuronCore/Blast.h's BlastLighting and Flash (R16, Design/ADR/ADR-025): what the lighting pass knows
// of the frame's detonations. The C++ structs are the truth, and the layout echo in NeuronClientTests proves that the
// two agree.

// NeuronCore's MAX_LIT_BLASTS and HEAT_COLOR_STEPS.
static const uint MAX_LIT_BLASTS = 8;
static const uint HEAT_COLOR_STEPS = 16;

struct Flash
{
  float3 position;
  float softness; // the square of the distance within which the light no longer grows
  float3 intensity;
  float padding;
};

struct BlastLighting
{
  float4 heatColors[HEAT_COLOR_STEPS]; // the ramp: a unit-luminance color in xyz, w unused
  Flash flashes[MAX_LIT_BLASTS];
  uint flashCount;
  float heatGain;
  float padding0;
  float padding1;
};
