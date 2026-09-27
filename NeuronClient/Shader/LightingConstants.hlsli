#pragma once

// The HLSL mirror of NeuronClient/LightingConstants.h (R16, Design/SampleRenderer.md §7.4, §11). The C++ struct is the
// truth, and the layout echo in NeuronClientTests proves that the two agree.
struct LightingConstants
{
  float3 toSun;             // unit, from a surface towards the sun
  float shadowNormalOffset; // world units a shaded point moves along its normal before the shadow map is read
  float3 sunRadiance;
  float emissiveGain;
  float3 skyColor;
  float skyIntensity;
  float3 groundAlbedo;
  uint groundVisible; // 0 or 1
  float3 background;
};
