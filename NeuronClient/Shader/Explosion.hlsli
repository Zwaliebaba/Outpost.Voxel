#pragma once

// pose(i, t) of the explosion (Design/SampleRenderer.md §12): where a voxel is, and how it is turned, some time after the
// detonation. The C++ twin is NeuronCore/Explosion.h and .cpp (R15), function for function; the explosion's envelope is
// the CPU's alone, since no shader needs it.

#include "ExplosionConstants.hlsli"
#include "Hash.hlsli"

static const uint EXPLOSION_BOUNCES = 3;
static const float VOXEL_BOUNDING_RADIUS = 0.866025404; // √3 / 2
static const float VOXEL_REST_HEIGHT = 0.5;
static const float EXPLOSION_LIFT_CLEARANCE = 0.5;

static const float TWO_PI = 6.28318531;
static const float HALF_PI = 1.57079633;
static const float SMALLEST_DIRECTION = 1.0e-6;
static const float SHORTEST_SPIN_SECONDS = 1.0e-6;

// The independent random numbers each voxel draws: NeuronCore's HashStream.
static const uint HASH_JITTER_HEIGHT = 0;
static const uint HASH_JITTER_ANGLE = 1;
static const uint HASH_SPEED_VARIATION = 2;
static const uint HASH_SPIN_AXES = 3;
static const uint HASH_FIRST_SPIN = 4;
static const uint HASH_SECOND_SPIN = 5;
static const uint HASH_STREAMS = 8;

struct VoxelPose
{
  float3 center;
  float3 axisX;
  float3 axisY;
  float3 axisZ;
};

// A number in [0, 1) from the top 24 bits of the hash, which a float holds exactly.
float HashUnit(uint _voxel, uint _stream)
{
  return float(PcgHash(_voxel * HASH_STREAMS + _stream) >> 8u) * (1.0 / 16777216.0);
}

// How long a voxel at _height, moving up at _verticalSpeed, takes to come down to _contactHeight: the later root of the
// flight's quadratic.
float FlightTime(float _height, float _verticalSpeed, float _contactHeight, float _gravity)
{
  float discriminant = _verticalSpeed * _verticalSpeed + 2.0 * _gravity * (_height - _contactHeight);
  return (_verticalSpeed + sqrt(max(discriminant, 0.0))) / _gravity;
}

// Away from the blast origin, biased upward and jittered by hash, at a speed that falls off with distance. A voxel that
// starts below the bounding radius is lifted clear of it.
float3 LaunchVelocity(uint _voxel, float3 _restCenter, ExplosionConstants _explosion)
{
  float3 offset = _restCenter - _explosion.blastOrigin;
  float distance = length(offset);
  float3 away = distance > 0.0 ? offset * (1.0 / distance) : float3(0.0, 0.0, 1.0);

  // A unit vector uniform on the sphere: its height uniform in [-1, 1), its heading uniform.
  float jitterHeight = 2.0 * HashUnit(_voxel, HASH_JITTER_HEIGHT) - 1.0;
  float jitterAngle = TWO_PI * HashUnit(_voxel, HASH_JITTER_ANGLE);
  float jitterRing = sqrt(max(1.0 - jitterHeight * jitterHeight, 0.0));
  float3 jitter = float3(jitterRing * cos(jitterAngle), jitterRing * sin(jitterAngle), jitterHeight);

  float3 sum = away + float3(0.0, 0.0, _explosion.upwardBias) + jitter * _explosion.directionJitter;
  float sumLength = length(sum);
  float3 direction = sumLength > SMALLEST_DIRECTION ? sum * (1.0 / sumLength) : float3(0.0, 0.0, 1.0);
  float variation = 1.0 + _explosion.speedJitter * (2.0 * HashUnit(_voxel, HASH_SPEED_VARIATION) - 1.0);
  float speed = _explosion.launchSpeed / (1.0 + distance / _explosion.falloffDistance) * variation;

  float3 velocity = direction * speed;
  float liftHeight = VOXEL_BOUNDING_RADIUS + EXPLOSION_LIFT_CLEARANCE - _restCenter.z;
  velocity.z = max(velocity.z, sqrt(max(2.0 * _explosion.gravity * liftHeight, 0.0)));
  return velocity;
}

// The piecewise trajectory, followed from the launch at every evaluation (§12): a flight to the bounding radius, the
// bounces, and a last flight down to the rest height. The twin also keeps the contact times, which no shader needs.
struct Trajectory
{
  float3 center;          // at the time asked
  float spinStartSeconds; // when the center first stands a bounding radius above the ground
  float spinEndSeconds;   // when it last does, on the way down to rest
};

Trajectory FollowTrajectory(uint _voxel, float3 _restCenter, ExplosionConstants _explosion)
{
  float gravity = _explosion.gravity;
  float3 launch = LaunchVelocity(_voxel, _restCenter, _explosion);

  // A voxel that starts at or above the bounding radius turns from the start; a lifted one once it has risen that far,
  // which is the earlier root of the same quadratic.
  float rise = launch.z * launch.z + 2.0 * gravity * (_restCenter.z - VOXEL_BOUNDING_RADIUS);
  float spinStart = max((launch.z - sqrt(max(rise, 0.0))) / gravity, 0.0);

  float x = _restCenter.x;
  float y = _restCenter.y;
  float height = _restCenter.z;
  float velocityX = launch.x;
  float velocityY = launch.y;
  float velocityZ = launch.z;
  float flightStart = 0.0;
  float3 center = float3(0.0, 0.0, 0.0);
  float spinEnd = 0.0;
  bool placed = false;
  [unroll] for (uint flight = 0; flight <= EXPLOSION_BOUNCES; ++flight)
  {
    float contactHeight = flight < EXPLOSION_BOUNCES ? VOXEL_BOUNDING_RADIUS : VOXEL_REST_HEIGHT;
    float duration = FlightTime(height, velocityZ, contactHeight, gravity);
    if (flight == EXPLOSION_BOUNCES)
    {
      spinEnd = flightStart + FlightTime(height, velocityZ, VOXEL_BOUNDING_RADIUS, gravity);
    }
    if (!placed && _explosion.timeSeconds < flightStart + duration)
    {
      float elapsed = max(_explosion.timeSeconds - flightStart, 0.0);
      center = float3(x + velocityX * elapsed, y + velocityY * elapsed, height + velocityZ * elapsed - 0.5 * gravity * elapsed * elapsed);
      placed = true;
    }

    // At the contact, the vertical velocity reflects with restitution and the horizontal one is damped.
    x += velocityX * duration;
    y += velocityY * duration;
    height = contactHeight;
    float impactSpeed = gravity * duration - velocityZ;
    velocityZ = _explosion.restitution * impactSpeed;
    velocityX *= _explosion.horizontalDamping;
    velocityY *= _explosion.horizontalDamping;
    flightStart += duration;
  }
  if (!placed)
  {
    center = float3(x, y, VOXEL_REST_HEIGHT);
  }
  Trajectory trajectory;
  trajectory.center = center;
  trajectory.spinStartSeconds = spinStart;
  trajectory.spinEndSeconds = spinEnd;
  return trajectory;
}

// cos and sin of _quarterTurns quarter turns. The whole turns are taken exactly and only the rest goes through cos and
// sin, so that a whole number of quarter turns, at rest, gives exactly 0 and ±1.
void QuarterTurnCosSin(float _quarterTurns, out float _cos, out float _sin)
{
  float whole = floor(_quarterTurns + 0.5);
  float angle = (_quarterTurns - whole) * HALF_PI;
  float cosRest = cos(angle);
  float sinRest = sin(angle);
  uint quadrant = asuint(int(whole)) & 3u;
  _cos = quadrant == 0u ? cosRest : (quadrant == 1u ? -sinRest : (quadrant == 2u ? -cosRest : sinRest));
  _sin = quadrant == 0u ? sinRest : (quadrant == 1u ? cosRest : (quadrant == 2u ? -sinRest : -cosRest));
}

// _vector turned about coordinate axis _axis (0, 1 or 2 for x, y or z) by the angle whose cos and sin are given.
float3 RotateAboutAxis(uint _axis, float _cos, float _sin, float3 _vector)
{
  if (_axis == 0u)
  {
    return float3(_vector.x, _cos * _vector.y - _sin * _vector.z, _sin * _vector.y + _cos * _vector.z);
  }
  if (_axis == 1u)
  {
    return float3(_cos * _vector.x + _sin * _vector.z, _vector.y, _cos * _vector.z - _sin * _vector.x);
  }
  return float3(_cos * _vector.x - _sin * _vector.y, _sin * _vector.x + _cos * _vector.y, _vector.z);
}

// _vector turned by the second spin and then the first: a column of the voxel's rotation when _vector is a unit axis.
float3 Spin(uint _firstAxis, float _firstCos, float _firstSin, uint _secondAxis, float _secondCos, float _secondSin, float3 _vector)
{
  return RotateAboutAxis(_firstAxis, _firstCos, _firstSin, RotateAboutAxis(_secondAxis, _secondCos, _secondSin, _vector));
}

// A spin's whole number of quarter turns: 1 to maxQuarterTurns either way, or none if that is 0.
float SpinQuarterTurns(uint _voxel, uint _stream, uint _maxQuarterTurns)
{
  if (_maxQuarterTurns == 0u)
  {
    return 0.0;
  }
  uint hash = PcgHash(_voxel * HASH_STREAMS + _stream);
  float turns = float(1u + (hash >> 1u) % _maxQuarterTurns);
  return (hash & 1u) != 0u ? -turns : turns;
}

// pose(i, t) of §12 for voxel _voxel, whose center is _restCenter while the model is intact, at _explosion.timeSeconds.
VoxelPose ExplosionPose(uint _voxel, float3 _restCenter, ExplosionConstants _explosion)
{
  Trajectory trajectory = FollowTrajectory(_voxel, _restCenter, _explosion);

  // Two different coordinate axes, each turned through a whole number of quarter turns, eased to no angular velocity,
  // while the center is at least a bounding radius above the ground (§12, Design/ADR/ADR-009).
  uint axes = PcgHash(_voxel * HASH_STREAMS + HASH_SPIN_AXES);
  uint firstAxis = axes % 3u;
  uint secondAxis = (firstAxis + 1u + (axes >> 16u) % 2u) % 3u;
  float spinSeconds = max(trajectory.spinEndSeconds - trajectory.spinStartSeconds, SHORTEST_SPIN_SECONDS);
  float progress = clamp((_explosion.timeSeconds - trajectory.spinStartSeconds) / spinSeconds, 0.0, 1.0);
  float eased = progress * (2.0 - progress);

  float firstCos = 1.0;
  float firstSin = 0.0;
  float secondCos = 1.0;
  float secondSin = 0.0;
  QuarterTurnCosSin(SpinQuarterTurns(_voxel, HASH_FIRST_SPIN, _explosion.maxQuarterTurns) * eased, firstCos, firstSin);
  QuarterTurnCosSin(SpinQuarterTurns(_voxel, HASH_SECOND_SPIN, _explosion.maxQuarterTurns) * eased, secondCos, secondSin);

  VoxelPose pose;
  pose.center = trajectory.center;
  pose.axisX = Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, float3(1.0, 0.0, 0.0));
  pose.axisY = Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, float3(0.0, 1.0, 0.0));
  pose.axisZ = Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, float3(0.0, 0.0, 1.0));
  return pose;
}
