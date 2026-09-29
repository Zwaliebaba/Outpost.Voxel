#include "pch.h"

#include "SkyConstants.h"

namespace NeuronClient
{

SkyConstants MakeSkyConstants(const NeuronCore::SkyParameters& _sky) noexcept
{
  return {_sky.toSun,        _sky.sunAngularRadiusRadians,
          _sky.sunRadiance,  _sky.seed,
          _sky.galaxy.axisX, _sky.galaxyGain,
          _sky.galaxy.axisY, _sky.starGain,
          _sky.galaxy.axisZ};
}

} // namespace NeuronClient
