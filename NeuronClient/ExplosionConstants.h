#pragma once

#include "Explosion.h"
#include "Float3.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// The explosion's parameter block and the time since the detonation, as the oriented splat shaders read them (R16,
// Design/SampleRenderer.md §7.4, §12). This struct is the truth; Shader/ExplosionConstants.hlsli mirrors it, and the
// layout echo in NeuronClientTests proves the two agree. The float3 and the time share 16 bytes, so there is no padding.
struct ExplosionConstants
{
  NeuronCore::Float3 blastOrigin;
  float timeSeconds; // since the detonation
  float gravity;
  float launchSpeed;
  float falloffDistance;
  float upwardBias;
  float directionJitter;
  float speedJitter;
  float restitution;
  float horizontalDamping;
  std::uint32_t maxQuarterTurns;
};

static_assert(sizeof(ExplosionConstants) == 52);
static_assert(offsetof(ExplosionConstants, blastOrigin) == 0);
static_assert(offsetof(ExplosionConstants, timeSeconds) == 12);
static_assert(offsetof(ExplosionConstants, gravity) == 16);
static_assert(offsetof(ExplosionConstants, launchSpeed) == 20);
static_assert(offsetof(ExplosionConstants, falloffDistance) == 24);
static_assert(offsetof(ExplosionConstants, upwardBias) == 28);
static_assert(offsetof(ExplosionConstants, directionJitter) == 32);
static_assert(offsetof(ExplosionConstants, speedJitter) == 36);
static_assert(offsetof(ExplosionConstants, restitution) == 40);
static_assert(offsetof(ExplosionConstants, horizontalDamping) == 44);
static_assert(offsetof(ExplosionConstants, maxQuarterTurns) == 48);

// The twin's parameters as the shaders read them, _timeSeconds after the detonation.
[[nodiscard]] ExplosionConstants MakeExplosionConstants(const NeuronCore::ExplosionParameters& _parameters, float _timeSeconds) noexcept;

} // namespace NeuronClient
