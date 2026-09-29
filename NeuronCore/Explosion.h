#pragma once

#include "Float3.h"
#include "Fragmentation.h"
#include "Sphere.h"
#include "VoxModel.h"

#include <cstdint>

namespace NeuronCore
{

// The detonation's parameter block (Design/Archive/SpaceScene.md §5.5). Lengths are in voxels and times in seconds, and nothing in
// it has an up: there is no gravity and no ground. Design/ADR/ADR-024 records the fragments' motion and its defaults, which
// replace ADR-013's, and Design/ADR/ADR-014 the inherited velocity and the seed, which a detonation brings with it (§7.7).
// What flies is a fragment (Fragmentation.h): a lone voxel near the blast, a chunk farther out.
struct ExplosionParameters
{
  Float3 blastOrigin;       // what every fragment is launched away from
  float launchSpeed;        // of a lone voxel at the blast origin, before its variation, in voxels per second
  float falloffDistance;    // the launch speed halves at this distance from the origin
  float directionJitter;    // the length of the hashed unit vector added to the launch direction
  float speedSpread;        // the launch speed is scaled by e^(speedSpread z), z near normal in [-3, 3]
  float drag;               // per second, of a lone voxel and of the inherited velocity: speed falls as e^(-drag t); positive
  float minDrag;            // the least drag of any fragment, the largest's: positive, and at most drag
  float maxSpinRadians;     // a lone voxel turns through up to this in all, about a hashed axis; a larger fragment less
  float shockSpeed;         // the blast crosses the model at this speed: a fragment starts once it reaches its pivot
  Float3 inheritedVelocity; // added to every fragment's motion: the velocity the entity had when it detonated
  std::uint32_t seed;       // mixed into every fragment's hash, so that two detonations of one model differ
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

// The defaults of ADR-024, around _blastOrigin, with no inherited velocity and seed 0.
[[nodiscard]] ExplosionParameters DefaultExplosionParameters(Float3 _blastOrigin) noexcept;

// pose(i, t) of §5.5: where a voxel whose center is _restCenter while the model is intact is _timeSeconds after the
// detonation, as part of fragment _fragment, the model's index of it, whose pivot and size are _shape. At time 0 it is
// exactly its intact self, and it stays so until the blast has crossed the model to its fragment's pivot. Then the
// fragment flies away from the blast origin, jittered and varied by its hash, the slower the larger it is, and turns
// about a hashed axis through its pivot, the less the larger it is; its own drag, the lower the larger it is, slows both
// towards where it ends. The inherited velocity carries every fragment alike, under the lone voxel's drag. The twin of
// ExplosionPose in Shader/Explosion.hlsli (R15).
[[nodiscard]] VoxelPose ExplosionPose(std::uint32_t _fragment, const Fragment& _shape, Float3 _restCenter,
                                      const ExplosionParameters& _parameters, float _timeSeconds) noexcept;

// The envelope of §5.5, bounded in closed form from the parameters, the box of the intact voxels and the farthest any
// voxel center lies from its fragment's pivot: a sphere that every voxel's box stays inside, and a time from which every
// voxel has drifted to a stop, no point of it farther than EXPLOSION_STOP_DISTANCE from where it ends. The sphere starts
// about the blast origin, and the inherited velocity carries it as it carries every voxel: by the time t it has moved
// drift × (1 - e^(-drag t)). It does not depend on the seed.
struct ExplosionEnvelope
{
  Float3 center; // at the detonation: the blast origin
  float radius;
  Float3 drift; // how far the center moves in all: the inherited velocity over the drag
  float stopSeconds;
};

[[nodiscard]] ExplosionEnvelope BoundExplosion(const ExplosionParameters& _parameters, Float3 _lower, Float3 _upper,
                                               float _fragmentRadius) noexcept;

// The sphere every voxel's box is inside at _timeSeconds after the detonation: the envelope's, as far along its drift as
// the drag has let the motion go. Culling uses it (§7.4).
[[nodiscard]] Sphere EnvelopeSphereAt(const ExplosionEnvelope& _envelope, const ExplosionParameters& _parameters,
                                      float _timeSeconds) noexcept;

// The sphere every voxel's box stays inside at every time: around the whole of the drift. What the shadow view is fitted
// around, and what F frames.
[[nodiscard]] Sphere EnvelopeSphere(const ExplosionEnvelope& _envelope) noexcept;

// The mean of every voxel's center, which is where the blast comes from by default (§5.5). A model with no voxel has
// none, and gives the origin.
[[nodiscard]] Float3 VoxelCentroid(const VoxModel& _model) noexcept;

} // namespace NeuronCore
