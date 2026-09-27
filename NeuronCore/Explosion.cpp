#include "pch.h"

#include "Explosion.h"

#include "Hash.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace NeuronCore
{
namespace
{

constexpr float TWO_PI = 6.28318531f;
constexpr float HALF_PI = 1.57079633f;

// A launch direction that sums to almost nothing points straight up instead.
constexpr float SMALLEST_DIRECTION = 1.0e-6f;
// A spin always has some time to run: this much, if the flights leave it none.
constexpr float SHORTEST_SPIN_SECONDS = 1.0e-6f;

// The independent random numbers each voxel draws: the hash of (voxel, stream), never buffer order (§12).
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

// How long a voxel at _height, moving up at _verticalSpeed, takes to come down to _contactHeight: the later root of the
// flight's quadratic.
[[nodiscard]] float FlightTime(float _height, float _verticalSpeed, float _contactHeight, float _gravity) noexcept
{
  const float discriminant = _verticalSpeed * _verticalSpeed + 2.0f * _gravity * (_height - _contactHeight);
  return (_verticalSpeed + std::sqrt(std::max(discriminant, 0.0f))) / _gravity;
}

// Away from the blast origin, biased upward and jittered by hash, at a speed that falls off with distance. A voxel that
// starts below the bounding radius is lifted clear of it.
[[nodiscard]] Float3 LaunchVelocity(std::uint32_t _voxel, Float3 _restCenter, const ExplosionParameters& _parameters) noexcept
{
  const Float3 offset = _restCenter - _parameters.blastOrigin;
  const float distance = Length(offset);
  const Float3 away = distance > 0.0f ? offset * (1.0f / distance) : Float3{0.0f, 0.0f, 1.0f};

  // A unit vector uniform on the sphere: its height uniform in [-1, 1), its heading uniform.
  const float jitterHeight = 2.0f * HashUnit(_voxel, JitterHeight) - 1.0f;
  const float jitterAngle = TWO_PI * HashUnit(_voxel, JitterAngle);
  const float jitterRing = std::sqrt(std::max(1.0f - jitterHeight * jitterHeight, 0.0f));
  const Float3 jitter{jitterRing * std::cos(jitterAngle), jitterRing * std::sin(jitterAngle), jitterHeight};

  const Float3 sum = away + Float3{0.0f, 0.0f, _parameters.upwardBias} + jitter * _parameters.directionJitter;
  const float sumLength = Length(sum);
  const Float3 direction = sumLength > SMALLEST_DIRECTION ? sum * (1.0f / sumLength) : Float3{0.0f, 0.0f, 1.0f};
  const float variation = 1.0f + _parameters.speedJitter * (2.0f * HashUnit(_voxel, SpeedVariation) - 1.0f);
  const float speed = _parameters.launchSpeed / (1.0f + distance / _parameters.falloffDistance) * variation;

  Float3 velocity = direction * speed;
  const float liftHeight = VOXEL_BOUNDING_RADIUS + EXPLOSION_LIFT_CLEARANCE - _restCenter.z;
  velocity.z = std::max(velocity.z, std::sqrt(std::max(2.0f * _parameters.gravity * liftHeight, 0.0f)));
  return velocity;
}

// The piecewise trajectory, followed from the launch at every evaluation (§12): a flight to the bounding radius, the
// bounces, and a last flight down to the rest height.
struct Trajectory
{
  Float3 center;                                           // at the time asked
  std::array<float, EXPLOSION_BOUNCES + 1> contactSeconds; // each bounce, then the landing
  float spinStartSeconds;                                  // when the center first stands a bounding radius above the ground
  float spinEndSeconds;                                    // when it last does, on the way down to rest
};

[[nodiscard]] Trajectory FollowTrajectory(std::uint32_t _voxel, Float3 _restCenter, const ExplosionParameters& _parameters,
                                          float _timeSeconds) noexcept
{
  const float gravity = _parameters.gravity;
  const Float3 launch = LaunchVelocity(_voxel, _restCenter, _parameters);

  // A voxel that starts at or above the bounding radius turns from the start; a lifted one once it has risen that far,
  // which is the earlier root of the same quadratic.
  const float rise = launch.z * launch.z + 2.0f * gravity * (_restCenter.z - VOXEL_BOUNDING_RADIUS);
  const float spinStart = std::max((launch.z - std::sqrt(std::max(rise, 0.0f))) / gravity, 0.0f);

  float x = _restCenter.x;
  float y = _restCenter.y;
  float height = _restCenter.z;
  float velocityX = launch.x;
  float velocityY = launch.y;
  float velocityZ = launch.z;
  float flightStart = 0.0f;
  Float3 center{0.0f, 0.0f, 0.0f};
  std::array<float, EXPLOSION_BOUNCES + 1> contacts{};
  float spinEnd = 0.0f;
  bool placed = false;
  for (std::uint32_t flight = 0; flight <= EXPLOSION_BOUNCES; ++flight)
  {
    const float contactHeight = flight < EXPLOSION_BOUNCES ? VOXEL_BOUNDING_RADIUS : VOXEL_REST_HEIGHT;
    const float duration = FlightTime(height, velocityZ, contactHeight, gravity);
    if (flight == EXPLOSION_BOUNCES)
    {
      spinEnd = flightStart + FlightTime(height, velocityZ, VOXEL_BOUNDING_RADIUS, gravity);
    }
    if (!placed && _timeSeconds < flightStart + duration)
    {
      const float elapsed = std::max(_timeSeconds - flightStart, 0.0f);
      center = {x + velocityX * elapsed, y + velocityY * elapsed, height + velocityZ * elapsed - 0.5f * gravity * elapsed * elapsed};
      placed = true;
    }

    // At the contact, the vertical velocity reflects with restitution and the horizontal one is damped.
    x += velocityX * duration;
    y += velocityY * duration;
    height = contactHeight;
    const float impactSpeed = gravity * duration - velocityZ;
    velocityZ = _parameters.restitution * impactSpeed;
    velocityX *= _parameters.horizontalDamping;
    velocityY *= _parameters.horizontalDamping;
    flightStart += duration;
    contacts[flight] = flightStart;
  }
  if (!placed)
  {
    center = {x, y, VOXEL_REST_HEIGHT};
  }
  return {center, contacts, spinStart, spinEnd};
}

// cos and sin of _quarterTurns quarter turns. The whole turns are taken exactly and only the rest goes through cos and
// sin, so that a whole number of quarter turns, at rest, gives exactly 0 and ±1.
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
          .gravity = 30.0f,
          .launchSpeed = 50.0f,
          .falloffDistance = 150.0f,
          .upwardBias = 0.5f,
          .directionJitter = 0.35f,
          .speedJitter = 0.2f,
          .restitution = 0.3f,
          .horizontalDamping = 0.4f,
          .maxQuarterTurns = 4u};
}

VoxelPose ExplosionPose(std::uint32_t _voxel, Float3 _restCenter, const ExplosionParameters& _parameters, float _timeSeconds) noexcept
{
  const Trajectory trajectory = FollowTrajectory(_voxel, _restCenter, _parameters, _timeSeconds);

  // Two different coordinate axes, each turned through a whole number of quarter turns, eased to no angular velocity
  // (§12). The spin runs only while the center is at least a bounding radius above the ground, so no rotation can push a
  // corner into it: it ends as the voxel passes that height on its way down to rest, and the voxel lands already square
  // (Design/ADR/ADR-009).
  const std::uint32_t axes = PcgHash(_voxel * HASH_STREAMS + SpinAxes);
  const std::uint32_t firstAxis = axes % 3u;
  const std::uint32_t secondAxis = (firstAxis + 1u + (axes >> 16u) % 2u) % 3u;
  const float spinSeconds = std::max(trajectory.spinEndSeconds - trajectory.spinStartSeconds, SHORTEST_SPIN_SECONDS);
  const float progress = std::clamp((_timeSeconds - trajectory.spinStartSeconds) / spinSeconds, 0.0f, 1.0f);
  const float eased = progress * (2.0f - progress);

  float firstCos = 1.0f;
  float firstSin = 0.0f;
  float secondCos = 1.0f;
  float secondSin = 0.0f;
  QuarterTurnCosSin(SpinQuarterTurns(_voxel, FirstSpin, _parameters.maxQuarterTurns) * eased, firstCos, firstSin);
  QuarterTurnCosSin(SpinQuarterTurns(_voxel, SecondSpin, _parameters.maxQuarterTurns) * eased, secondCos, secondSin);

  return {trajectory.center, Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, {1.0f, 0.0f, 0.0f}),
          Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, {0.0f, 1.0f, 0.0f}),
          Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, {0.0f, 0.0f, 1.0f})};
}

std::array<float, EXPLOSION_BOUNCES + 1> ExplosionContactTimes(std::uint32_t _voxel, Float3 _restCenter,
                                                               const ExplosionParameters& _parameters) noexcept
{
  return FollowTrajectory(_voxel, _restCenter, _parameters, 0.0f).contactSeconds;
}

float ExplosionRestTime(std::uint32_t _voxel, Float3 _restCenter, const ExplosionParameters& _parameters) noexcept
{
  return ExplosionContactTimes(_voxel, _restCenter, _parameters)[EXPLOSION_BOUNCES];
}

ExplosionEnvelope BoundExplosion(const ExplosionParameters& _parameters, Float3 _lower, Float3 _upper) noexcept
{
  const float gravity = _parameters.gravity;
  const float restitution = _parameters.restitution;
  const float damping = _parameters.horizontalDamping;

  // The intact voxels' centers lie half a voxel inside their box. The fastest launch is at the origin, with the most
  // variation, and the fastest lift is the lowest voxel's.
  const float lowest = _lower.z + 0.5f;
  const float highest = _upper.z - 0.5f;
  const float speed = _parameters.launchSpeed * (1.0f + _parameters.speedJitter);
  const float lift = std::sqrt(std::max(2.0f * gravity * (VOXEL_BOUNDING_RADIUS + EXPLOSION_LIFT_CLEARANCE - lowest), 0.0f));
  const float rise = std::max(speed, lift);
  const float drop = std::max(highest - VOXEL_BOUNDING_RADIUS, 0.0f);
  const float impact = std::sqrt(rise * rise + 2.0f * gravity * drop);

  // The first flight's horizontal reach. At a given speed, the farthest a launch from a height carries is
  // (v / g) sqrt(v² + 2gh), whatever the angle. A lifted voxel may rise faster than its speed allows, but its horizontal
  // speed is still at most the launch speed, for at most twice its lift's time aloft.
  float reach = std::max(speed / gravity * std::sqrt(speed * speed + 2.0f * gravity * drop), 2.0f * speed * lift / gravity);
  float restTime = (rise + impact) / gravity;

  // Each bounce keeps a fraction of the vertical speed and of the horizontal one.
  float verticalSpeed = impact;
  float horizontalSpeed = speed;
  float bounceApex = 0.0f;
  for (std::uint32_t bounce = 1; bounce <= EXPLOSION_BOUNCES; ++bounce)
  {
    verticalSpeed *= restitution;
    horizontalSpeed *= damping;
    const float contactHeight = bounce < EXPLOSION_BOUNCES ? VOXEL_BOUNDING_RADIUS : VOXEL_REST_HEIGHT;
    const float duration = FlightTime(VOXEL_BOUNDING_RADIUS, verticalSpeed, contactHeight, gravity);
    reach += horizontalSpeed * duration;
    restTime += duration;
    bounceApex = std::max(bounceApex, VOXEL_BOUNDING_RADIUS + verticalSpeed * verticalSpeed / (2.0f * gravity));
  }
  const float apex = std::max(highest + rise * rise / (2.0f * gravity), bounceApex);

  // A box's corners stay within the bounding radius of its center, and no corner goes below the ground.
  const float margin = reach + VOXEL_BOUNDING_RADIUS - 0.5f;
  return {{_lower.x - margin, _lower.y - margin, std::min(_lower.z, 0.0f)},
          {_upper.x + margin, _upper.y + margin, std::max(_upper.z, apex + VOXEL_BOUNDING_RADIUS)},
          restTime};
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
