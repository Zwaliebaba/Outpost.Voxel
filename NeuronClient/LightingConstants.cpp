#include "pch.h"

#include "LightingConstants.h"

namespace NeuronClient
{

LightingConstants MakeLightingConstants(const NeuronCore::LightingParameters& _lighting,
                                        const NeuronCore::OrthographicView& _shadowView) noexcept
{
  return {_lighting.toSun,        NeuronCore::ShadowNormalOffset(_shadowView),
          _lighting.sunRadiance,  _lighting.emissiveGain,
          _lighting.skyColor,     _lighting.skyIntensity,
          _lighting.groundAlbedo, _lighting.groundVisible ? 1u : 0u,
          _lighting.background};
}

} // namespace NeuronClient
