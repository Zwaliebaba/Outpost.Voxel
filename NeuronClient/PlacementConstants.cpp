#include "pch.h"

#include "PlacementConstants.h"

namespace NeuronClient
{

PlacementConstants MakePlacementConstants(const NeuronCore::Placement& _placement) noexcept
{
  const NeuronCore::RigidTransform& transform = _placement.transform;
  return {transform.rotation.axisX, _placement.firstRecord, transform.rotation.axisY, _placement.recordCount,
          transform.rotation.axisZ, _placement.firstVoxel,  transform.translation,    _placement.paletteIndex};
}

} // namespace NeuronClient
