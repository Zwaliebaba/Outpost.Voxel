#pragma once

// The HLSL mirror of NeuronCore/Blast.h's GasShells and GasShell (R16, Design/ADR/ADR-025): the frame's shells of hot gas.
// The C++ structs are the truth, and the layout echo in NeuronClientTests proves that the two agree. It includes nothing,
// so that the layout echo can include it beside BlastLighting.hlsli: a header reached by two spellings of its path is
// read twice, whatever #pragma once says.

// NeuronCore's MAX_LIT_BLASTS, as the shells' count.
static const uint MAX_GAS_SHELLS = 8;

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
  GasShell shells[MAX_GAS_SHELLS];
  uint count;
  uint padding0;
  uint padding1;
  uint padding2;
};
