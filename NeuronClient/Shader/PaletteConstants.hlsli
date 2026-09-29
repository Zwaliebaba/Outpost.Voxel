#pragma once

// The HLSL mirror of NeuronClient/PaletteConstants.h (R16, Design/Archive/SampleRenderer.md §7.2, §7.4): an element of the
// scene's structured buffer of palettes, one per model (Design/Archive/SpaceScene.md §7.1). The C++ struct is the truth, and the
// layout echo in NeuronClientTests proves that the two agree.

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
