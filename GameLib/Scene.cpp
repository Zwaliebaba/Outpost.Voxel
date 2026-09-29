#include "pch.h"

#include "Scene.h"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

namespace GameLib
{

Scene::Scene(std::span<const NeuronCore::VoxModel> _models, std::span<const NeuronCore::CompositeModel> _composites, std::size_t _sideCount,
             NeuronCore::Float3 _toSun)
  : m_models(_models, _composites, _sideCount),
    m_toSun(_toSun)
{
}

std::vector<NeuronCore::Placement> Scene::Place(const NeuronClient::WorldSample& _sample,
                                                std::span<const NeuronClient::SampledEntity> _remembered) const
{
  std::vector<NeuronCore::Placement> placements;
  placements.reserve(_sample.entities.size() + _remembered.size());
  for (const NeuronClient::SampledEntity& entity : _sample.entities)
  {
    m_models.Place(entity, placements);
  }
  for (const NeuronClient::SampledEntity& entity : _remembered)
  {
    m_models.Place(entity, placements, true);
  }
  if (!NeuronCore::AssignVoxelIds(placements))
  {
    throw std::runtime_error("The world holds more voxels than a frame can name.");
  }
  return placements;
}

std::vector<NeuronCore::Blast> Scene::Blasts(const NeuronClient::WorldSample& _sample) const
{
  std::vector<NeuronCore::Blast> blasts;
  for (const NeuronClient::SampledEntity& entity : _sample.entities)
  {
    if (const std::optional<NeuronCore::Blast> blast = m_models.Blast(entity))
    {
      blasts.push_back(*blast);
    }
  }
  return blasts;
}

bool Scene::FitShadowView(const NeuronClient::WorldSample& _sample)
{
  std::vector<NeuronCore::Sphere> reaches;
  reaches.reserve(_sample.entities.size());
  for (const NeuronClient::SampledEntity& entity : _sample.entities)
  {
    reaches.push_back(m_models.Reach(entity));
  }
  NeuronCore::Sphere sphere = NeuronCore::EnclosingSphere(reaches);
  if (m_shadowSphere)
  {
    const NeuronCore::Sphere held = *m_shadowSphere;
    const bool holds = std::ranges::all_of(reaches, [&held](const NeuronCore::Sphere& _reach)
                                           { return NeuronCore::Length(_reach.center - held.center) + _reach.radius <= held.radius; });
    if (holds)
    {
      return false;
    }
    const std::array<NeuronCore::Sphere, 2> both{held, sphere};
    sphere = NeuronCore::EnclosingSphere(both);
    sphere.radius *= SHADOW_VIEW_GROWTH;
  }
  sphere.radius = std::max(sphere.radius, NeuronCore::SHADOW_HALF_EXTENT);
  m_shadowSphere = sphere;
  const NeuronCore::Float3 extent{sphere.radius, sphere.radius, sphere.radius};
  m_shadowView = NeuronCore::MakeShadowView(m_toSun, sphere.center, sphere.radius, sphere.center - extent, sphere.center + extent,
                                            NeuronCore::SHADOW_MAP_PIXELS);
  return true;
}

} // namespace GameLib
