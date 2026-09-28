#include "pch.h"

#include "LightingConstants.h"

namespace NeuronClient
{

LightingConstants MakeLightingConstants(const NeuronCore::LightingParameters& _lighting, const NeuronCore::OrthographicView& _shadowView,
                                        std::uint32_t _placementCount) noexcept
{
  return {_lighting.toSun,       NeuronCore::ShadowNormalOffset(_shadowView),
          _lighting.sunRadiance, _lighting.emissiveGain,
          _lighting.skyColor,    _lighting.skyIntensity,
          _lighting.groundColor, _placementCount,
          _lighting.background};
}

} // namespace NeuronClient
