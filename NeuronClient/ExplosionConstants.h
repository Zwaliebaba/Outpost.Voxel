#pragma once

#include "Explosion.h"
#include "Float3.h"
#include "Placement.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// A placement's detonation as the oriented splat shaders read it: the parameter block, the time since the detonation, and
// the model-local index its voxels' hashes count from (R16, Design/Archive/SampleRenderer.md §7.4, Design/SpaceScene.md
// §5.5, §7.7). This struct is the truth; Shader/ExplosionConstants.hlsli mirrors it, and the layout echo in
// NeuronClientTests proves the two agree. Each float3 shares 16 bytes with a scalar, so there is no padding.
struct ExplosionConstants
{
  NeuronCore::Float3 blastOrigin;
  float timeSeconds; // since the detonation; at 0 or before, the shader draws every voxel at rest and reads nothing else
  NeuronCore::Float3 inheritedVelocity;
  float launchSpeed;
  float falloffDistance;
  float directionJitter;
  float speedJitter;
  float drag;
  std::uint32_t maxQuarterTurns;
  std::uint32_t seed;
  std::uint32_t hashBase;
};

static_assert(sizeof(ExplosionConstants) == 60);
static_assert(offsetof(ExplosionConstants, blastOrigin) == 0);
static_assert(offsetof(ExplosionConstants, timeSeconds) == 12);
static_assert(offsetof(ExplosionConstants, inheritedVelocity) == 16);
static_assert(offsetof(ExplosionConstants, launchSpeed) == 28);
static_assert(offsetof(ExplosionConstants, falloffDistance) == 32);
static_assert(offsetof(ExplosionConstants, directionJitter) == 36);
static_assert(offsetof(ExplosionConstants, speedJitter) == 40);
static_assert(offsetof(ExplosionConstants, drag) == 44);
static_assert(offsetof(ExplosionConstants, maxQuarterTurns) == 48);
static_assert(offsetof(ExplosionConstants, seed) == 52);
static_assert(offsetof(ExplosionConstants, hashBase) == 56);

// What the oriented permutation reads for _placement: its detonation, at its time and with its hash base; or, for a whole
// placement, zero, whose time 0 draws every voxel at rest.
[[nodiscard]] ExplosionConstants MakeExplosionConstants(const NeuronCore::Placement& _placement) noexcept;

} // namespace NeuronClient
