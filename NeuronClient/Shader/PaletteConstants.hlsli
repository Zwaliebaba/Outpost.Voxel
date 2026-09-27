#pragma once

// The HLSL mirror of NeuronClient/PaletteConstants.h (R16, Design/SampleRenderer.md §7.2, §7.4). The C++ struct is the
// truth, and the layout echo in NeuronClientTests proves that the two agree.

// R14: the palette has sixteen entries.
static const uint PALETTE_ENTRY_COUNT = 16;

struct PaletteMaterial
{
  float3 albedo; // linear
  float emissiveScale;
};

struct PaletteConstants
{
  PaletteMaterial materials[PALETTE_ENTRY_COUNT];
};
