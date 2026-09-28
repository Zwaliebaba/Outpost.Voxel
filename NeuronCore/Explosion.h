#pragma once

#include "Float3.h"
#include "VoxModel.h"

#include <cstdint>

namespace NeuronCore
{

// The detonation's parameter block (Design/SpaceScene.md §5.5). Lengths are in voxels and times in seconds, and nothing in
// it has an up: there is no gravity and no ground. Design/ADR/ADR-013 records the defaults and how they were chosen.
struct ExplosionParameters
{
  Float3 blastOrigin;            // what every voxel is launched away from
  float launchSpeed;             // at the blast origin, in voxels per second
  float falloffDistance;         // the launch speed halves at this distance from the origin
  float directionJitter;         // the length of the hashed unit vector added to the launch direction
  float speedJitter;             // the launch speed varies by up to this fraction either way
  float drag;                    // per second: a voxel's speed and spin fall as e^(-drag t); positive
  std::uint32_t maxQuarterTurns; // each of a voxel's two spins turns through 1 to this many quarter turns, either way
};

// Half a voxel's diagonal: no point of a voxel, however it is turned, is farther than this from its center.
inline constexpr float VOXEL_BOUNDING_RADIUS = 0.866025404f; // √3 / 2

// From the envelope's stop time on, no point of any voxel is farther than this from where it ends, in voxels: the sliver
// of a voxel's edge within which the GPU tests let rounding decide (Design/Archive/SampleRenderer.md §14).
inline constexpr float EXPLOSION_STOP_DISTANCE = 1.0f / 256.0f;

// A voxel at some time after the detonation: its center, and its rotation as the box's three axes in world space.
struct VoxelPose
{
  Float3 center;
  Float3 axisX;
  Float3 axisY;
  Float3 axisZ;
};

// The defaults of ADR-013, around _blastOrigin.
[[nodiscard]] ExplosionParameters DefaultExplosionParameters(Float3 _blastOrigin) noexcept;

// pose(i, t) of §5.5: where voxel _voxel, whose center is _restCenter while the model is intact, is _timeSeconds after the
// detonation. At time 0 it is exactly its intact self. It slows under the drag towards where it ends, turned by a whole
// number of quarter turns about each of its two spin axes. The twin of ExplosionPose in Shader/Explosion.hlsli (R15).
[[nodiscard]] VoxelPose ExplosionPose(std::uint32_t _voxel, Float3 _restCenter, const ExplosionParameters& _parameters,
                                      float _timeSeconds) noexcept;

// The envelope of §5.5, bounded in closed form from the parameters and the box of the intact voxels: a sphere about the
// blast origin that every voxel's box stays inside, and a time from which every voxel has drifted to a stop, no point of
// it farther than EXPLOSION_STOP_DISTANCE from where it ends.
struct ExplosionEnvelope
{
  Float3 center;
  float radius;
  float stopSeconds;
};

[[nodiscard]] ExplosionEnvelope BoundExplosion(const ExplosionParameters& _parameters, Float3 _lower, Float3 _upper) noexcept;

// The mean of every voxel's center, which is where the blast comes from by default (§5.5). A model with no voxel has
// none, and gives the origin.
[[nodiscard]] Float3 VoxelCentroid(const VoxModel& _model) noexcept;

} // namespace NeuronCore
