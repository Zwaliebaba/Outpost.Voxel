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
constexpr float HALF_PI = 1.57079633f;

// A launch direction that sums to almost nothing takes the jitter's instead.
constexpr float SMALLEST_DIRECTION = 1.0e-6f;

// How far a quarter turn about any axis through a voxel's center moves a point of the voxel, at most: π/2 × √3/2.
constexpr float CORNER_TRAVEL_PER_QUARTER_TURN = HALF_PI * VOXEL_BOUNDING_RADIUS;

// The independent random numbers each voxel draws: the hash of (voxel, stream), never buffer order (§5.5). The streams
// keep the numbers the retired explosion gave them, so that a voxel keeps its jitter, its speed and its spins.
enum HashStream : std::uint32_t
{
  JitterHeight,
  JitterAngle,
  SpeedVariation,
  SpinAxes,
  FirstSpin,
  SecondSpin
};
constexpr std::uint32_t HASH_STREAMS = 8;

// A number in [0, 1) from the top 24 bits of the hash, which a float holds exactly.
[[nodiscard]] float HashUnit(std::uint32_t _voxel, HashStream _stream) noexcept
{
  return static_cast<float>(PcgHash(_voxel * HASH_STREAMS + _stream) >> 8u) * (1.0f / 16777216.0f);
}

// A unit vector uniform on the sphere: its y uniform in [-1, 1), its heading about the y axis uniform.
[[nodiscard]] Float3 Jitter(std::uint32_t _voxel) noexcept
{
  const float height = 2.0f * HashUnit(_voxel, JitterHeight) - 1.0f;
  const float angle = TWO_PI * HashUnit(_voxel, JitterAngle);
  const float ring = std::sqrt(std::max(1.0f - height * height, 0.0f));
  return {ring * std::cos(angle), height, ring * std::sin(angle)};
}

// Away from the blast origin, jittered by hash, at a speed that falls off with distance. A voxel with no direction away
// from the origin, one at the origin itself, flies along its jitter.
[[nodiscard]] Float3 LaunchVelocity(std::uint32_t _voxel, Float3 _restCenter, const ExplosionParameters& _parameters) noexcept
{
  const Float3 offset = _restCenter - _parameters.blastOrigin;
  const float distance = Length(offset);
  const Float3 away = distance > 0.0f ? offset * (1.0f / distance) : Float3{0.0f, 0.0f, 0.0f};
  const Float3 jitter = Jitter(_voxel);
  const Float3 sum = away + jitter * _parameters.directionJitter;
  const float sumLength = Length(sum);
  const Float3 direction = sumLength > SMALLEST_DIRECTION ? sum * (1.0f / sumLength) : jitter;
  const float variation = 1.0f + _parameters.speedJitter * (2.0f * HashUnit(_voxel, SpeedVariation) - 1.0f);
  const float speed = _parameters.launchSpeed / (1.0f + distance / _parameters.falloffDistance) * variation;
  return direction * speed;
}

// How much of its motion the drag has let a voxel make by _timeSeconds: 1 - e^(-drag t), from 0 at the detonation
// towards 1. Time 0 gives exactly 0 whatever exp makes of it, so that the intact model is exact (§5.5).
[[nodiscard]] float MotionMade(float _drag, float _timeSeconds) noexcept
{
  return _timeSeconds > 0.0f ? 1.0f - std::exp(-_drag * _timeSeconds) : 0.0f;
}

// cos and sin of _quarterTurns quarter turns. The whole turns are taken exactly and only the rest goes through cos and
// sin, so that a whole number of quarter turns gives exactly 0 and ±1.
void QuarterTurnCosSin(float _quarterTurns, float& _cos, float& _sin) noexcept
{
  const float whole = std::floor(_quarterTurns + 0.5f);
  const float angle = (_quarterTurns - whole) * HALF_PI;
  const float cosRest = std::cos(angle);
  const float sinRest = std::sin(angle);
  switch (static_cast<std::int32_t>(whole) & 3)
  {
  case 0:
    _cos = cosRest;
    _sin = sinRest;
    break;
  case 1:
    _cos = -sinRest;
    _sin = cosRest;
    break;
  case 2:
    _cos = -cosRest;
    _sin = -sinRest;
    break;
  default:
    _cos = sinRest;
    _sin = -cosRest;
    break;
  }
}

// _vector turned about coordinate axis _axis (0, 1 or 2 for x, y or z) by the angle whose cos and sin are given.
[[nodiscard]] Float3 RotateAboutAxis(std::uint32_t _axis, float _cos, float _sin, Float3 _vector) noexcept
{
  if (_axis == 0u)
  {
    return {_vector.x, _cos * _vector.y - _sin * _vector.z, _sin * _vector.y + _cos * _vector.z};
  }
  if (_axis == 1u)
  {
    return {_cos * _vector.x + _sin * _vector.z, _vector.y, _cos * _vector.z - _sin * _vector.x};
  }
  return {_cos * _vector.x - _sin * _vector.y, _sin * _vector.x + _cos * _vector.y, _vector.z};
}

// _vector turned by the second spin and then the first: a column of the voxel's rotation when _vector is a unit axis.
[[nodiscard]] Float3 Spin(std::uint32_t _firstAxis, float _firstCos, float _firstSin, std::uint32_t _secondAxis, float _secondCos,
                          float _secondSin, Float3 _vector) noexcept
{
  return RotateAboutAxis(_firstAxis, _firstCos, _firstSin, RotateAboutAxis(_secondAxis, _secondCos, _secondSin, _vector));
}

// A spin's whole number of quarter turns: 1 to maxQuarterTurns either way, or none if that is 0.
[[nodiscard]] float SpinQuarterTurns(std::uint32_t _voxel, HashStream _stream, std::uint32_t _maxQuarterTurns) noexcept
{
  if (_maxQuarterTurns == 0u)
  {
    return 0.0f;
  }
  const std::uint32_t hash = PcgHash(_voxel * HASH_STREAMS + _stream);
  const float turns = static_cast<float>(1u + (hash >> 1u) % _maxQuarterTurns);
  return (hash & 1u) != 0u ? -turns : turns;
}

} // namespace

ExplosionParameters DefaultExplosionParameters(Float3 _blastOrigin) noexcept
{
  return {.blastOrigin = _blastOrigin,
          .launchSpeed = 350.0f,
          .falloffDistance = 150.0f,
          .directionJitter = 0.35f,
          .speedJitter = 0.2f,
          .drag = 1.0f,
          .maxQuarterTurns = 4u};
}

VoxelPose ExplosionPose(std::uint32_t _voxel, Float3 _restCenter, const ExplosionParameters& _parameters, float _timeSeconds) noexcept
{
  // Under a drag the velocity falls as e^(-drag t), so the center has moved the launch velocity times
  // (1 - e^(-drag t)) / drag, and ends the launch velocity over the drag from where it started (§5.5).
  const float made = MotionMade(_parameters.drag, _timeSeconds);
  const Float3 center = _restCenter + LaunchVelocity(_voxel, _restCenter, _parameters) * (made / _parameters.drag);

  // Two different coordinate axes, each turned through its whole number of quarter turns as the drag lets the motion
  // run, so that each spin slows with the flight and the voxel ends square to the axes.
  const std::uint32_t axes = PcgHash(_voxel * HASH_STREAMS + SpinAxes);
  const std::uint32_t firstAxis = axes % 3u;
  const std::uint32_t secondAxis = (firstAxis + 1u + (axes >> 16u) % 2u) % 3u;
  float firstCos = 1.0f;
  float firstSin = 0.0f;
  float secondCos = 1.0f;
  float secondSin = 0.0f;
  QuarterTurnCosSin(SpinQuarterTurns(_voxel, FirstSpin, _parameters.maxQuarterTurns) * made, firstCos, firstSin);
  QuarterTurnCosSin(SpinQuarterTurns(_voxel, SecondSpin, _parameters.maxQuarterTurns) * made, secondCos, secondSin);

  return {center, Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, {1.0f, 0.0f, 0.0f}),
          Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, {0.0f, 1.0f, 0.0f}),
          Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, {0.0f, 0.0f, 1.0f})};
}

ExplosionEnvelope BoundExplosion(const ExplosionParameters& _parameters, Float3 _lower, Float3 _upper) noexcept
{
  // The intact voxels' centers lie half a voxel inside their box, so the farthest from the blast origin is at most as
  // far as the farthest corner of the box shrunk by that half.
  const Float3 lowest = _lower + Float3{0.5f, 0.5f, 0.5f};
  const Float3 highest = _upper - Float3{0.5f, 0.5f, 0.5f};
  float farthest = 0.0f;
  for (std::uint32_t corner = 0; corner < 8; ++corner)
  {
    const Float3 point{(corner & 1u) != 0u ? highest.x : lowest.x, (corner & 2u) != 0u ? highest.y : lowest.y,
                       (corner & 4u) != 0u ? highest.z : lowest.z};
    farthest = std::max(farthest, Length(point - _parameters.blastOrigin));
  }

  // A voxel launched from distance d moves at most reach / (1 + d / falloff) before it stops, where reach is the
  // fastest launch over the drag, so it ends within d + reach / (1 + d / falloff) of the origin. That sum is convex in
  // d, so over the voxels, which lie between 0 and the farthest distance, it is largest at one end or the other.
  const float fastest = _parameters.launchSpeed * (1.0f + _parameters.speedJitter);
  const float reach = fastest / _parameters.drag;
  const float centers = std::max(reach, farthest + reach / (1.0f + farthest / _parameters.falloffDistance));

  // What is left of the motion at time t is e^(-drag t) of it: at most reach for a center, and for a point of the voxel
  // a quarter turn's travel for each quarter turn its two spins have left.
  const float travel = reach + 2.0f * static_cast<float>(_parameters.maxQuarterTurns) * CORNER_TRAVEL_PER_QUARTER_TURN;
  const float stopSeconds = travel > EXPLOSION_STOP_DISTANCE ? std::log(travel / EXPLOSION_STOP_DISTANCE) / _parameters.drag : 0.0f;
  return {_parameters.blastOrigin, centers + VOXEL_BOUNDING_RADIUS, stopSeconds};
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
