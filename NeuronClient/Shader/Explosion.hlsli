#pragma once

// pose(i, t) of the detonation (Design/SpaceScene.md §5.5, §7.7, Design/ADR/ADR-013, ADR-014): where a voxel is, and how
// it is turned, some time after the detonation. The C++ twin is NeuronCore/Explosion.h and .cpp (R15), function for
// function; the envelope is the CPU's alone, since no shader needs it.

#include "ExplosionConstants.hlsli"
#include "Hash.hlsli"

static const float TWO_PI = 6.28318531;
static const float HALF_PI = 1.57079633;
static const float SMALLEST_DIRECTION = 1.0e-6;

// The independent random numbers each voxel draws: NeuronCore's HashStream.
static const uint HASH_JITTER_HEIGHT = 0;
static const uint HASH_JITTER_ANGLE = 1;
static const uint HASH_SPEED_VARIATION = 2;
static const uint HASH_SPIN_AXES = 3;
static const uint HASH_FIRST_SPIN = 4;
static const uint HASH_SECOND_SPIN = 5;
static const uint HASH_STREAMS = 8;

// How far apart two seeds put a voxel's hash inputs: 2^32 over the golden ratio. Seed 0 adds nothing.
static const uint SEED_STEP = 0x9E3779B9u;

struct VoxelPose
{
  float3 center;
  float3 axisX;
  float3 axisY;
  float3 axisZ;
};

// Voxel _voxel's random bits for _stream in the detonation with _seed.
uint VoxelHash(uint _voxel, uint _stream, uint _seed)
{
  return PcgHash(_voxel * HASH_STREAMS + _stream + _seed * SEED_STEP);
}

// A number in [0, 1) from the top 24 bits of the hash, which a float holds exactly.
float HashUnit(uint _voxel, uint _stream, uint _seed)
{
  return float(VoxelHash(_voxel, _stream, _seed) >> 8u) * (1.0 / 16777216.0);
}

// A unit vector uniform on the sphere: its y uniform in [-1, 1), its heading about the y axis uniform.
float3 Jitter(uint _voxel, uint _seed)
{
  float height = 2.0 * HashUnit(_voxel, HASH_JITTER_HEIGHT, _seed) - 1.0;
  float angle = TWO_PI * HashUnit(_voxel, HASH_JITTER_ANGLE, _seed);
  float ring = sqrt(max(1.0 - height * height, 0.0));
  return float3(ring * cos(angle), height, ring * sin(angle));
}

// Away from the blast origin, jittered by hash, at a speed that falls off with distance, plus the inherited velocity. A
// voxel with no direction away from the origin, one at the origin itself, flies along its jitter.
float3 LaunchVelocity(uint _voxel, float3 _restCenter, ExplosionConstants _explosion)
{
  float3 offset = _restCenter - _explosion.blastOrigin;
  float distance = length(offset);
  float3 away = distance > 0.0 ? offset * (1.0 / distance) : float3(0.0, 0.0, 0.0);
  float3 jitter = Jitter(_voxel, _explosion.seed);
  float3 sum = away + jitter * _explosion.directionJitter;
  float sumLength = length(sum);
  float3 direction = sumLength > SMALLEST_DIRECTION ? sum * (1.0 / sumLength) : jitter;
  float variation = 1.0 + _explosion.speedJitter * (2.0 * HashUnit(_voxel, HASH_SPEED_VARIATION, _explosion.seed) - 1.0);
  float speed = _explosion.launchSpeed / (1.0 + distance / _explosion.falloffDistance) * variation;
  return direction * speed + _explosion.inheritedVelocity;
}

// How much of its motion the drag has let a voxel make by _timeSeconds: 1 - e^(-drag t), from 0 at the detonation
// towards 1. Time 0 gives exactly 0 whatever exp makes of it, so that the intact model is exact.
float MotionMade(float _drag, float _timeSeconds)
{
  return _timeSeconds > 0.0 ? 1.0 - exp(-_drag * _timeSeconds) : 0.0;
}

// cos and sin of _quarterTurns quarter turns. The whole turns are taken exactly and only the rest goes through cos and
// sin, so that a whole number of quarter turns gives exactly 0 and ±1.
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
float SpinQuarterTurns(uint _voxel, uint _stream, ExplosionConstants _explosion)
{
  if (_explosion.maxQuarterTurns == 0u)
  {
    return 0.0;
  }
  uint hash = VoxelHash(_voxel, _stream, _explosion.seed);
  float turns = float(1u + (hash >> 1u) % _explosion.maxQuarterTurns);
  return (hash & 1u) != 0u ? -turns : turns;
}

// pose(i, t) of §5.5 for voxel _voxel, whose center is _restCenter while the model is intact, at _explosion.timeSeconds.
VoxelPose ExplosionPose(uint _voxel, float3 _restCenter, ExplosionConstants _explosion)
{
  // Under the drag the center has moved the launch velocity times (1 - e^(-drag t)) / drag.
  float made = MotionMade(_explosion.drag, _explosion.timeSeconds);
  float3 center = _restCenter + LaunchVelocity(_voxel, _restCenter, _explosion) * (made / _explosion.drag);

  // Two different coordinate axes, each turned through its whole number of quarter turns as the drag lets the motion
  // run, so that the voxel ends square to the axes.
  uint axes = VoxelHash(_voxel, HASH_SPIN_AXES, _explosion.seed);
  uint firstAxis = axes % 3u;
  uint secondAxis = (firstAxis + 1u + (axes >> 16u) % 2u) % 3u;
  float firstCos = 1.0;
  float firstSin = 0.0;
  float secondCos = 1.0;
  float secondSin = 0.0;
  QuarterTurnCosSin(SpinQuarterTurns(_voxel, HASH_FIRST_SPIN, _explosion) * made, firstCos, firstSin);
  QuarterTurnCosSin(SpinQuarterTurns(_voxel, HASH_SECOND_SPIN, _explosion) * made, secondCos, secondSin);

  VoxelPose pose;
  pose.center = center;
  pose.axisX = Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, float3(1.0, 0.0, 0.0));
  pose.axisY = Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, float3(0.0, 1.0, 0.0));
  pose.axisZ = Spin(firstAxis, firstCos, firstSin, secondAxis, secondCos, secondSin, float3(0.0, 0.0, 1.0));
  return pose;
}
