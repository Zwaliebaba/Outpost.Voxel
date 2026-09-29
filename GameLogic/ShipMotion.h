#pragma once

#include "Route.h"

#include "Float3.h"
#include "RigidTransform.h"

#include <cstddef>

namespace GameLogic
{

// What a class of ship can do (Design/Archive/SpaceScene.md §5.3, Design/ADR/ADR-017). The speeds are in units a second, the
// acceleration in units a second squared, and the rates in radians a second.
struct ShipClass
{
  float radius;       // its model's sphere, about the center of its occupied box
  float cruiseSpeed;  // what a leader flies at
  float minSpeed;     // what a wingman may slow to, letting its slot come up to it
  float maxSpeed;     // what a wingman may reach, catching up with its slot
  float acceleration; // how fast its speed changes, either way
  float turnRate;     // how fast its heading turns
  float bankLimit;    // radians, either way
  float bankRate;     // how fast it rolls about its heading, to bank or to level out
};

// Pure pursuit looks this far ahead, in seconds of travel (§5.3).
inline constexpr float LOOK_AHEAD_SECONDS = 1.5f;

// The lateral acceleration a ship banks to 45 degrees at, in units a second squared. Space has no gravity to balance a
// bank against; this stands in for it, so that a ship leans into a turn as its turn is tight.
inline constexpr float BANK_REFERENCE_ACCELERATION = 10.0f;

// How hard a wingman closes on its slot: speed added per unit it lags, a second.
inline constexpr float SLOT_GAIN = 0.5f;

// How many wingmen a flight has at most, each with a slot of its own.
inline constexpr std::size_t MAX_WINGMEN = 3;

// A ship's flight: where it is, its heading and its up, its speed, and how far along its flight's route it lies.
struct ShipMotion
{
  NeuronCore::Float3 position; // the center of its model's occupied box
  NeuronCore::Float3 forward;  // unit
  NeuronCore::Float3 up;       // unit, square to forward: banked into a turn
  float speed;
  float progress;
};

// The ship's rotation, from model space, where +Z is forward and +Y up (Design/Archive/NeuronVoxelFormat.md §12), into the
// world.
[[nodiscard]] NeuronCore::Rotation ShipRotation(const ShipMotion& _motion) noexcept;

// Slot _wingman of a flight, in its leader's frame, in units: right, up and forward. The slots stand back from the
// leader, to either side and then behind, a ship's width apart.
[[nodiscard]] NeuronCore::Float3 FormationSlot(std::size_t _wingman, const ShipClass& _class) noexcept;

// The up a ship heading along _forward at _speed settles on at _progress along _route: the route's reference up there,
// banked for the route's curve. A flight starts so, and its first tick needs no roll.
[[nodiscard]] NeuronCore::Float3 SteadyUp(const Route& _route, float _progress, NeuronCore::Float3 _forward, float _speed,
                                          const ShipClass& _class) noexcept;

// The slot's place in the world, for a leader that flies _leader.
[[nodiscard]] NeuronCore::Float3 SlotPosition(const ShipMotion& _leader, NeuronCore::Float3 _slot) noexcept;

// One tick, _seconds long, of a leader on _route: pure pursuit of the point LOOK_AHEAD_SECONDS of travel ahead, at its
// class's cruise, rolling toward the route's reference up banked into its turn (§5.3).
void FlyLeader(ShipMotion& _motion, const Route& _route, const ShipClass& _class, float _seconds) noexcept;

// One tick of a wingman whose slot is _slot in its leader's frame, for a leader that has flown this tick from _before to
// _after. It flies _route as its leader does, aiming a look-ahead ahead of itself but to its slot's side of the route,
// at its slot's own speed, faster as the slot lies ahead of it along the route and slower as it lies behind. So it
// closes on its slot along the route and never across it, however far it has to come (Design/ADR/ADR-017).
void FlyWingman(ShipMotion& _motion, const ShipMotion& _before, const ShipMotion& _after, NeuronCore::Float3 _slot, const Route& _route,
                const ShipClass& _class, float _seconds) noexcept;

} // namespace GameLogic
