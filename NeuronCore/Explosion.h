#pragma once

#include "Float3.h"
#include "VoxModel.h"

#include <array>
#include <cstdint>

namespace NeuronCore
{

// The explosion's parameter block (Design/Archive/SampleRenderer.md §12). Lengths are in voxels and times in seconds, the
// world's +Y is up and the ground is the plane y = 0 (§7.5). Design/ADR/ADR-009 records the defaults and how they were
// chosen.
struct ExplosionParameters
{
  Float3 blastOrigin;            // what every voxel is launched away from
  float gravity;                 // downward, in voxels per second squared
  float launchSpeed;             // at the blast origin, in voxels per second
  float falloffDistance;         // the launch speed halves at this distance from the origin
  float upwardBias;              // added to the launch direction's vertical component before it is normalized
  float directionJitter;         // the length of the hashed unit vector added to the launch direction
  float speedJitter;             // the launch speed varies by up to this fraction either way
  float restitution;             // the fraction of its vertical speed a voxel keeps at a bounce
  float horizontalDamping;       // the fraction of its horizontal speed a voxel keeps at a bounce
  std::uint32_t maxQuarterTurns; // each of a voxel's two spins turns through 1 to this many quarter turns, either way
};

// How many times a voxel bounces before it rests (§12: "a small fixed number").
inline constexpr std::uint32_t EXPLOSION_BOUNCES = 3;

// A bounce happens where a voxel's bounding sphere meets the ground, so no rotation can push a corner into it, and a
// voxel rests where its own bottom face does (§12).
inline constexpr float VOXEL_BOUNDING_RADIUS = 0.866025404f; // √3 / 2
inline constexpr float VOXEL_REST_HEIGHT = 0.5f;

// A voxel whose center starts lower than the bounding radius, one of the layer on the ground, is launched up fast enough
// to rise this far above that radius, and turns only once its center has passed it (Design/ADR/ADR-009).
inline constexpr float EXPLOSION_LIFT_CLEARANCE = 0.5f;

// A voxel at some time after the detonation: its center, and its rotation as the box's three axes in world space.
struct VoxelPose
{
  Float3 center;
  Float3 axisX;
  Float3 axisY;
  Float3 axisZ;
};

// The defaults of ADR-009, around _blastOrigin.
[[nodiscard]] ExplosionParameters DefaultExplosionParameters(Float3 _blastOrigin) noexcept;

// pose(i, t) of §12: where voxel _voxel, whose center is _restCenter while the model is intact, is _timeSeconds after
// the detonation. At time 0 it is at rest with no rotation, and from its rest time on it lies flat on the ground in one
// of the cube's 24 orientations. The twin of ExplosionPose in Shader/Explosion.hlsli (R15).
[[nodiscard]] VoxelPose ExplosionPose(std::uint32_t _voxel, Float3 _restCenter, const ExplosionParameters& _parameters,
                                      float _timeSeconds) noexcept;

// When voxel _voxel meets the ground: its bounces, then its landing, which is T_rest(i) of §12.
[[nodiscard]] std::array<float, EXPLOSION_BOUNCES + 1> ExplosionContactTimes(std::uint32_t _voxel, Float3 _restCenter,
                                                                             const ExplosionParameters& _parameters) noexcept;

// T_rest(i) of §12: when voxel _voxel comes to rest.
[[nodiscard]] float ExplosionRestTime(std::uint32_t _voxel, Float3 _restCenter, const ExplosionParameters& _parameters) noexcept;

// The envelope of §12, bounded in closed form from the parameters and the box of the intact voxels: a box every voxel's
// box stays inside, and a time by which every voxel rests.
struct ExplosionEnvelope
{
  Float3 lower;
  Float3 upper;
  float restTimeSeconds;
};

[[nodiscard]] ExplosionEnvelope BoundExplosion(const ExplosionParameters& _parameters, Float3 _lower, Float3 _upper) noexcept;

// The mean of every voxel's center, which is where the blast comes from by default (§12). A model with no voxel has
// none, and gives the origin.
[[nodiscard]] Float3 VoxelCentroid(const VoxModel& _model) noexcept;

} // namespace NeuronCore
