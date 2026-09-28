#pragma once

// The HLSL mirror of NeuronClient/ViewConstants.h (R16, Design/Archive/SampleRenderer.md §7.4). The C++ struct is the truth, and
// the layout echo in NeuronClientTests proves that the two agree.
struct ViewConstants
{
  float3 position;
  float tanHalfFovY;
  float3 right;
  float aspect;
  float3 up;
  float nearPlane;
  float3 forward;
  uint widthPixels;
  uint heightPixels;
};
