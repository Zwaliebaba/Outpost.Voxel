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
          .speedSpread = parameters.speedSpread,
          .drag = parameters.drag,
          .minDrag = parameters.minDrag,
          .maxSpinRadians = parameters.maxSpinRadians,
          .shockSpeed = parameters.shockSpeed,
          .seed = parameters.seed,
          .firstFragment = _placement.detonation->fragments.firstFragment};
}

} // namespace NeuronClient
