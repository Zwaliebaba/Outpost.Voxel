#include "pch.h"

#include "ShipMotion.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace GameLogic
{
namespace
{

using NeuronCore::Float3;

constexpr Float3 WORLD_UP{0.0f, 1.0f, 0.0f};

// _vector made square to _forward and of unit length, or false when it runs along _forward.
[[nodiscard]] bool SquareTo(Float3 _forward, Float3 _vector, Float3& _square) noexcept
{
  const Float3 square = _vector - _forward * NeuronCore::Dot(_vector, _forward);
  const float length = NeuronCore::Length(square);
  if (length < 1.0e-3f)
  {
    return false;
  }
  _square = square * (1.0f / length);
  return true;
}

// _forward turned toward _desired by at most _maxAngle radians, about the axis square to both, with _turned the angle
// it turned. The angle comes from the sine and cosine together, since the cosine alone loses it at the few tenths of a
// degree a capital ship turns in a tick. A heading straight away from the desired one turns about any axis square to it.
[[nodiscard]] Float3 TurnToward(Float3 _forward, Float3 _desired, float _maxAngle, float& _turned) noexcept
{
  Float3 axis = NeuronCore::Cross(_forward, _desired);
  const float angle = std::atan2(NeuronCore::Length(axis), NeuronCore::Dot(_forward, _desired));
  if (angle <= _maxAngle)
  {
    _turned = angle;
    return _desired;
  }
  if (NeuronCore::Length(axis) < 1.0e-6f)
  {
    axis = NeuronCore::Cross(_forward, WORLD_UP);
    if (NeuronCore::Length(axis) < 1.0e-3f)
    {
      axis = NeuronCore::Cross(_forward, Float3{1.0f, 0.0f, 0.0f});
    }
  }
  axis = NeuronCore::Normalize(axis);
  _turned = _maxAngle;
  return NeuronCore::Normalize(_forward * std::cos(_maxAngle) + NeuronCore::Cross(axis, _forward) * std::sin(_maxAngle));
}

// The first half of a tick: the heading turned toward _desired by at most the class's turn rate, and the speed changed
// toward _desiredSpeed by at most its acceleration. Returns the angle the heading turned.
[[nodiscard]] float TurnAndAccelerate(ShipMotion& _motion, Float3 _desired, float _desiredSpeed, const ShipClass& _class,
                                      float _seconds) noexcept
{
  float turned = 0.0f;
  _motion.forward = TurnToward(_motion.forward, _desired, _class.turnRate * _seconds, turned);
  const float change = std::clamp(_desiredSpeed - _motion.speed, -_class.acceleration * _seconds, _class.acceleration * _seconds);
  _motion.speed = std::clamp(_motion.speed + change, 0.0f, _class.maxSpeed);
  return turned;
}

// The up a ship rolls toward after turning from _before by _turned radians: _reference made level to its heading, and
// banked into the turn by the angle its lateral acceleration calls for, within the class's limit.
[[nodiscard]] Float3 BankedUp(const ShipMotion& _motion, Float3 _before, float _turned, Float3 _reference, const ShipClass& _class,
                              float _seconds) noexcept
{
  Float3 level{};
  if (!SquareTo(_motion.forward, _reference, level))
  {
    return _motion.up;
  }
  const Float3 right = NeuronCore::Cross(level, _motion.forward);
  const Float3 swing = _motion.forward - _before;
  const float swingLength = NeuronCore::Length(swing);
  const float side = swingLength > 0.0f ? NeuronCore::Dot(swing, right) / swingLength : 0.0f;
  const float lateral = _motion.speed * (_turned / _seconds) * side;
  const float bank = std::clamp(std::atan2(lateral, BANK_REFERENCE_ACCELERATION), -_class.bankLimit, _class.bankLimit);
  return level * std::cos(bank) + right * std::sin(bank);
}

// The second half of a tick: the up kept square to the new heading and rolled about it toward _target by at most the
// class's bank rate, whether it banks or levels out, and the ship moved along its heading.
void RollAndMove(ShipMotion& _motion, Float3 _target, const ShipClass& _class, float _seconds) noexcept
{
  Float3 up{};
  if (!SquareTo(_motion.forward, _motion.up, up) && !SquareTo(_motion.forward, _target, up) && !SquareTo(_motion.forward, WORLD_UP, up))
  {
    static_cast<void>(SquareTo(_motion.forward, Float3{1.0f, 0.0f, 0.0f}, up));
  }
  Float3 target{};
  if (SquareTo(_motion.forward, _target, target))
  {
    const float angle = std::atan2(NeuronCore::Dot(NeuronCore::Cross(up, target), _motion.forward), NeuronCore::Dot(up, target));
    const float roll = std::clamp(angle, -_class.bankRate * _seconds, _class.bankRate * _seconds);
    up = NeuronCore::Normalize(up * std::cos(roll) + NeuronCore::Cross(_motion.forward, up) * std::sin(roll));
  }
  _motion.up = up;
  _motion.position = _motion.position + _motion.forward * (_motion.speed * _seconds);
}

// How far ahead pure pursuit looks, for a ship at _speed: LOOK_AHEAD_SECONDS of travel, and never less than at half
// its class's cruise, so that a slow ship still aims along its route.
[[nodiscard]] float LookAhead(float _speed, const ShipClass& _class) noexcept
{
  return LOOK_AHEAD_SECONDS * std::max(_speed, 0.5f * _class.cruiseSpeed);
}

// Where to aim: _target, or straight on when the target is where the ship already is.
[[nodiscard]] Float3 Heading(const ShipMotion& _motion, Float3 _target) noexcept
{
  const Float3 toward = _target - _motion.position;
  return NeuronCore::Length(toward) > 1.0e-3f ? NeuronCore::Normalize(toward) : _motion.forward;
}

// One tick of flight toward _aim at _desiredSpeed, banking from _reference.
void FlyToward(ShipMotion& _motion, Float3 _aim, float _desiredSpeed, Float3 _reference, const ShipClass& _class, float _seconds) noexcept
{
  const Float3 before = _motion.forward;
  const float turned = TurnAndAccelerate(_motion, Heading(_motion, _aim), _desiredSpeed, _class, _seconds);
  RollAndMove(_motion, BankedUp(_motion, before, turned, _reference, _class, _seconds), _class, _seconds);
}

// How far aim points lie ahead of a ship on the plane: any length would do, since only their direction counts.
constexpr float PLANE_AIM_UNITS = 100.0f;

// A halted ship whose up lies within a thousandth of a radian of the world's is level.
constexpr float LEVEL_COSINE = 0.9999995f;

// _vector on the plane: its y dropped.
[[nodiscard]] Float3 Flat(Float3 _vector) noexcept
{
  return {_vector.x, 0.0f, _vector.z};
}

[[nodiscard]] float PlaneDistance(Float3 _from, Float3 _to) noexcept
{
  return NeuronCore::Length(Flat(_to - _from));
}

// _vector on the plane at unit length, or _fallback when it has next to none.
[[nodiscard]] Float3 PlaneDirection(Float3 _vector, Float3 _fallback) noexcept
{
  const Float3 flat = Flat(_vector);
  const float length = NeuronCore::Length(flat);
  return length > 1.0e-3f ? flat * (1.0f / length) : _fallback;
}

// A heading on the plane toward _desired from _forward: _desired itself, or the ship's right when _desired lies straight
// behind it, which TurnToward would otherwise turn about a level axis, out of the plane.
[[nodiscard]] Float3 PlaneTurn(Float3 _forward, Float3 _desired) noexcept
{
  if (NeuronCore::Length(NeuronCore::Cross(_forward, _desired)) < 1.0e-3f && NeuronCore::Dot(_forward, _desired) < 0.0f)
  {
    return NeuronCore::Normalize(NeuronCore::Cross(WORLD_UP, _forward));
  }
  return _desired;
}

// Whether a ship at _position is done with corner _corner of _path: within CORNER_REACHED of it, or past the line through
// it that halves its turn.
[[nodiscard]] bool CornerDone(Float3 _position, std::span<const Float3> _path, std::size_t _corner) noexcept
{
  if (PlaneDistance(_position, _path[_corner]) <= CORNER_REACHED)
  {
    return true;
  }
  const Float3 in = PlaneDirection(_path[_corner] - _path[_corner - 1], Float3{});
  const Float3 out = PlaneDirection(_path[_corner + 1] - _path[_corner], Float3{});
  return NeuronCore::Dot(in, out) > 0.0f && NeuronCore::Dot(Flat(_position - _path[_corner]), in + out) > 0.0f;
}

// The fastest a ship may take corner _corner of _path: the speed whose turning circle, at its turn rate, swings it out of
// the corner's turn by CORNER_SWING, and no limit for a corner that hardly turns.
[[nodiscard]] float CornerSpeed(std::span<const Float3> _path, std::size_t _corner, const ShipClass& _class) noexcept
{
  const Float3 in = PlaneDirection(_path[_corner] - _path[_corner - 1], Float3{});
  const Float3 out = PlaneDirection(_path[_corner + 1] - _path[_corner], Float3{});
  const float bend = 1.0f - std::clamp(NeuronCore::Dot(in, out), -1.0f, 1.0f);
  return bend > 1.0e-6f ? _class.turnRate * CORNER_SWING / bend : _class.maxSpeed;
}

} // namespace

NeuronCore::Rotation ShipRotation(const ShipMotion& _motion) noexcept
{
  return {NeuronCore::Cross(_motion.up, _motion.forward), _motion.up, _motion.forward};
}

Float3 FormationSlot(std::size_t _wingman, const ShipClass& _class) noexcept
{
  // Right, up and forward, in ship widths: back to the right, back to the left, and behind.
  constexpr std::array<Float3, MAX_WINGMEN> SLOTS{{{1.2f, 0.0f, -1.2f}, {-1.2f, 0.0f, -1.2f}, {0.0f, 0.0f, -2.4f}}};
  return SLOTS[std::min(_wingman, MAX_WINGMEN - 1)] * (2.0f * _class.radius);
}

Float3 SteadyUp(const Route& _route, float _progress, Float3 _forward, float _speed, const ShipClass& _class) noexcept
{
  // The route's bend, from three of its points a step apart: long enough that float positions some thousands of units
  // out resolve the bend of the widest orbit, and short against its radius.
  constexpr float STEP = 20.0f;
  const Float3 bend = (_route.PositionAt(_progress + STEP) - _route.PositionAt(_progress) * 2.0f + _route.PositionAt(_progress - STEP)) *
                      (1.0f / (STEP * STEP));
  Float3 level{};
  if (!SquareTo(_forward, _route.ReferenceUpAt(_progress), level) && !SquareTo(_forward, WORLD_UP, level))
  {
    static_cast<void>(SquareTo(_forward, Float3{1.0f, 0.0f, 0.0f}, level));
  }
  const Float3 right = NeuronCore::Cross(level, _forward);
  const float lateral = _speed * _speed * NeuronCore::Dot(bend, right);
  const float bank = std::clamp(std::atan2(lateral, BANK_REFERENCE_ACCELERATION), -_class.bankLimit, _class.bankLimit);
  return level * std::cos(bank) + right * std::sin(bank);
}

Float3 SlotPosition(const ShipMotion& _leader, Float3 _slot) noexcept
{
  return _leader.position + NeuronCore::RotateVector(ShipRotation(_leader), _slot);
}

void FlyLeader(ShipMotion& _motion, const Route& _route, const ShipClass& _class, float _seconds) noexcept
{
  _motion.progress = _route.Track(_motion.progress, _motion.position);
  const Float3 aim = _route.PositionAt(_motion.progress + LookAhead(_motion.speed, _class));
  FlyToward(_motion, aim, _class.cruiseSpeed, _route.ReferenceUpAt(_motion.progress), _class, _seconds);
}

float RemainingLength(Float3 _position, std::span<const Float3> _path, std::size_t _next) noexcept
{
  float length = PlaneDistance(_position, _path[_next]);
  for (std::size_t point = _next + 1; point < _path.size(); ++point)
  {
    length += PlaneDistance(_path[point - 1], _path[point]);
  }
  return length;
}

std::size_t FlyPath(ShipMotion& _motion, std::span<const Float3> _path, std::size_t _next, float _pace, Float3 _avoid,
                    const ShipClass& _class, float _seconds) noexcept
{
  const std::size_t last = _path.size() - 1;
  while (_next < last && CornerDone(_motion.position, _path, _next))
  {
    ++_next;
  }

  // The speed it can still slow from, at BRAKING_SHARE of its acceleration, to each corner's speed and to a halt at the
  // destination.
  const float braking = 2.0f * BRAKING_SHARE * _class.acceleration;
  float speed = _pace;
  float ahead = PlaneDistance(_motion.position, _path[_next]);
  for (std::size_t corner = _next; corner < last; ++corner)
  {
    const float cornerSpeed = CornerSpeed(_path, corner, _class);
    speed = std::min(speed, std::sqrt(cornerSpeed * cornerSpeed + braking * ahead));
    ahead += PlaneDistance(_path[corner], _path[corner + 1]);
  }
  speed = std::min(speed, std::sqrt(braking * ahead));

  const Float3 toward = PlaneDirection(_path[_next] - _motion.position, _motion.forward);
  const Float3 aim = PlaneTurn(_motion.forward, PlaneDirection(toward + Flat(_avoid), toward));
  const float facing = std::max(NeuronCore::Dot(_motion.forward, aim), 0.0f);
  FlyToward(_motion, _motion.position + aim * PLANE_AIM_UNITS, speed * facing * facing, WORLD_UP, _class, _seconds);
  return _next;
}

void Brake(ShipMotion& _motion, const ShipClass& _class, float _seconds) noexcept
{
  FlyToward(_motion, _motion.position + _motion.forward * PLANE_AIM_UNITS, 0.0f, WORLD_UP, _class, _seconds);
  if (_motion.speed <= 0.0f && NeuronCore::Dot(_motion.up, WORLD_UP) >= LEVEL_COSINE)
  {
    _motion.up = WORLD_UP;
  }
}

void FlyWingman(ShipMotion& _motion, const ShipMotion& _before, const ShipMotion& _after, Float3 _slot, const Route& _route,
                const ShipClass& _class, float _seconds) noexcept
{
  _motion.progress = _route.Track(_motion.progress, _motion.position);

  // The slot's own speed along its leader's heading, which on a curve is more than the leader's outside the turn and
  // less inside it, and the slot's lead along the route.
  const float slotSpeed = NeuronCore::Dot(SlotPosition(_after, _slot) - SlotPosition(_before, _slot), _after.forward) / _seconds;
  const float lag = _route.Separation(_motion.progress, _after.progress + _slot.z);
  const float speed = std::clamp(slotSpeed + SLOT_GAIN * lag, _class.minSpeed, _class.maxSpeed);

  // The aim: the route a look-ahead on, moved to the slot's side in the leader's frame, turned to the route's heading
  // there.
  const float ahead = _motion.progress + LookAhead(_motion.speed, _class);
  const Float3 heading = _route.DirectionAt(ahead);
  Float3 right{};
  if (!SquareTo(heading, ShipRotation(_after).axisX, right) &&
      !SquareTo(heading, NeuronCore::Cross(_route.ReferenceUpAt(ahead), heading), right))
  {
    right = NeuronCore::Cross(WORLD_UP, heading);
  }
  const Float3 aim = _route.PositionAt(ahead) + right * _slot.x + NeuronCore::Cross(heading, right) * _slot.y;
  FlyToward(_motion, aim, speed, _route.ReferenceUpAt(_motion.progress), _class, _seconds);
}

} // namespace GameLogic
