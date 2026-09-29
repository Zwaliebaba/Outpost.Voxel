#include "pch.h"

#include "Profile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numbers>

namespace GameCore
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Int3;

// A unit cube's moment about an axis through its center, per unit of its mass: (1 + 1) / 12.
constexpr double CUBE_MOMENT = 1.0 / 6.0;

[[nodiscard]] std::array<double, 3> CenterOf(Int3 _cell) noexcept
{
  return {static_cast<double>(_cell.x) + 0.5, static_cast<double>(_cell.y) + 0.5, static_cast<double>(_cell.z) + 0.5};
}

// A point mass's moments about the three axes through _center.
void AddMoments(std::array<double, 3>& _inertia, const std::array<double, 3>& _point, const std::array<double, 3>& _center,
                double _mass) noexcept
{
  const double x = _point[0] - _center[0];
  const double y = _point[1] - _center[1];
  const double z = _point[2] - _center[2];
  _inertia[0] += _mass * (y * y + z * z);
  _inertia[1] += _mass * (x * x + z * z);
  _inertia[2] += _mass * (x * x + y * y);
}

// The torque about the vertical an engine gives by vectoring VECTORED_THRUST_SHARE of its _thrust sideways, across its
// line and within the plane, at _lever from the center of mass (Design/ADR/ADR-026). Along the ship's length, that is
// its distance fore or aft of the center; across it, its distance to port or starboard; and an engine that fires up or
// down swings its share whichever way the plane turns it most.
[[nodiscard]] double VectoredTorque(Int3 _facing, const std::array<double, 3>& _lever, float _thrust) noexcept
{
  const double share = static_cast<double>(VECTORED_THRUST_SHARE) * static_cast<double>(_thrust);
  if (_facing.z != 0)
  {
    return share * std::abs(_lever[2]);
  }
  if (_facing.x != 0)
  {
    return share * std::abs(_lever[0]);
  }
  return share * std::sqrt(_lever[0] * _lever[0] + _lever[2] * _lever[2]);
}

// A half turn from rest to rest, spinning up at _acceleration to at most _rate and down again.
[[nodiscard]] float HalfTurnSeconds(double _acceleration, double _rate) noexcept
{
  constexpr double HALF_TURN = std::numbers::pi;
  if (!(_acceleration > 0.0) || !(_rate > 0.0))
  {
    return std::numeric_limits<float>::infinity();
  }
  if (_rate * _rate / _acceleration >= HALF_TURN)
  {
    return static_cast<float>(2.0 * std::sqrt(HALF_TURN / _acceleration));
  }
  return static_cast<float>(HALF_TURN / _rate + _rate / _acceleration);
}

} // namespace

Profile ComputeProfile(const Design& _design) noexcept
{
  const SizeClassSpec& sizeClass = SizeClassOf(_design.sizeClass);
  Profile profile{};
  profile.sizeClass = _design.sizeClass;
  profile.voxels = static_cast<std::uint32_t>(_design.voxels.size());

  // Mass and its center: each voxel at its center, by its class's density, and each module at its mount's center.
  double mass = 0.0;
  double price = 0.0;
  std::array<double, 3> moment{};
  const auto addMass = [&mass, &moment](const std::array<double, 3>& _point, double _mass)
  {
    mass += _mass;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      moment[axis] += _mass * _point[axis];
    }
  };
  for (const HullVoxel& voxel : _design.voxels)
  {
    const MaterialClass materialClass = _design.spec->materials[voxel.color];
    const Material& material = MaterialOf(materialClass);
    profile.heavyVoxels += materialClass == MaterialClass::Heavy ? 1u : 0u;
    addMass(CenterOf(voxel.cell), material.density);
    price += material.priceCredits;
  }
  for (const Mount& mount : _design.mounts)
  {
    addMass(CenterOf(mount.centerCell), mount.module->mass);
    price += mount.module->priceCredits;
    profile.powerSupply += mount.module->powerSupply;
    profile.powerDraw += mount.module->powerDraw;
  }
  const std::array<double, 3> center{moment[0] / mass, moment[1] / mass, moment[2] / mass};

  // Inertia about that center: each voxel's as a unit cube's, and each module's as a point's.
  std::array<double, 3> inertia{};
  for (const HullVoxel& voxel : _design.voxels)
  {
    const double density = MaterialOf(_design.spec->materials[voxel.color]).density;
    AddMoments(inertia, CenterOf(voxel.cell), center, density);
    for (double& axis : inertia)
    {
      axis += density * CUBE_MOMENT;
    }
  }
  for (const Mount& mount : _design.mounts)
  {
    AddMoments(inertia, CenterOf(mount.centerCell), center, mount.module->mass);
  }

  // Thrust, the turn it gives, and the sensors, from the modules whose lines are clear (G38). An engine pushes the ship
  // against the way it faces.
  std::array<double, 3> thrustPositive{};
  std::array<double, 3> thrustNegative{};
  double torque = 0.0;
  for (const Mount& mount : _design.mounts)
  {
    if (WorksAlongLine(mount.kind) && !mount.lineClear)
    {
      ++profile.blockedLines;
      continue;
    }
    if (mount.kind == ModuleKind::Engine)
    {
      const Int3 facing = Facing(mount);
      const std::array<std::int32_t, 3> push{-facing.x, -facing.y, -facing.z};
      for (std::size_t axis = 0; axis < 3; ++axis)
      {
        if (push[axis] > 0)
        {
          thrustPositive[axis] += mount.module->thrust;
        }
        else if (push[axis] < 0)
        {
          thrustNegative[axis] += mount.module->thrust;
        }
      }
      const std::array<double, 3> point = CenterOf(mount.centerCell);
      torque += VectoredTorque(facing, {point[0] - center[0], point[1] - center[1], point[2] - center[2]}, mount.module->thrust);
    }
    if (mount.kind == ModuleKind::Sensor)
    {
      profile.sensorRangeUnits = std::max(profile.sensorRangeUnits, mount.module->sensorRangeUnits);
    }
  }

  const double turnAcceleration = torque / inertia[1];
  profile.mass = static_cast<float>(mass);
  profile.centerOfMass = {static_cast<float>(center[0]), static_cast<float>(center[1]), static_cast<float>(center[2])};
  profile.inertia = {static_cast<float>(inertia[0]), static_cast<float>(inertia[1]), static_cast<float>(inertia[2])};
  profile.thrustPositive = {static_cast<float>(thrustPositive[0]), static_cast<float>(thrustPositive[1]),
                            static_cast<float>(thrustPositive[2])};
  profile.thrustNegative = {static_cast<float>(thrustNegative[0]), static_cast<float>(thrustNegative[1]),
                            static_cast<float>(thrustNegative[2])};
  profile.accelerationUnitsPerSecondSquared = static_cast<float>(thrustPositive[2] / mass);
  profile.speedUnitsPerSecond = sizeClass.speedCapUnitsPerSecond;
  profile.turnAccelerationRadiansPerSecondSquared = static_cast<float>(turnAcceleration);
  profile.turnRateRadiansPerSecond = sizeClass.turnRateCapRadiansPerSecond;
  profile.halfTurnSeconds = HalfTurnSeconds(turnAcceleration, sizeClass.turnRateCapRadiansPerSecond);
  profile.priceCredits = static_cast<float>(price);
  profile.buildSeconds = static_cast<float>(price / BUILD_CREDITS_PER_SECOND);
  profile.commandPoints = sizeClass.commandPoints;
  return profile;
}

} // namespace GameCore
