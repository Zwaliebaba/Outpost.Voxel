#include "pch.h"

#include "SceneModels.h"

#include "Composite.h"
#include "Explosion.h"
#include "Quaternion.h"
#include "SidePalette.h"

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

} // namespace

SceneModels::SceneModels(std::span<const NeuronCore::VoxModel> _models, std::span<const NeuronCore::CompositeModel> _composites,
                         std::size_t _sideCount)
  : m_models(_models.begin(), _models.end()),
    m_fragments(m_models),
    m_sideCount(_sideCount)
{
  const std::vector<std::uint32_t> firstRecords = NeuronCore::ModelFirstRecords(m_models);
  for (std::uint32_t index = 0; index < _composites.size(); ++index)
  {
    const NeuronCore::CompositeModel& composite = _composites[index];
    for (const NeuronCore::CompositeComponent& component : composite.components)
    {
      if (component.model >= m_models.size())
      {
        throw std::invalid_argument("Composite " + std::to_string(index) + " of the welcome names model " +
                                    std::to_string(component.model) + ", of its " + std::to_string(m_models.size()) + ".");
      }
    }
    const std::optional<NeuronCore::VoxelBounds> bounds = NeuronCore::CompositeBounds(m_models, composite);
    if (!bounds)
    {
      throw std::invalid_argument("Composite " + std::to_string(index) + " of the welcome holds no voxel.");
    }
    const Float3 lower = ToFloat3(bounds->lower);
    const Float3 upper = ToFloat3(bounds->upper);
    const Float3 middle = (lower + upper) * 0.5f;
    const auto firstPart = static_cast<std::uint32_t>(m_parts.size());
    for (const NeuronCore::CompositeComponent& component : composite.components)
    {
      const NeuronCore::VoxModel& model = m_models[component.model];
      for (std::uint32_t part = 0; part < model.instances.size(); ++part)
      {
        m_parts.push_back({NeuronCore::PlacePart(model, component.model, firstRecords[component.model], part,
                                                 {NeuronCore::IDENTITY_ROTATION, {0.0f, 0.0f, 0.0f}}),
                           NeuronCore::ComponentTransform(component), NeuronCore::IsIdentityComponent(component),
                           ToFloat3(model.instances[part].origin), component.model, part});
      }
    }
    m_measures.push_back({middle, NeuronCore::Length(upper - middle), NeuronCore::CompositeCentroid(m_models, composite), firstPart,
                          static_cast<std::uint32_t>(m_parts.size()) - firstPart});
  }
}

namespace
{

// The rotation that turns a part of a component of an entity turned by _rotation into the world.
[[nodiscard]] NeuronCore::Rotation PartRotation(const NeuronCore::RigidTransform& _component, bool _isIdentity,
                                                const NeuronCore::Rotation& _rotation) noexcept
{
  return _isIdentity ? _rotation : NeuronCore::ComposeRotations(_rotation, _component.rotation);
}

} // namespace

void SceneModels::Place(const SampledEntity& _entity, std::vector<NeuronCore::Placement>& _placements, bool _remembered) const
{
  const Measure& measure = m_measures.at(_entity.composite);
  const NeuronCore::Rotation rotation = NeuronCore::RotationOf(_entity.rotation);
  for (std::uint32_t index = 0; index < measure.partCount; ++index)
  {
    const Part& part = m_parts[measure.firstPart + index];
    // Where the part's origin stands in the composite's space, about the middle of its box, turned with the entity and
    // taken to its position (§7.2). The component turns by a symmetry of the cube and moves by whole voxels, so for a
    // station, whose position is whole and whose rotation is a symmetry of the cube too, every term is exact, and so is
    // every voxel's center. A component that leaves its model where it is adds nothing, not even a rounding.
    const Float3 origin = part.isIdentity ? part.origin : NeuronCore::TransformPoint(part.component, part.origin);
    const NeuronCore::Rotation turned = PartRotation(part.component, part.isIdentity, rotation);
    NeuronCore::Placement placement = part.whole;
    placement.transform = {turned, _entity.position + NeuronCore::RotateVector(rotation, origin - measure.middle)};
    placement.paletteIndex = _remembered ? NeuronCore::RememberedPaletteIndex(part.model, _entity.side, m_sideCount, m_models.size())
                                         : NeuronCore::SidePaletteIndex(part.model, _entity.side, m_sideCount);
    if (_entity.detonation)
    {
      const NeuronCore::DetonationEvent& event = _entity.detonation->event;
      // The detonation as the part sees it (§7.7): ADR-024's defaults about the composite's centroid, the entity's
      // velocity at the event turned into the part's axes, and the event's seed; the fragments its model breaks into,
      // and ADR-025's heat for the composite's size.
      const Float3 centroid = part.isIdentity ? measure.centroid : NeuronCore::InverseTransformPoint(part.component, measure.centroid);
      NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(centroid - part.origin);
      parameters.inheritedVelocity = NeuronCore::UnrotateVector(turned, event.velocity);
      parameters.seed = event.seed;
      placement.detonation =
        NeuronCore::PlacementDetonation{parameters, _entity.detonation->seconds, m_fragments.Part(part.model, part.part),
                                        NeuronCore::DefaultHeatParameters(measure.radius)};
    }
    _placements.push_back(placement);
  }
}

std::optional<NeuronCore::Blast> SceneModels::Blast(const SampledEntity& _entity) const
{
  if (!_entity.detonation)
  {
    return std::nullopt;
  }
  const Measure& measure = m_measures.at(_entity.composite);
  const NeuronCore::Rotation rotation = NeuronCore::RotationOf(_entity.rotation);
  // The debris drifts by the entity's velocity over the lone voxel's drag, as every part's detonation has it.
  const float drag = NeuronCore::DefaultExplosionParameters({0.0f, 0.0f, 0.0f}).drag;
  const NeuronCore::DetonationEvent& event = _entity.detonation->event;
  return NeuronCore::Blast{.origin = _entity.position + NeuronCore::RotateVector(rotation, measure.centroid - measure.middle),
                           .drift = event.velocity * (1.0f / drag),
                           .drag = drag,
                           .extent = measure.radius,
                           .seed = event.seed,
                           .timeSeconds = _entity.detonation->seconds};
}

NeuronCore::Sphere SceneModels::Extent(const SampledEntity& _entity) const
{
  if (!_entity.detonation)
  {
    return {_entity.position, m_measures.at(_entity.composite).radius};
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
  const Measure& measure = m_measures.at(_entity.composite);
  // The seed and the time move voxels within the envelope, not the envelope.
  SampledEntity detonated = _entity;
  if (!detonated.detonation)
  {
    detonated.detonation = SampledDetonation{{_entity.id, 0u, 0u, _entity.velocity}, 0.0f};
  }
  std::vector<NeuronCore::Placement> placements;
  placements.reserve(measure.partCount);
  Place(detonated, placements);
  std::vector<NeuronCore::Sphere> spheres{{_entity.position, measure.radius}};
  for (const NeuronCore::Placement& placement : placements)
  {
    // Every placement of a detonated entity is detonated.
    if (!placement.detonation)
    {
      continue;
    }
    const NeuronCore::Sphere local = NeuronCore::EnvelopeSphere(NeuronCore::PlacementEnvelope(placement, *placement.detonation));
    spheres.push_back({NeuronCore::TransformPoint(placement.transform, local.center), local.radius});
  }
  return NeuronCore::EnclosingSphere(spheres);
}

float SceneModels::Radius(std::uint16_t _composite) const
{
  return m_measures.at(_composite).radius;
}

} // namespace NeuronClient
