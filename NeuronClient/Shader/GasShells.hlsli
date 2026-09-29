#pragma once

// The HLSL mirror of NeuronCore/Blast.h's GasShells and GasShell (R16, Design/ADR/ADR-025): the frame's shells of hot gas.
// The C++ structs are the truth, and the layout echo in NeuronClientTests proves that the two agree.

#include "BlastLighting.hlsli"

struct GasShell
{
  float3 center;
  float radius;
  float3 emission;
  float thickness;
  uint seed;
  uint padding0;
  uint padding1;
  uint padding2;
};

struct GasShells
{
  GasShell shells[MAX_LIT_BLASTS];
  uint count;
  uint padding0;
  uint padding1;
  uint padding2;
};
