#include "pch.h"

#include "Scene.h"

#include "VoxelRecord.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace GameLib
{

Scene LoadScene(const std::filesystem::path& _path)
{
  std::expected<NeuronCore::VoxModel, NeuronCore::VoxError> loaded = NeuronCore::LoadVoxModel(_path);
  if (!loaded)
  {
    throw std::runtime_error(_path.string() + " was refused: " + NeuronCore::VoxErrorName(loaded.error()));
  }

  // The box around every voxel, each voxel the unit cube at its minimum corner.
  constexpr std::int32_t NONE = std::numeric_limits<std::int32_t>::max();
  NeuronCore::Int3 lower{NONE, NONE, NONE};
  NeuronCore::Int3 upper{-NONE, -NONE, -NONE};
  for (const NeuronCore::ModelInstance& instance : loaded->instances)
  {
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(loaded->records[instance.firstRecord + i]);
      const NeuronCore::Int3 corner = instance.origin + NeuronCore::Int3{voxel.x, voxel.y, voxel.z};
      lower = {std::min(lower.x, corner.x), std::min(lower.y, corner.y), std::min(lower.z, corner.z)};
      upper = {std::max(upper.x, corner.x + 1), std::max(upper.y, corner.y + 1), std::max(upper.z, corner.z + 1)};
    }
  }
  if (lower.x > upper.x)
  {
    throw std::runtime_error(_path.string() + " holds no visible voxel");
  }
  const NeuronCore::Float3 minimum{static_cast<float>(lower.x), static_cast<float>(lower.y), static_cast<float>(lower.z)};
  const NeuronCore::Float3 maximum{static_cast<float>(upper.x), static_cast<float>(upper.y), static_cast<float>(upper.z)};
  const NeuronCore::Float3 center = (minimum + maximum) * 0.5f;
  return {std::move(*loaded), center, NeuronCore::Length(maximum - center)};
}

} // namespace GameLib
