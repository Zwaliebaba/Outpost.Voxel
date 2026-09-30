#include "pch.h"

#include "CombatProfile.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace GameCore
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Int3;

// The bearing is chosen among whole degrees off the bow, from -179 to 180.
constexpr std::int32_t DEGREES_PER_TURN = 360;

[[nodiscard]] Float3 CenterOf(Int3 _cell) noexcept
{
  return {static_cast<float>(_cell.x) + 0.5f, static_cast<float>(_cell.y) + 0.5f, static_cast<float>(_cell.z) + 0.5f};
}

// How many of _weapons bear on a target _degrees off the bow, level: those whose facing leans toward it.
[[nodiscard]] std::size_t Bearing(std::span<const CombatWeapon> _weapons, std::int32_t _degrees) noexcept
{
  const double radians = static_cast<double>(_degrees) * std::numbers::pi / 180.0;
  const double x = std::sin(radians);
  const double z = std::cos(radians);
  return static_cast<std::size_t>(
    std::ranges::count_if(_weapons, [x, z](const CombatWeapon& _weapon)
                          { return static_cast<double>(_weapon.facing.x) * x + static_cast<double>(_weapon.facing.z) * z > 1.0e-9; }));
}

// The bearing that brings the most of _weapons to bear: the middle of the widest run of whole degrees that does, the
// nearer the bow on a tie, and to starboard on a tie again. Dead ahead for a design with no weapon on the plane.
[[nodiscard]] float BearingOf(std::span<const CombatWeapon> _weapons) noexcept
{
  std::array<std::size_t, DEGREES_PER_TURN> counts{};
  std::size_t most = 0;
  for (std::int32_t degree = 0; degree < DEGREES_PER_TURN; ++degree)
  {
    counts[static_cast<std::size_t>(degree)] = Bearing(_weapons, degree);
    most = std::max(most, counts[static_cast<std::size_t>(degree)]);
  }
  if (most == 0)
  {
    return 0.0f;
  }
  // The runs of the most, each from a degree whose predecessor falls short, around the turn.
  std::int32_t bestWidth = 0;
  double bestMiddle = 0.0;
  const auto at = [&counts](std::int32_t _degree)
  { return counts[static_cast<std::size_t>(((_degree % DEGREES_PER_TURN) + DEGREES_PER_TURN) % DEGREES_PER_TURN)]; };
  for (std::int32_t start = 0; start < DEGREES_PER_TURN; ++start)
  {
    if (at(start) != most || at(start - 1) == most)
    {
      continue;
    }
    std::int32_t width = 0;
    while (width < DEGREES_PER_TURN && at(start + width) == most)
    {
      ++width;
    }
    // The middle, as an angle from -180 to 180 off the bow.
    double middle = static_cast<double>(start) + 0.5 * static_cast<double>(width - 1);
    middle = middle > 180.0 ? middle - 360.0 : middle;
    const bool better = width > bestWidth || (width == bestWidth && (std::abs(middle) < std::abs(bestMiddle) ||
                                                                     (std::abs(middle) == std::abs(bestMiddle) && middle > bestMiddle)));
    if (better)
    {
      bestWidth = width;
      bestMiddle = middle;
    }
  }
  if (bestWidth == 0)
  {
    return 0.0f; // every degree bears
  }
  return static_cast<float>(bestMiddle * std::numbers::pi / 180.0);
}

// The silhouettes of _voxels (G57): for each direction a shooter may look along, the voxel nearest the shooter in each
// column a voxel wide and a voxel high, square to the direction, a tie going to the lower index; once for each column,
// in the columns' order.
[[nodiscard]] std::array<std::vector<std::uint32_t>, SILHOUETTE_DIRECTIONS> Silhouettes(std::span<const NeuronCore::CompositeVoxel> _voxels)
{
  std::array<std::vector<std::uint32_t>, SILHOUETTE_DIRECTIONS> silhouettes;
  if (_voxels.empty())
  {
    return silhouettes;
  }
  std::int32_t lowestY = _voxels.front().cell.y;
  std::int32_t highestY = lowestY;
  for (const NeuronCore::CompositeVoxel& voxel : _voxels)
  {
    lowestY = std::min(lowestY, voxel.cell.y);
    highestY = std::max(highestY, voxel.cell.y);
  }
  const auto rows = static_cast<std::size_t>(highestY - lowestY) + 1u;
  for (std::size_t direction = 0; direction < SILHOUETTE_DIRECTIONS; ++direction)
  {
    // Looking along (sin a, 0, cos a); across it, (cos a, 0, -sin a).
    const double angle = 2.0 * std::numbers::pi * static_cast<double>(direction) / static_cast<double>(SILHOUETTE_DIRECTIONS);
    const double alongX = std::sin(angle);
    const double alongZ = std::cos(angle);
    std::vector<std::int64_t> columns(_voxels.size());
    std::int64_t first = std::numeric_limits<std::int64_t>::max();
    std::int64_t last = std::numeric_limits<std::int64_t>::lowest();
    for (std::size_t index = 0; index < _voxels.size(); ++index)
    {
      const Int3 cell = _voxels[index].cell;
      const double across = (static_cast<double>(cell.x) + 0.5) * alongZ - (static_cast<double>(cell.z) + 0.5) * alongX;
      columns[index] = static_cast<std::int64_t>(std::floor(across));
      first = std::min(first, columns[index]);
      last = std::max(last, columns[index]);
    }
    const auto width = static_cast<std::size_t>(last - first + 1);
    std::vector<double> nearest(width * rows, std::numeric_limits<double>::infinity());
    std::vector<std::uint32_t> holder(width * rows, NeuronCore::NO_VOXEL);
    for (std::size_t index = 0; index < _voxels.size(); ++index)
    {
      const Int3 cell = _voxels[index].cell;
      const double depth = (static_cast<double>(cell.x) + 0.5) * alongX + (static_cast<double>(cell.z) + 0.5) * alongZ;
      const std::size_t slot = static_cast<std::size_t>(columns[index] - first) + width * static_cast<std::size_t>(cell.y - lowestY);
      if (depth < nearest[slot])
      {
        nearest[slot] = depth;
        holder[slot] = static_cast<std::uint32_t>(index);
      }
    }
    for (const std::uint32_t voxel : holder)
    {
      if (voxel != NeuronCore::NO_VOXEL)
      {
        silhouettes[direction].push_back(voxel);
      }
    }
  }
  return silhouettes;
}

} // namespace

CombatProfile ComputeCombatProfile(const Design& _design, std::span<const NeuronCore::VoxModel> _models,
                                   const NeuronCore::CompositeModel& _composite)
{
  CombatProfile profile{};
  profile.voxels = NeuronCore::CompositeVoxels(_models, _composite);

  // The components, the hull first and then the module at each mount, as DesignComposite places them.
  profile.components.resize(_composite.components.size());
  for (std::size_t component = 1; component < profile.components.size() && component <= _design.mounts.size(); ++component)
  {
    profile.components[component].module = _design.mounts[component - 1].kind;
  }
  for (std::uint32_t index = 0; index < profile.voxels.size(); ++index)
  {
    CombatComponent& component = profile.components[profile.voxels[index].component];
    if (component.voxelCount == 0)
    {
      component.firstVoxel = index;
    }
    ++component.voxelCount;
  }

  // A hull voxel's class is its palette entry's, and a module's voxels are light.
  profile.materials.reserve(profile.voxels.size());
  for (const NeuronCore::CompositeVoxel& voxel : profile.voxels)
  {
    profile.materials.push_back(voxel.component == 0 ? _design.spec->materials[voxel.color] : MaterialClass::Light);
  }

  // Each weapon fires from the middle of its mount box's front face, half the box's depth ahead of its center cell's
  // center: a mount's box is odd on every axis (Design/ADR/ADR-027).
  for (std::size_t mount = 0; mount < _design.mounts.size(); ++mount)
  {
    const Mount& fitted = _design.mounts[mount];
    const WeaponSpec* weapon = fitted.kind == ModuleKind::Weapon ? FindWeapon(fitted.module->name) : nullptr;
    if (weapon == nullptr)
    {
      continue;
    }
    const Int3 facing = Facing(fitted);
    const float ahead = 0.5f * static_cast<float>(MountBox(fitted.size).z);
    profile.weapons.push_back({weapon, static_cast<std::uint16_t>(mount + 1),
                               CenterOf(fitted.centerCell) +
                                 Float3{static_cast<float>(facing.x), static_cast<float>(facing.y), static_cast<float>(facing.z)} * ahead,
                               facing});
  }
  profile.bearingRadians = BearingOf(profile.weapons);
  profile.silhouettes = Silhouettes(profile.voxels);
  return profile;
}

std::size_t SilhouetteDirection(Float3 _direction) noexcept
{
  const double angle = std::atan2(static_cast<double>(_direction.x), static_cast<double>(_direction.z));
  const double step = 2.0 * std::numbers::pi / static_cast<double>(SILHOUETTE_DIRECTIONS);
  const auto nearest = static_cast<std::int64_t>(std::llround(angle / step));
  const auto count = static_cast<std::int64_t>(SILHOUETTE_DIRECTIONS);
  return static_cast<std::size_t>(((nearest % count) + count) % count);
}

std::vector<CombatComponent> ComponentsOf(std::span<const std::string> _names, std::span<const NeuronCore::VoxModel> _models,
                                          const NeuronCore::CompositeModel& _composite)
{
  std::vector<CombatComponent> components;
  components.reserve(_composite.components.size());
  std::uint32_t first = 0;
  for (const NeuronCore::CompositeComponent& component : _composite.components)
  {
    std::uint32_t count = 0;
    for (const NeuronCore::ModelInstance& instance : _models[component.model].instances)
    {
      count += instance.recordCount;
    }
    const ModuleSpec* module = component.model < _names.size() ? FindModule(_names[component.model]) : nullptr;
    components.push_back({module != nullptr ? std::optional(module->kind) : std::nullopt, first, count});
    first += count;
  }
  return components;
}

Condition ConditionOf(std::span<const CombatComponent> _components, std::span<const std::uint8_t> _gone) noexcept
{
  const auto remaining = [_gone](const CombatComponent& _component)
  {
    std::uint32_t gone = 0;
    for (std::uint32_t voxel = _component.firstVoxel; voxel < _component.firstVoxel + _component.voxelCount; ++voxel)
    {
      const std::size_t byte = voxel / 8u;
      gone += byte < _gone.size() && ((_gone[byte] >> (voxel % 8u)) & 1u) != 0u ? 1u : 0u;
    }
    return _component.voxelCount - gone;
  };
  Condition condition{1.0f, 1.0f};
  std::uint32_t hullVoxels = 0;
  std::uint32_t hullRemaining = 0;
  for (const CombatComponent& component : _components)
  {
    if (component.voxelCount == 0)
    {
      continue;
    }
    if (!component.module)
    {
      hullVoxels += component.voxelCount;
      hullRemaining += remaining(component);
    }
    else if (*component.module == ModuleKind::Command || *component.module == ModuleKind::Reactor)
    {
      const float share = static_cast<float>(remaining(component)) / static_cast<float>(component.voxelCount);
      condition.vital = std::min(condition.vital, std::clamp((share - FAIL_SHARE) / (1.0f - FAIL_SHARE), 0.0f, 1.0f));
    }
  }
  if (hullVoxels > 0)
  {
    condition.hull = static_cast<float>(hullRemaining) / static_cast<float>(hullVoxels);
  }
  return condition;
}

} // namespace GameCore
