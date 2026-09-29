#include "pch.h"

#include "Fragmentation.h"

#include "Explosion.h"
#include "Hash.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace NeuronCore
{
namespace
{

// No fragment yet.
constexpr std::uint32_t NO_FRAGMENT = std::numeric_limits<std::uint32_t>::max();

// Mixed into a record's index for the draw that decides whether it starts a fragment: the fractional part of √2, so that
// the draw shares no input with a detonation's hashes.
constexpr std::uint32_t SITE_SALT = 0x6A09E667u;

// ADR-024's defaults: the shatter distance as a fraction of the farthest a voxel center lies from the centroid, never
// below a few voxels, and the growth steps.
constexpr float SHATTER_FRACTION = 0.08f;
constexpr float MIN_SHATTER_DISTANCE = 3.0f;
constexpr std::uint32_t GROWTH_STEPS = 10u;

// The six face neighbors of a cell.
constexpr std::array<std::array<std::int32_t, 3>, 6> FACE_STEPS{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};

// A cell's key in a part's lookup: its three 8-bit coordinates.
[[nodiscard]] std::uint32_t CellKey(std::uint32_t _x, std::uint32_t _y, std::uint32_t _z) noexcept
{
  return _x | (_y << 8u) | (_z << 16u);
}

// Whether the record at model index _record starts a fragment, _distance from the blast origin: the nearer, the likelier,
// and certainly at the origin itself.
[[nodiscard]] bool StartsFragment(std::uint32_t _record, float _distance, float _shatterDistance) noexcept
{
  const float ratio = _distance / _shatterDistance;
  const float chance = 1.0f / (1.0f + ratio * ratio * ratio);
  const float draw = static_cast<float>(PcgHash(_record ^ SITE_SALT) >> 8u) * (1.0f / 16777216.0f);
  return draw < chance;
}

// Breaks one part, whose records are _records and whose first record in its model is _firstRecord, into fragments
// numbered from _firstFragment. Appends them to _fragments, writes each record's fragment, and gives the part's radius.
float FragmentPart(std::span<const std::uint32_t> _records, std::uint32_t _firstRecord, Float3 _blastOrigin,
                   const FragmentationParameters& _parameters, std::uint32_t _firstFragment, std::span<std::uint32_t> _fragmentOf,
                   std::vector<Fragment>& _fragments)
{
  const auto count = static_cast<std::uint32_t>(_records.size());
  std::unordered_map<std::uint32_t, std::uint32_t> cells;
  cells.reserve(count);
  std::vector<Float3> centers(count);
  for (std::uint32_t i = 0; i < count; ++i)
  {
    const VoxelRecord voxel = UnpackVoxelRecord(_records[i]);
    cells.emplace(CellKey(voxel.x, voxel.y, voxel.z), i);
    centers[i] = {static_cast<float>(voxel.x) + 0.5f, static_cast<float>(voxel.y) + 0.5f, static_cast<float>(voxel.z) + 0.5f};
  }

  // Each fragment grows from the voxel that starts it, a face step at a time, and takes every voxel it reaches first, up
  // to growthSteps from where it started: a breadth-first search from every start at once, in record order, so that the
  // same part always breaks the same way.
  std::vector<std::uint32_t> owner(count, NO_FRAGMENT);
  std::vector<std::uint32_t> steps(count, 0u);
  std::vector<std::uint32_t> queue;
  queue.reserve(count);
  std::uint32_t local = 0;
  const auto grow = [&](std::size_t _head)
  {
    while (_head < queue.size())
    {
      const std::uint32_t at = queue[_head++];
      if (steps[at] >= _parameters.growthSteps)
      {
        continue;
      }
      const VoxelRecord voxel = UnpackVoxelRecord(_records[at]);
      for (const std::array<std::int32_t, 3>& step : FACE_STEPS)
      {
        const std::int32_t x = static_cast<std::int32_t>(voxel.x) + step[0];
        const std::int32_t y = static_cast<std::int32_t>(voxel.y) + step[1];
        const std::int32_t z = static_cast<std::int32_t>(voxel.z) + step[2];
        if (x < 0 || y < 0 || z < 0 || x > 255 || y > 255 || z > 255)
        {
          continue;
        }
        const auto found = cells.find(CellKey(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(z)));
        if (found == cells.end() || owner[found->second] != NO_FRAGMENT)
        {
          continue;
        }
        owner[found->second] = owner[at];
        steps[found->second] = steps[at] + 1u;
        queue.push_back(found->second);
      }
    }
  };
  for (std::uint32_t i = 0; i < count; ++i)
  {
    if (StartsFragment(_firstRecord + i, Length(centers[i] - _blastOrigin), _parameters.shatterDistance))
    {
      owner[i] = local++;
      queue.push_back(i);
    }
  }
  grow(0);
  // What no start reached, beyond every fragment's reach or in a piece of its own, starts fragments of its own, in record
  // order.
  for (std::uint32_t i = 0; i < count; ++i)
  {
    if (owner[i] == NO_FRAGMENT)
    {
      owner[i] = local++;
      const std::size_t head = queue.size();
      queue.push_back(i);
      grow(head);
    }
  }

  // Each fragment's pivot, the mean of its centers, in double as VoxelCentroid takes it; and its size.
  std::vector<std::array<double, 3>> sums(local, {0.0, 0.0, 0.0});
  std::vector<std::uint32_t> sizes(local, 0u);
  for (std::uint32_t i = 0; i < count; ++i)
  {
    sums[owner[i]][0] += centers[i].x;
    sums[owner[i]][1] += centers[i].y;
    sums[owner[i]][2] += centers[i].z;
    ++sizes[owner[i]];
  }
  const std::size_t first = _fragments.size();
  for (std::uint32_t f = 0; f < local; ++f)
  {
    const double scale = 1.0 / static_cast<double>(sizes[f]);
    _fragments.push_back(
      {{static_cast<float>(sums[f][0] * scale), static_cast<float>(sums[f][1] * scale), static_cast<float>(sums[f][2] * scale)},
       static_cast<float>(1.0 / std::cbrt(static_cast<double>(sizes[f])))});
  }

  // The radius as the pose measures it, in float, from each center to its fragment's pivot.
  float radius = 0.0f;
  for (std::uint32_t i = 0; i < count; ++i)
  {
    _fragmentOf[i] = _firstFragment + owner[i];
    radius = std::max(radius, Length(centers[i] - _fragments[first + owner[i]].pivot));
  }
  return radius;
}

} // namespace

FragmentationParameters DefaultFragmentationParameters(const VoxModel& _model) noexcept
{
  const Float3 centroid = VoxelCentroid(_model);
  float extent = 0.0f;
  for (const ModelInstance& instance : _model.instances)
  {
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      extent = std::max(extent, Length(VoxelBox(instance, _model.records[instance.firstRecord + i]).center - centroid));
    }
  }
  return {
    .blastOrigin = centroid, .shatterDistance = std::max(SHATTER_FRACTION * extent, MIN_SHATTER_DISTANCE), .growthSteps = GROWTH_STEPS};
}

ModelFragments FragmentModel(const VoxModel& _model, const FragmentationParameters& _parameters)
{
  ModelFragments fragments;
  fragments.fragmentOf.resize(_model.records.size());
  for (const ModelInstance& instance : _model.instances)
  {
    // The part's own space, where its records lie: the model's less the part's origin.
    const Float3 origin{static_cast<float>(instance.origin.x), static_cast<float>(instance.origin.y),
                        static_cast<float>(instance.origin.z)};
    const std::span<const std::uint32_t> records(_model.records.data() + instance.firstRecord, instance.recordCount);
    const auto firstFragment = static_cast<std::uint32_t>(fragments.fragments.size());
    fragments.partRadius.push_back(FragmentPart(records, instance.firstRecord, _parameters.blastOrigin - origin, _parameters, firstFragment,
                                                std::span(fragments.fragmentOf).subspan(instance.firstRecord, instance.recordCount),
                                                fragments.fragments));
  }
  return fragments;
}

SceneFragments::SceneFragments(std::span<const VoxModel> _models)
{
  std::uint32_t firstRecord = 0;
  for (const VoxModel& model : _models)
  {
    const ModelFragments broken = FragmentModel(model, DefaultFragmentationParameters(model));
    m_models.push_back({firstRecord, static_cast<std::uint32_t>(m_fragments.size()), static_cast<std::uint32_t>(broken.fragments.size()),
                        static_cast<std::uint32_t>(m_parts.size())});
    for (std::size_t part = 0; part < model.instances.size(); ++part)
    {
      m_parts.push_back({model.instances[part].firstRecord, model.instances[part].recordCount, broken.partRadius[part]});
    }
    m_fragmentOf.insert(m_fragmentOf.end(), broken.fragmentOf.begin(), broken.fragmentOf.end());
    m_fragments.insert(m_fragments.end(), broken.fragments.begin(), broken.fragments.end());
    firstRecord += static_cast<std::uint32_t>(model.records.size());
  }
}

PartFragments SceneFragments::Part(std::uint32_t _model, std::uint32_t _part) const noexcept
{
  const ModelSpan& model = m_models[_model];
  const PartSpan& part = m_parts[model.firstPart + _part];
  return {std::span(m_fragmentOf).subspan(model.firstRecord + part.firstRecord, part.recordCount),
          std::span(m_fragments).subspan(model.firstFragment, model.fragmentCount), model.firstFragment, part.radius};
}

} // namespace NeuronCore
