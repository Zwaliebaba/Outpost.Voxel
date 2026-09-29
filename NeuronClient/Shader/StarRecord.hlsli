#pragma once

// The HLSL mirror of NeuronCore/StarCatalog.h's StarRecord (R16, Design/Archive/SpaceScene.md §11.2): one star in its structured
// buffer, 28 bytes. The C++ struct is the truth, and the layout echo in NeuronClientTests proves that the two agree.
struct StarRecord
{
  float3 direction; // unit, in the world: towards the star
  float flux;       // 10^(-0.4 m), for its magnitude m
  float3 color;     // linear Rec. 709, of unit luminance
};
