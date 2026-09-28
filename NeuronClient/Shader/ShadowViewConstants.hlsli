#pragma once

// The HLSL mirror of NeuronClient/ShadowViewConstants.h (R16, Design/Archive/SampleRenderer.md §7.4). The C++ struct is the
// truth, and the layout echo in NeuronClientTests proves that the two agree.
struct ShadowViewConstants
{
  float3 origin; // the centre of the near plane
  float halfWidth;
  float3 right;
  float halfHeight;
  float3 up;
  float depthRange;
  float3 forward;
  uint widthPixels;
  uint heightPixels;
};
