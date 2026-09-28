#pragma once

#include "Float3.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <array>
#include <cstddef>

namespace NeuronClient
{

// One palette entry as the shaders read it (Design/Archive/SampleRenderer.md §7.2): linear albedo, and the scale that turns it
// into emitted light, NeuronCore::EmissiveScale's reading of the file's _emit and _flux (Design/ADR/ADR-008).
struct PaletteMaterial
{
  NeuronCore::Float3 albedo;
  float emissiveScale;
};

// The sixteen entries of R14, 256 bytes (R16, §7.4): one model's palette, an element of the scene's structured buffer of
// palettes (Design/SpaceScene.md §7.1). This struct is the truth; Shader/PaletteConstants.hlsli mirrors it, and the
// layout echo in NeuronClientTests proves the two agree.
struct PaletteConstants
{
  std::array<PaletteMaterial, NeuronCore::PALETTE_ENTRY_COUNT> materials;
};

static_assert(sizeof(PaletteMaterial) == 16);
static_assert(offsetof(PaletteMaterial, albedo) == 0);
static_assert(offsetof(PaletteMaterial, emissiveScale) == 12);
static_assert(sizeof(PaletteConstants) == 256);
static_assert(offsetof(PaletteConstants, materials) == 0);

// The file's sRGB bytes as linear albedo, by the exact curve, and each entry's emissive scale.
[[nodiscard]] PaletteConstants
MakePaletteConstants(const std::array<NeuronCore::PaletteEntry, NeuronCore::PALETTE_ENTRY_COUNT>& _palette) noexcept;

} // namespace NeuronClient
