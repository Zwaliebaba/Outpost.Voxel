#pragma once

#include "Explosion.h"
#include "Float3.h"
#include "Placement.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// A placement's detonation as the oriented splat shaders read it: the parameter block, the time since the detonation, and
// where its model's fragments start in the scene's fragment buffer (R16, Design/Archive/SampleRenderer.md §7.4,
// Design/Archive/SpaceScene.md §5.5, §7.7, Design/ADR/ADR-024). This struct is the truth; Shader/ExplosionConstants.hlsli
// mirrors it, and the layout echo in NeuronClientTests proves the two agree. Each float3 shares 16 bytes with a scalar, so
// there is no padding.
struct ExplosionConstants
{
  NeuronCore::Float3 blastOrigin;
  float timeSeconds; // since the detonation; at 0 or before, the shader draws every voxel at rest and reads nothing else
  NeuronCore::Float3 inheritedVelocity;
  float launchSpeed;
  float falloffDistance;
  float directionJitter;
  float speedSpread;
  float drag;
  float minDrag;
  float maxSpinRadians;
  float shockSpeed;
  std::uint32_t seed;
  std::uint32_t firstFragment;
};

static_assert(sizeof(ExplosionConstants) == 68);
static_assert(offsetof(ExplosionConstants, blastOrigin) == 0);
static_assert(offsetof(ExplosionConstants, timeSeconds) == 12);
static_assert(offsetof(ExplosionConstants, inheritedVelocity) == 16);
static_assert(offsetof(ExplosionConstants, launchSpeed) == 28);
static_assert(offsetof(ExplosionConstants, falloffDistance) == 32);
static_assert(offsetof(ExplosionConstants, directionJitter) == 36);
static_assert(offsetof(ExplosionConstants, speedSpread) == 40);
static_assert(offsetof(ExplosionConstants, drag) == 44);
static_assert(offsetof(ExplosionConstants, minDrag) == 48);
static_assert(offsetof(ExplosionConstants, maxSpinRadians) == 52);
static_assert(offsetof(ExplosionConstants, shockSpeed) == 56);
static_assert(offsetof(ExplosionConstants, seed) == 60);
static_assert(offsetof(ExplosionConstants, firstFragment) == 64);

// What the oriented permutation reads for _placement: its detonation, at its time and with its model's first fragment;
// or, for a whole placement, zero, whose time 0 draws every voxel at rest.
[[nodiscard]] ExplosionConstants MakeExplosionConstants(const NeuronCore::Placement& _placement) noexcept;

} // namespace NeuronClient
