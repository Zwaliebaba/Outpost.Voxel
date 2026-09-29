#pragma once

// The HLSL mirror of NeuronClient/SkyConstants.h (R16, Design/Archive/SpaceScene.md §11.5). The C++ struct is the truth, and the
// layout echo in NeuronClientTests proves that the two agree.
struct SkyConstants
{
  float3 toSun;
  float sunAngularRadiusRadians;
  float3 sunRadiance; // at the middle of the disc
  uint seed;          // the galaxy's noise's
  float3 galaxyX;     // the galaxy's axes in the world: its core, its north pole, and the third
  float galaxyGain;
  float3 galaxyY;
  float starGain;
  float3 galaxyZ;
};
