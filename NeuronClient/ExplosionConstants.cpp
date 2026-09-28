#include "pch.h"

#include "ExplosionConstants.h"

namespace NeuronClient
{

ExplosionConstants MakeExplosionConstants(const NeuronCore::Placement& _placement) noexcept
{
  if (!_placement.detonation.has_value())
  {
    return {};
  }
  const NeuronCore::ExplosionParameters& parameters = _placement.detonation->parameters;
  return {.blastOrigin = parameters.blastOrigin,
          .timeSeconds = _placement.detonation->timeSeconds,
          .inheritedVelocity = parameters.inheritedVelocity,
          .launchSpeed = parameters.launchSpeed,
          .falloffDistance = parameters.falloffDistance,
          .directionJitter = parameters.directionJitter,
          .speedJitter = parameters.speedJitter,
          .drag = parameters.drag,
          .maxQuarterTurns = parameters.maxQuarterTurns,
          .seed = parameters.seed,
          .hashBase = _placement.hashBase};
}

} // namespace NeuronClient
