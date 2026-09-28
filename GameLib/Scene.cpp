#include "pch.h"

#include "Scene.h"

#include "RigidTransform.h"
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
namespace
{

// The scene's detonation as _placement's part sees it: its blast origin in the part's space.
[[nodiscard]] NeuronCore::ExplosionParameters PartExplosion(const Scene& _scene, const NeuronCore::Placement& _placement) noexcept
{
  NeuronCore::ExplosionParameters parameters = _scene.explosion;
  parameters.blastOrigin = NeuronCore::InverseTransformPoint(_placement.transform, _scene.explosion.blastOrigin);
  return parameters;
}

} // namespace

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
  Scene scene{std::move(*loaded), minimum, maximum, center, NeuronCore::Length(maximum - center), {}, {}};

  // The scene's only model is model 0, its records first in the record buffer. Each part stands unturned at its origin,
  // so that its voxels' centers are where they were, exactly, and its ids are its records' indices.
  for (std::uint32_t part = 0; part < scene.model.instances.size(); ++part)
  {
    const NeuronCore::Int3 origin = scene.model.instances[part].origin;
    const NeuronCore::RigidTransform transform{NeuronCore::IDENTITY_ROTATION,
                                               {static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z)}};
    scene.placements.push_back(NeuronCore::PlacePart(scene.model, 0, 0, part, transform));
  }
  if (!NeuronCore::AssignVoxelIds(scene.placements))
  {
    throw std::runtime_error(_path.string() + " holds more voxels than a frame can name");
  }
  scene.explosion = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(scene.model));
  return scene;
}

std::vector<NeuronCore::Placement> PlacementsAt(const Scene& _scene, float _timeSeconds)
{
  std::vector<NeuronCore::Placement> placements = _scene.placements;
  if (_timeSeconds > 0.0f)
  {
    for (NeuronCore::Placement& placement : placements)
    {
      placement.detonation = NeuronCore::PlacementDetonation{PartExplosion(_scene, placement), _timeSeconds};
    }
  }
  return placements;
}

ExplosionReach BoundSceneExplosion(const Scene& _scene) noexcept
{
  ExplosionReach reach{{_scene.explosion.blastOrigin, 0.0f}, 0.0f};
  for (const NeuronCore::Placement& placement : _scene.placements)
  {
    const NeuronCore::ExplosionParameters parameters = PartExplosion(_scene, placement);
    const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, placement.lower, placement.upper);
    const NeuronCore::Sphere part = NeuronCore::EnvelopeSphere(envelope);
    const NeuronCore::Float3 center = NeuronCore::TransformPoint(placement.transform, part.center);
    reach.sphere.radius = std::max(reach.sphere.radius, NeuronCore::Length(center - reach.sphere.center) + part.radius);
    reach.stopSeconds = std::max(reach.stopSeconds, envelope.stopSeconds);
  }
  return reach;
}

} // namespace GameLib
