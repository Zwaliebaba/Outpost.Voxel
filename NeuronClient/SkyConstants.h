#pragma once

#include "Float3.h"
#include "Sky.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// What the sky pass knows besides the view (R16, Design/SpaceScene.md §11.5): the sun, the galaxy's frame and noise, and
// the two gains. This struct is the truth; Shader/SkyConstants.hlsli mirrors it, and the layout echo in NeuronClientTests
// proves the two agree. Each float3 shares 16 bytes with a scalar, so there is no padding.
struct SkyConstants
{
  NeuronCore::Float3 toSun;
  float sunAngularRadiusRadians;
  NeuronCore::Float3 sunRadiance; // at the middle of the disc
  std::uint32_t seed;             // the galaxy's noise's
  NeuronCore::Float3 galaxyX;     // the galaxy's axes in the world: its core, its north pole, and the third
  float galaxyGain;
  NeuronCore::Float3 galaxyY;
  float starGain;
  NeuronCore::Float3 galaxyZ;
};

static_assert(sizeof(SkyConstants) == 76);
static_assert(offsetof(SkyConstants, toSun) == 0);
static_assert(offsetof(SkyConstants, sunAngularRadiusRadians) == 12);
static_assert(offsetof(SkyConstants, sunRadiance) == 16);
static_assert(offsetof(SkyConstants, seed) == 28);
static_assert(offsetof(SkyConstants, galaxyX) == 32);
static_assert(offsetof(SkyConstants, galaxyGain) == 44);
static_assert(offsetof(SkyConstants, galaxyY) == 48);
static_assert(offsetof(SkyConstants, starGain) == 60);
static_assert(offsetof(SkyConstants, galaxyZ) == 64);

// The twin's parameters as the shaders read them.
[[nodiscard]] SkyConstants MakeSkyConstants(const NeuronCore::SkyParameters& _sky) noexcept;

} // namespace NeuronClient
