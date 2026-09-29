#pragma once

#include "Catalogue.h"
#include "Design.h"

#include "Float3.h"

#include <cstdint>

namespace GameCore
{

// What a design can do, from its voxels, materials and modules (G29, Design/GameConcept.md §5.4), over the MVP's subset
// of it (Design/MvpPlan.md §2.2). Flight takes its limits from here, and the opponent and the HUD read it. Masses are in
// the mass of a light voxel, and positions in the model's space, in voxels.
struct Profile
{
  SizeClass sizeClass;
  std::uint32_t voxels;
  std::uint32_t heavyVoxels;
  float mass;
  NeuronCore::Float3 centerOfMass;
  NeuronCore::Float3 inertia; // the moments about the center of mass, along the model's axes

  // The thrust of the engines whose lines are clear (G38), along +X, +Y and +Z, and along -X, -Y and -Z as magnitudes.
  NeuronCore::Float3 thrustPositive;
  NeuronCore::Float3 thrustNegative;
  float accelerationUnitsPerSecondSquared; // forward, along +Z
  float speedUnitsPerSecond;               // the class's cap

  // Turning about the vertical, the one turn the MVP's plane needs: the acceleration vectored thrust gives the design's
  // inertia (Design/ADR/ADR-026), the class's cap on the rate, and a half turn from rest to rest within both.
  float turnAccelerationRadiansPerSecondSquared;
  float turnRateRadiansPerSecond;
  float halfTurnSeconds;

  float sensorRangeUnits; // the farthest-seeing sensor whose line is clear
  float powerSupply;
  float powerDraw;
  std::uint32_t blockedLines; // mounts whose modules work along lines the hull blocks, and so do nothing (G38)

  float priceCredits; // its voxels by class, plus its modules
  float buildSeconds; // its price at a shipyard's rate
  std::uint32_t commandPoints;
};

[[nodiscard]] Profile ComputeProfile(const Design& _design) noexcept;

} // namespace GameCore
