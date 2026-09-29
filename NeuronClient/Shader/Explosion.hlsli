#pragma once

// pose(i, t) of the detonation (Design/Archive/SpaceScene.md §5.5, §7.7, Design/ADR/ADR-014, ADR-024): where a voxel is, and how
// it is turned, some time after the detonation, as part of its fragment. The C++ twin is NeuronCore/Explosion.h and .cpp
// (R15), function for function; the envelope and the breaking into fragments are the CPU's alone, since no shader needs
// them.

#include "ExplosionConstants.hlsli"
#include "Fragment.hlsli"
#include "Hash.hlsli"

static const float TWO_PI = 6.28318531;
static const float SMALLEST_DIRECTION = 1.0e-6;
static const float SPEED_SPREAD_BOUND = 3.0;

// The independent random numbers each fragment draws: NeuronCore's HashStream.
static const uint HASH_JITTER_HEIGHT = 0;
static const uint HASH_JITTER_ANGLE = 1;
static const uint HASH_FIRST_SPEED = 2;
static const uint HASH_SECOND_SPEED = 3;
static const uint HASH_THIRD_SPEED = 4;
static const uint HASH_SPIN_HEIGHT = 5;
static const uint HASH_SPIN_ANGLE = 6;
static const uint HASH_SPIN_AMOUNT = 7;
static const uint HASH_STREAMS = 8;

// How far apart two seeds put a fragment's hash inputs: 2^32 over the golden ratio. Seed 0 adds nothing.
static const uint SEED_STEP = 0x9E3779B9u;

struct VoxelPose
{
  float3 center;
  float3 axisX;
  float3 axisY;
  float3 axisZ;
};

// Fragment _fragment's random bits for _stream in the detonation with _seed.
uint FragmentHash(uint _fragment, uint _stream, uint _seed)
{
  return PcgHash(_fragment * HASH_STREAMS + _stream + _seed * SEED_STEP);
}

// A number in [0, 1) from the top 24 bits of the hash, which a float holds exactly.
float HashUnit(uint _fragment, uint _stream, uint _seed)
{
  return float(FragmentHash(_fragment, _stream, _seed) >> 8u) * (1.0 / 16777216.0);
}

// A unit vector uniform on the sphere: its y uniform in [-1, 1), its heading about the y axis uniform.
float3 UnitVector(uint _fragment, uint _height, uint _angle, uint _seed)
{
  float height = 2.0 * HashUnit(_fragment, _height, _seed) - 1.0;
  float angle = TWO_PI * HashUnit(_fragment, _angle, _seed);
  float ring = sqrt(max(1.0 - height * height, 0.0));
  return float3(ring * cos(angle), height, ring * sin(angle));
}

// e^(speedSpread z), z = 2 (u1 + u2 + u3) - 3.
float SpeedVariation(uint _fragment, ExplosionConstants _explosion)
{
  float sum = HashUnit(_fragment, HASH_FIRST_SPEED, _explosion.seed) + HashUnit(_fragment, HASH_SECOND_SPEED, _explosion.seed) +
              HashUnit(_fragment, HASH_THIRD_SPEED, _explosion.seed);
  return exp(_explosion.speedSpread * (2.0 * sum - SPEED_SPREAD_BOUND));
}

// Away from the blast origin from the fragment's pivot, jittered by hash, at a speed that falls off with distance, varies
// by hash and falls with the fragment's size. A fragment whose pivot is the origin itself flies along its jitter.
float3 LaunchVelocity(uint _fragment, Fragment _shape, ExplosionConstants _explosion)
{
  float3 offset = _shape.pivot - _explosion.blastOrigin;
  float distance = length(offset);
  float3 away = distance > 0.0 ? offset * (1.0 / distance) : float3(0.0, 0.0, 0.0);
  float3 jitter = UnitVector(_fragment, HASH_JITTER_HEIGHT, HASH_JITTER_ANGLE, _explosion.seed);
  float3 sum = away + jitter * _explosion.directionJitter;
  float sumLength = length(sum);
  float3 direction = sumLength > SMALLEST_DIRECTION ? sum * (1.0 / sumLength) : jitter;
  float speed =
    _explosion.launchSpeed / (1.0 + distance / _explosion.falloffDistance) * SpeedVariation(_fragment, _explosion) * _shape.sizeScale;
  return direction * speed;
}

// A fragment's drag: the lone voxel's, times the square root of its size scale, and never below the least.
float FragmentDrag(Fragment _shape, ExplosionConstants _explosion)
{
  return max(_explosion.drag * sqrt(_shape.sizeScale), _explosion.minDrag);
}

// How much of its motion a drag of _drag has let a fragment make by _timeSeconds after it started: 1 - e^(-drag t), from
// 0 at its start towards 1. Before its start, and at it, exactly 0 whatever exp makes of it, so that the intact model is
// exact.
float MotionMade(float _drag, float _timeSeconds)
{
  return _timeSeconds > 0.0 ? 1.0 - exp(-_drag * _timeSeconds) : 0.0;
}

// _vector turned about the unit _axis by the angle whose cos and sin are given (Rodrigues). An angle of exactly 0, cos 1
// and sin 0, gives _vector exactly.
float3 Turn(float3 _axis, float _cos, float _sin, float3 _vector)
{
  return _vector * _cos + cross(_axis, _vector) * _sin + _axis * (dot(_axis, _vector) * (1.0 - _cos));
}

// pose(i, t) of §5.5 for a voxel whose center is _restCenter while the model is intact, of fragment _fragment, the model's
// index of it, whose pivot and size are _shape, at _explosion.timeSeconds.
VoxelPose ExplosionPose(uint _fragment, Fragment _shape, float3 _restCenter, ExplosionConstants _explosion)
{
  // The inherited velocity carries every fragment alike, under the lone voxel's drag, from the detonation on.
  float3 carried = _explosion.inheritedVelocity * (MotionMade(_explosion.drag, _explosion.timeSeconds) / _explosion.drag);

  // The fragment starts once the blast has crossed the model to its pivot, and its pivot has moved the launch velocity
  // times (1 - e^(-drag t)) / drag under its own drag.
  float delay = length(_shape.pivot - _explosion.blastOrigin) / _explosion.shockSpeed;
  float drag = FragmentDrag(_shape, _explosion);
  float made = MotionMade(drag, _explosion.timeSeconds - delay);
  float3 flown = LaunchVelocity(_fragment, _shape, _explosion) * (made / drag);

  // The fragment turns about its hashed axis through its pivot, as the drag lets its motion run.
  float angle = _explosion.maxSpinRadians * HashUnit(_fragment, HASH_SPIN_AMOUNT, _explosion.seed) * _shape.sizeScale * made;
  float3 axis = UnitVector(_fragment, HASH_SPIN_HEIGHT, HASH_SPIN_ANGLE, _explosion.seed);
  float cosAngle = cos(angle);
  float sinAngle = sin(angle);
  float3 offset = _restCenter - _shape.pivot;

  VoxelPose pose;
  pose.center = _restCenter + (Turn(axis, cosAngle, sinAngle, offset) - offset) + flown + carried;
  pose.axisX = Turn(axis, cosAngle, sinAngle, float3(1.0, 0.0, 0.0));
  pose.axisY = Turn(axis, cosAngle, sinAngle, float3(0.0, 1.0, 0.0));
  pose.axisZ = Turn(axis, cosAngle, sinAngle, float3(0.0, 0.0, 1.0));
  return pose;
}
