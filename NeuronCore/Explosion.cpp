#include "pch.h"

#include "Explosion.h"

#include "Hash.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NeuronCore
{
namespace
{

constexpr float TWO_PI = 6.28318531f;

// A launch direction that sums to almost nothing takes the jitter's instead.
constexpr float SMALLEST_DIRECTION = 1.0e-6f;

// The speed's variation is e^(speedSpread z), with z the sum of three uniform numbers, scaled to [-3, 3): near normal,
// with unit variance, and bounded, so that the envelope is.
constexpr float SPEED_SPREAD_BOUND = 3.0f;

// The independent random numbers each fragment draws: the hash of (fragment, stream), never buffer order (§5.5).
enum HashStream : std::uint32_t
{
  JitterHeight,
  JitterAngle,
  FirstSpeed,
  SecondSpeed,
  ThirdSpeed,
  SpinHeight,
  SpinAngle,
  SpinAmount
};
constexpr std::uint32_t HASH_STREAMS = 8;

// How far apart two seeds put a fragment's hash inputs: 2^32 over the golden ratio, which spreads consecutive seeds over
// the whole range of inputs. Seed 0 adds nothing (ADR-014).
constexpr std::uint32_t SEED_STEP = 0x9E3779B9u;

// Fragment _fragment's random bits for _stream in the detonation with _seed.
[[nodiscard]] std::uint32_t FragmentHash(std::uint32_t _fragment, HashStream _stream, std::uint32_t _seed) noexcept
{
  return PcgHash(_fragment * HASH_STREAMS + _stream + _seed * SEED_STEP);
}

// A number in [0, 1) from the top 24 bits of the hash, which a float holds exactly.
[[nodiscard]] float HashUnit(std::uint32_t _fragment, HashStream _stream, std::uint32_t _seed) noexcept
{
  return static_cast<float>(FragmentHash(_fragment, _stream, _seed) >> 8u) * (1.0f / 16777216.0f);
}

// A unit vector uniform on the sphere: its y uniform in [-1, 1), its heading about the y axis uniform.
[[nodiscard]] Float3 UnitVector(std::uint32_t _fragment, HashStream _height, HashStream _angle, std::uint32_t _seed) noexcept
{
  const float height = 2.0f * HashUnit(_fragment, _height, _seed) - 1.0f;
  const float angle = TWO_PI * HashUnit(_fragment, _angle, _seed);
  const float ring = std::sqrt(std::max(1.0f - height * height, 0.0f));
  return {ring * std::cos(angle), height, ring * std::sin(angle)};
}

// e^(speedSpread z), z = 2 (u1 + u2 + u3) - 3.
[[nodiscard]] float SpeedVariation(std::uint32_t _fragment, const ExplosionParameters& _parameters) noexcept
{
  const float sum = HashUnit(_fragment, FirstSpeed, _parameters.seed) + HashUnit(_fragment, SecondSpeed, _parameters.seed) +
                    HashUnit(_fragment, ThirdSpeed, _parameters.seed);
  return std::exp(_parameters.speedSpread * (2.0f * sum - SPEED_SPREAD_BOUND));
}

// Away from the blast origin from the fragment's pivot, jittered by hash, at a speed that falls off with distance, varies
// by hash and falls with the fragment's size. A fragment whose pivot is the origin itself flies along its jitter.
[[nodiscard]] Float3 LaunchVelocity(std::uint32_t _fragment, const Fragment& _shape, const ExplosionParameters& _parameters) noexcept
{
  const Float3 offset = _shape.pivot - _parameters.blastOrigin;
  const float distance = Length(offset);
  const Float3 away = distance > 0.0f ? offset * (1.0f / distance) : Float3{0.0f, 0.0f, 0.0f};
  const Float3 jitter = UnitVector(_fragment, JitterHeight, JitterAngle, _parameters.seed);
  const Float3 sum = away + jitter * _parameters.directionJitter;
  const float sumLength = Length(sum);
  const Float3 direction = sumLength > SMALLEST_DIRECTION ? sum * (1.0f / sumLength) : jitter;
  const float speed =
    _parameters.launchSpeed / (1.0f + distance / _parameters.falloffDistance) * SpeedVariation(_fragment, _parameters) * _shape.sizeScale;
  return direction * speed;
}

// A fragment's drag: the lone voxel's, times the square root of its size scale, and never below the least.
[[nodiscard]] float FragmentDrag(const Fragment& _shape, const ExplosionParameters& _parameters) noexcept
{
  return std::max(_parameters.drag * std::sqrt(_shape.sizeScale), _parameters.minDrag);
}

// How much of its motion a drag of _drag has let a fragment make by _timeSeconds after it started: 1 - e^(-drag t), from
// 0 at its start towards 1. Before its start, and at it, exactly 0 whatever exp makes of it, so that the intact model is
// exact (§5.5).
[[nodiscard]] float MotionMade(float _drag, float _timeSeconds) noexcept
{
  return _timeSeconds > 0.0f ? 1.0f - std::exp(-_drag * _timeSeconds) : 0.0f;
}

// _vector turned about the unit _axis by the angle whose cos and sin are given (Rodrigues). An angle of exactly 0, cos 1
// and sin 0, gives _vector exactly.
[[nodiscard]] Float3 Turn(Float3 _axis, float _cos, float _sin, Float3 _vector) noexcept
{
  return _vector * _cos + Cross(_axis, _vector) * _sin + _axis * (Dot(_axis, _vector) * (1.0f - _cos));
}

} // namespace

ExplosionParameters DefaultExplosionParameters(Float3 _blastOrigin) noexcept
{
  return {.blastOrigin = _blastOrigin,
          .launchSpeed = 180.0f,
          .falloffDistance = 150.0f,
          .directionJitter = 0.35f,
          .speedSpread = 0.3f,
          .drag = 1.0f,
          .minDrag = 0.3f,
          .maxSpinRadians = 12.5663706f,
          .shockSpeed = 400.0f,
          .inheritedVelocity = {0.0f, 0.0f, 0.0f},
          .seed = 0u};
}

VoxelPose ExplosionPose(std::uint32_t _fragment, const Fragment& _shape, Float3 _restCenter, const ExplosionParameters& _parameters,
                        float _timeSeconds) noexcept
{
  // The inherited velocity carries every fragment alike, under the lone voxel's drag, from the detonation on.
  const Float3 carried = _parameters.inheritedVelocity * (MotionMade(_parameters.drag, _timeSeconds) / _parameters.drag);

  // The fragment starts once the blast has crossed the model to its pivot. Under its drag its velocity falls as
  // e^(-drag t), so its pivot has moved the launch velocity times (1 - e^(-drag t)) / drag, and ends the launch velocity
  // over the drag from where it started (§5.5).
  const float delay = Length(_shape.pivot - _parameters.blastOrigin) / _parameters.shockSpeed;
  const float drag = FragmentDrag(_shape, _parameters);
  const float made = MotionMade(drag, _timeSeconds - delay);
  const Float3 flown = LaunchVelocity(_fragment, _shape, _parameters) * (made / drag);

  // The fragment turns about its hashed axis through its pivot, as the drag lets its motion run.
  const float angle = _parameters.maxSpinRadians * HashUnit(_fragment, SpinAmount, _parameters.seed) * _shape.sizeScale * made;
  const Float3 axis = UnitVector(_fragment, SpinHeight, SpinAngle, _parameters.seed);
  const float cosAngle = std::cos(angle);
  const float sinAngle = std::sin(angle);
  const Float3 offset = _restCenter - _shape.pivot;
  const Float3 center = _restCenter + (Turn(axis, cosAngle, sinAngle, offset) - offset) + flown + carried;
  return {center, Turn(axis, cosAngle, sinAngle, {1.0f, 0.0f, 0.0f}), Turn(axis, cosAngle, sinAngle, {0.0f, 1.0f, 0.0f}),
          Turn(axis, cosAngle, sinAngle, {0.0f, 0.0f, 1.0f})};
}

ExplosionEnvelope BoundExplosion(const ExplosionParameters& _parameters, Float3 _lower, Float3 _upper, float _fragmentRadius) noexcept
{
  // The intact voxels' centers lie half a voxel inside their box, and a fragment's pivot, their mean, among them, so the
  // farthest pivot from the blast origin is at most as far as the farthest corner of the box shrunk by that half.
  const Float3 lowest = _lower + Float3{0.5f, 0.5f, 0.5f};
  const Float3 highest = _upper - Float3{0.5f, 0.5f, 0.5f};
  float farthest = 0.0f;
  for (std::uint32_t corner = 0; corner < 8; ++corner)
  {
    const Float3 point{(corner & 1u) != 0u ? highest.x : lowest.x, (corner & 2u) != 0u ? highest.y : lowest.y,
                       (corner & 4u) != 0u ? highest.z : lowest.z};
    farthest = std::max(farthest, Length(point - _parameters.blastOrigin));
  }

  // A fragment's pivot launched from distance d at speed v s moves v s / max(drag √s, minDrag) ≤ v √s / drag before it
  // stops, so no more than reach / (1 + d / falloff), where reach is the fastest lone voxel's launch over the drag: it
  // ends within d + reach / (1 + d / falloff) of the origin. That sum is convex in d, so over the pivots, which lie
  // between 0 and the farthest distance, it is largest at one end or the other. A voxel keeps its distance from its
  // pivot however the fragment turns.
  const float fastest = _parameters.launchSpeed * std::exp(_parameters.speedSpread * SPEED_SPREAD_BOUND);
  const float reach = fastest / _parameters.drag;
  const float pivots = std::max(reach, farthest + reach / (1.0f + farthest / _parameters.falloffDistance));

  // The inherited velocity moves every voxel alike, so the sphere's center drifts by it over the drag, and the sphere
  // keeps its radius.
  const Float3 drift = _parameters.inheritedVelocity * (1.0f / _parameters.drag);

  // Every fragment has started by the time the blast reaches the farthest pivot. What is left of a fragment's motion a time
  // t after it started is e^(-drag t) of it, and its drag is at least the least: at most reach plus the drift for a
  // pivot, and for a point of a voxel the arc of what is left of the turn at its distance from the pivot.
  const float delay = farthest / _parameters.shockSpeed;
  const float travel = reach + Length(drift) + _parameters.maxSpinRadians * (_fragmentRadius + VOXEL_BOUNDING_RADIUS);
  const float stopSeconds =
    travel > EXPLOSION_STOP_DISTANCE ? delay + std::log(travel / EXPLOSION_STOP_DISTANCE) / _parameters.minDrag : 0.0f;
  return {_parameters.blastOrigin, pivots + _fragmentRadius + VOXEL_BOUNDING_RADIUS, drift, stopSeconds};
}

Sphere EnvelopeSphereAt(const ExplosionEnvelope& _envelope, const ExplosionParameters& _parameters, float _timeSeconds) noexcept
{
  return {_envelope.center + _envelope.drift * MotionMade(_parameters.drag, _timeSeconds), _envelope.radius};
}

Sphere EnvelopeSphere(const ExplosionEnvelope& _envelope) noexcept
{
  return {_envelope.center + _envelope.drift * 0.5f, _envelope.radius + 0.5f * Length(_envelope.drift)};
}

Float3 VoxelCentroid(const VoxModel& _model) noexcept
{
  // In double: the station's coordinates, summed over its 225,048 voxels, run past what a float holds exactly.
  double sumX = 0.0;
  double sumY = 0.0;
  double sumZ = 0.0;
  std::uint64_t count = 0;
  for (const ModelInstance& instance : _model.instances)
  {
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      const Float3 center = VoxelBox(instance, _model.records[instance.firstRecord + i]).center;
      sumX += center.x;
      sumY += center.y;
      sumZ += center.z;
      ++count;
    }
  }
  if (count == 0)
  {
    return {0.0f, 0.0f, 0.0f};
  }
  const double scale = 1.0 / static_cast<double>(count);
  return {static_cast<float>(sumX * scale), static_cast<float>(sumY * scale), static_cast<float>(sumZ * scale)};
}

} // namespace NeuronCore
