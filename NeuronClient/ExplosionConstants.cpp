#include "pch.h"

#include "ExplosionConstants.h"

namespace NeuronClient
{

ExplosionConstants MakeExplosionConstants(const NeuronCore::ExplosionParameters& _parameters, float _timeSeconds) noexcept
{
  return {_parameters.blastOrigin, _timeSeconds,     _parameters.launchSpeed,    _parameters.falloffDistance, _parameters.directionJitter,
          _parameters.speedJitter, _parameters.drag, _parameters.maxQuarterTurns};
}

} // namespace NeuronClient
