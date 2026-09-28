#pragma once

#include "Float3.h"
#include "Lighting.h"
#include "OrthographicView.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// What the lighting pass knows besides the pixel it shades (R16, Design/Archive/SampleRenderer.md §7.4, §11). This struct is the
// truth; Shader/LightingConstants.hlsli mirrors it, and the layout echo in NeuronClientTests proves the two agree. A
// float3 and one scalar to each 16 bytes, as HLSL packs them. The ground color has no scalar beside it since the ground
// went (Design/ADR/ADR-013), so a named word of padding keeps the background where HLSL puts it, and the echo reads it.
struct LightingConstants
{
  NeuronCore::Float3 toSun; // unit, from a surface towards the sun
  float shadowNormalOffset; // world units a shaded point moves along its normal before the shadow map is read
  NeuronCore::Float3 sunRadiance;
  float emissiveGain;
  NeuronCore::Float3 skyColor;
  float skyIntensity;
  NeuronCore::Float3 groundColor;
  std::uint32_t padding; // 0
  NeuronCore::Float3 background;
};

static_assert(sizeof(LightingConstants) == 76);
static_assert(offsetof(LightingConstants, toSun) == 0);
static_assert(offsetof(LightingConstants, shadowNormalOffset) == 12);
static_assert(offsetof(LightingConstants, sunRadiance) == 16);
static_assert(offsetof(LightingConstants, emissiveGain) == 28);
static_assert(offsetof(LightingConstants, skyColor) == 32);
static_assert(offsetof(LightingConstants, skyIntensity) == 44);
static_assert(offsetof(LightingConstants, groundColor) == 48);
static_assert(offsetof(LightingConstants, padding) == 60);
static_assert(offsetof(LightingConstants, background) == 64);

// The twin's parameters as the shader reads them, with the normal offset _shadowView's map calls for (§10).
[[nodiscard]] LightingConstants MakeLightingConstants(const NeuronCore::LightingParameters& _lighting,
                                                      const NeuronCore::OrthographicView& _shadowView) noexcept;

} // namespace NeuronClient
