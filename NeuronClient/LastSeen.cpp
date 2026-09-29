#include "pch.h"

#include "LastSeen.h"

#include <algorithm>
#include <utility>

namespace NeuronClient
{

LastSeen::LastSeen(std::vector<bool> _marked)
  : m_marked(std::move(_marked))
{
}

void LastSeen::See(const WorldSample& _sample)
{
  for (const SampledEntity& entity : _sample.entities)
  {
    if (entity.composite >= m_marked.size() || !m_marked[entity.composite])
    {
      continue;
    }
    const auto seen = std::ranges::lower_bound(m_seen, entity.id, {}, &SampledEntity::id);
    if (seen != m_seen.end() && seen->id == entity.id)
    {
      *seen = entity;
    }
    else
    {
      m_seen.insert(seen, entity);
    }
  }
}

std::vector<SampledEntity> LastSeen::Remembered(const WorldSample& _sample) const
{
  std::vector<SampledEntity> remembered;
  for (const SampledEntity& entity : m_seen)
  {
    if (!std::ranges::binary_search(_sample.entities, entity.id, {}, &SampledEntity::id))
    {
      remembered.push_back(entity);
    }
  }
  return remembered;
}

} // namespace NeuronClient
