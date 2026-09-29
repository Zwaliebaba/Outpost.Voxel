#include "pch.h"

#include "SceneModels.h"

#include "Explosion.h"
#include "Quaternion.h"
#include "RigidTransform.h"

#include <optional>
#include <stdexcept>
#include <string>

namespace NeuronClient
{
namespace
{

using NeuronCore::Float3;

[[nodiscard]] Float3 ToFloat3(NeuronCore::Int3 _value) noexcept
{
  return {static_cast<float>(_value.x), static_cast<float>(_value.y), static_cast<float>(_value.z)};
}

// Where the part whose origin is _origin stands, in a model whose box's middle is _middle, of an entity at _position
// turned by _rotation (§7.2). For a station, whose position is whole and whose rotation is a symmetry of the cube, every
// term is exact, and so is every voxel's center.
[[nodiscard]] NeuronCore::RigidTransform PartTransform(Float3 _middle, Float3 _origin, const NeuronCore::Rotation& _rotation,
                                                       Float3 _position) noexcept
{
  return {_rotation, _position + NeuronCore::RotateVector(_rotation, _origin - _middle)};
}

// The detonation as the part whose origin is _origin sees it (§7.7): ADR-024's defaults about the model's centroid, the
// entity's velocity at the event turned into the part's axes, and the event's seed.
[[nodiscard]] NeuronCore::ExplosionParameters PartExplosion(Float3 _centroid, Float3 _origin, const NeuronCore::Rotation& _rotation,
                                                            Float3 _velocity, std::uint32_t _seed) noexcept
{
  NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(_centroid - _origin);
  parameters.inheritedVelocity = NeuronCore::UnrotateVector(_rotation, _velocity);
  parameters.seed = _seed;
  return parameters;
}

} // namespace

SceneModels::SceneModels(std::span<const NeuronCore::VoxModel> _models)
  : m_models(_models.begin(), _models.end()),
    m_fragments(m_models)
{
  const std::vector<std::uint32_t> firstRecords = NeuronCore::ModelFirstRecords(m_models);
  for (std::uint32_t index = 0; index < m_models.size(); ++index)
  {
    const NeuronCore::VoxModel& model = m_models[index];
    const std::optional<NeuronCore::VoxelBounds> bounds = NeuronCore::OccupiedBounds(model);
    if (!bounds)
    {
      throw std::invalid_argument("Model " + std::to_string(index) + " of the welcome holds no voxel.");
    }
    const Float3 lower = ToFloat3(bounds->lower);
    const Float3 upper = ToFloat3(bounds->upper);
    const Float3 middle = (lower + upper) * 0.5f;
    m_measures.push_back({middle, NeuronCore::Length(upper - middle), NeuronCore::VoxelCentroid(model),
                          static_cast<std::uint32_t>(m_parts.size()), static_cast<std::uint32_t>(model.instances.size())});
    for (std::uint32_t part = 0; part < model.instances.size(); ++part)
    {
      m_parts.push_back(
        NeuronCore::PlacePart(model, index, firstRecords[index], part, {NeuronCore::IDENTITY_ROTATION, {0.0f, 0.0f, 0.0f}}));
    }
  }
}

void SceneModels::Place(const SampledEntity& _entity, std::vector<NeuronCore::Placement>& _placements) const
{
  const Measure& measure = m_measures.at(_entity.modelIndex);
  const NeuronCore::VoxModel& model = m_models[_entity.modelIndex];
  const NeuronCore::Rotation rotation = NeuronCore::RotationOf(_entity.rotation);
  for (std::uint32_t part = 0; part < measure.partCount; ++part)
  {
    NeuronCore::Placement placement = m_parts[measure.firstPart + part];
    const Float3 origin = ToFloat3(model.instances[part].origin);
    placement.transform = PartTransform(measure.middle, origin, rotation, _entity.position);
    if (_entity.detonation)
    {
      const NeuronCore::DetonationEvent& event = _entity.detonation->event;
      placement.detonation = NeuronCore::PlacementDetonation{PartExplosion(measure.centroid, origin, rotation, event.velocity, event.seed),
                                                             _entity.detonation->seconds, m_fragments.Part(_entity.modelIndex, part)};
    }
    _placements.push_back(placement);
  }
}

NeuronCore::Sphere SceneModels::Extent(const SampledEntity& _entity) const
{
  if (!_entity.detonation)
  {
    return {_entity.position, m_measures.at(_entity.modelIndex).radius};
  }
  std::vector<NeuronCore::Placement> placements;
  Place(_entity, placements);
  std::vector<NeuronCore::Sphere> spheres;
  spheres.reserve(placements.size());
  for (const NeuronCore::Placement& placement : placements)
  {
    spheres.push_back(NeuronCore::PlacementSphere(placement));
  }
  return NeuronCore::EnclosingSphere(spheres);
}

NeuronCore::Sphere SceneModels::Reach(const SampledEntity& _entity) const
{
  const Measure& measure = m_measures.at(_entity.modelIndex);
  const NeuronCore::VoxModel& model = m_models[_entity.modelIndex];
  const NeuronCore::Rotation rotation = NeuronCore::RotationOf(_entity.rotation);
  // The seed moves voxels within the envelope, not the envelope.
  const Float3 velocity = _entity.detonation ? _entity.detonation->event.velocity : _entity.velocity;
  std::vector<NeuronCore::Sphere> spheres{{_entity.position, measure.radius}};
  for (std::uint32_t part = 0; part < measure.partCount; ++part)
  {
    const NeuronCore::Placement& whole = m_parts[measure.firstPart + part];
    const Float3 origin = ToFloat3(model.instances[part].origin);
    const NeuronCore::ExplosionParameters parameters = PartExplosion(measure.centroid, origin, rotation, velocity, 0u);
    const NeuronCore::Sphere local = NeuronCore::EnvelopeSphere(
      NeuronCore::BoundExplosion(parameters, whole.lower, whole.upper, m_fragments.Part(_entity.modelIndex, part).radius));
    spheres.push_back(
      {NeuronCore::TransformPoint(PartTransform(measure.middle, origin, rotation, _entity.position), local.center), local.radius});
  }
  return NeuronCore::EnclosingSphere(spheres);
}

float SceneModels::Radius(std::uint16_t _model) const
{
  return m_measures.at(_model).radius;
}

} // namespace NeuronClient
