#include "pch.h"

#include "Combat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace GameLogic
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Int3;

// A brick of a body's voxels is this many cells on a side: a power of two, so that a cell's brick is its coordinates
// shifted.
constexpr std::int32_t BRICK_SHIFT = 3;

// How many voxels CutsNothing searches before it leaves the answer to a search of the whole.
constexpr std::size_t LOCAL_SEARCH_VOXELS = 512;

// How far past an entity's sphere a sweep still reads its grid: the half diagonal of a voxel, which a voxel's center
// lies within of the sphere.
constexpr float SPHERE_MARGIN = 1.0f;

[[nodiscard]] Float3 CenterOf(Int3 _cell) noexcept
{
  return {static_cast<float>(_cell.x) + 0.5f, static_cast<float>(_cell.y) + 0.5f, static_cast<float>(_cell.z) + 0.5f};
}

[[nodiscard]] std::array<double, 3> Doubled(Float3 _value) noexcept
{
  return {static_cast<double>(_value.x), static_cast<double>(_value.y), static_cast<double>(_value.z)};
}

// The distance from _point to the segment from _origin to _origin + _displacement.
[[nodiscard]] float SegmentDistance(Float3 _origin, Float3 _displacement, Float3 _point) noexcept
{
  const float length = NeuronCore::Dot(_displacement, _displacement);
  const float along = length > 0.0f ? std::clamp(NeuronCore::Dot(_point - _origin, _displacement) / length, 0.0f, 1.0f) : 0.0f;
  return NeuronCore::Length(_origin + _displacement * along - _point);
}

// The distance from _point to the box from _lower to _upper, 0 within it.
[[nodiscard]] float BoxDistance(Float3 _point, Float3 _lower, Float3 _upper) noexcept
{
  const float x = std::max({_lower.x - _point.x, 0.0f, _point.x - _upper.x});
  const float y = std::max({_lower.y - _point.y, 0.0f, _point.y - _upper.y});
  const float z = std::max({_lower.z - _point.z, 0.0f, _point.z - _upper.z});
  return std::sqrt(x * x + y * y + z * z);
}

// The cells a hit's reach covers about the voxel it lands on, nearest first: each offset with its squared length.
struct ReachOffset
{
  std::int32_t lengthSquared;
  Int3 offset;
};

[[nodiscard]] std::vector<ReachOffset> ReachOffsets(float _reachUnits)
{
  const auto extent = static_cast<std::int32_t>(std::floor(_reachUnits));
  const float reachSquared = _reachUnits * _reachUnits;
  std::vector<ReachOffset> offsets;
  for (std::int32_t z = -extent; z <= extent; ++z)
  {
    for (std::int32_t y = -extent; y <= extent; ++y)
    {
      for (std::int32_t x = -extent; x <= extent; ++x)
      {
        const std::int32_t lengthSquared = x * x + y * y + z * z;
        if (static_cast<float>(lengthSquared) <= reachSquared)
        {
          offsets.push_back({lengthSquared, {x, y, z}});
        }
      }
    }
  }
  std::ranges::stable_sort(offsets, {}, &ReachOffset::lengthSquared);
  return offsets;
}

// What one shot has spent so far on one voxel.
struct Spending
{
  std::size_t entity;
  std::uint32_t voxel;
  float damage;
  bool destroyed;
};

[[nodiscard]] Spending* FindSpending(std::vector<Spending>& _spending, std::size_t _entity, std::uint32_t _voxel) noexcept
{
  const auto found =
    std::ranges::find_if(_spending, [_entity, _voxel](const Spending& _item) { return _item.entity == _entity && _item.voxel == _voxel; });
  return found == _spending.end() ? nullptr : &*found;
}

} // namespace

VoxelBody::VoxelBody(std::vector<Int3> _cells)
  : m_cells(std::move(_cells)),
    m_grid(std::span<const Int3>(m_cells))
{
  if (m_cells.empty())
  {
    return;
  }
  constexpr std::array<Int3, 6> FACES{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
  m_faceNeighbors.resize(m_cells.size());
  for (std::size_t voxel = 0; voxel < m_cells.size(); ++voxel)
  {
    for (std::size_t face = 0; face < FACES.size(); ++face)
    {
      m_faceNeighbors[voxel][face] = m_grid.VoxelAt(m_cells[voxel] + FACES[face]);
    }
  }
  const Int3 lower = m_grid.Origin();
  const Int3 upper = lower + m_grid.Size();
  m_middle = {0.5f * static_cast<float>(lower.x + upper.x), 0.5f * static_cast<float>(lower.y + upper.y),
              0.5f * static_cast<float>(lower.z + upper.z)};
  const Int3 extent = upper - lower;
  m_radius = 0.5f * NeuronCore::Length({static_cast<float>(extent.x), static_cast<float>(extent.y), static_cast<float>(extent.z)});

  // The bricks, in the order of their cells' coordinates, z slowest.
  std::vector<std::uint32_t> order(m_cells.size());
  for (std::uint32_t index = 0; index < order.size(); ++index)
  {
    order[index] = index;
  }
  const auto brickOf = [this](std::uint32_t _voxel)
  {
    const Int3 cell = m_cells[_voxel];
    return std::tuple(cell.z >> BRICK_SHIFT, cell.y >> BRICK_SHIFT, cell.x >> BRICK_SHIFT);
  };
  std::ranges::stable_sort(order, {}, brickOf);
  m_brickVoxels = order;
  for (std::uint32_t index = 0; index < order.size(); ++index)
  {
    const Float3 center = CenterOf(m_cells[order[index]]);
    if (index == 0 || brickOf(order[index]) != brickOf(order[index - 1]))
    {
      m_bricks.push_back({center, center, index, 0});
    }
    Brick& brick = m_bricks.back();
    brick.lower = {std::min(brick.lower.x, center.x), std::min(brick.lower.y, center.y), std::min(brick.lower.z, center.z)};
    brick.upper = {std::max(brick.upper.x, center.x), std::max(brick.upper.y, center.y), std::max(brick.upper.z, center.z)};
    ++brick.count;
  }
}

std::optional<float> VoxelBody::NearestVoxel(Float3 _point, std::span<const std::uint8_t> _gone, float _limit) const
{
  // The bricks nearest first, each read only while it may hold a voxel nearer than the best so far.
  std::vector<std::pair<float, std::size_t>> bricks;
  bricks.reserve(m_bricks.size());
  for (std::size_t index = 0; index < m_bricks.size(); ++index)
  {
    const float bound = BoxDistance(_point, m_bricks[index].lower, m_bricks[index].upper);
    if (bound <= _limit)
    {
      bricks.emplace_back(bound, index);
    }
  }
  std::ranges::sort(bricks);
  float best = _limit;
  bool found = false;
  for (const auto& [bound, index] : bricks)
  {
    if (bound > best)
    {
      break;
    }
    const Brick& brick = m_bricks[index];
    for (std::uint32_t slot = brick.first; slot < brick.first + brick.count; ++slot)
    {
      const std::uint32_t voxel = m_brickVoxels[slot];
      if (IsGone(_gone, voxel))
      {
        continue;
      }
      const float distance = NeuronCore::Length(CenterOf(m_cells[voxel]) - _point);
      if (distance <= best)
      {
        best = distance;
        found = true;
      }
    }
  }
  return found ? std::optional(best) : std::nullopt;
}

bool IsGone(std::span<const std::uint8_t> _gone, std::uint32_t _voxel) noexcept
{
  const std::size_t byte = _voxel / 8u;
  return byte < _gone.size() && ((_gone[byte] >> (_voxel % 8u)) & 1u) != 0u;
}

bool CutsNothing(const VoxelBody& _body, std::span<const std::uint8_t> _gone, std::span<const std::uint32_t> _removed)
{
  std::vector<std::uint32_t> beside;
  for (const std::uint32_t voxel : _removed)
  {
    for (const std::uint32_t neighbor : _body.FaceNeighbors(voxel))
    {
      if (neighbor != NeuronCore::NO_VOXEL && !IsGone(_gone, neighbor))
      {
        beside.push_back(neighbor);
      }
    }
  }
  std::ranges::sort(beside);
  const auto repeated = std::ranges::unique(beside);
  beside.erase(repeated.begin(), repeated.end());
  if (beside.size() < 2)
  {
    return true;
  }
  // From the first, through what remains, nearest first, until every other is found or the search has read its fill.
  std::vector<std::uint8_t> seen(_body.Cells().size(), 0);
  seen[beside.front()] = 1;
  std::vector<std::uint32_t> open{beside.front()};
  std::size_t read = 1;
  std::size_t found = 1;
  for (std::size_t next = 0; next < open.size() && read < LOCAL_SEARCH_VOXELS; ++next)
  {
    for (const std::uint32_t neighbor : _body.FaceNeighbors(open[next]))
    {
      if (neighbor == NeuronCore::NO_VOXEL || seen[neighbor] != 0 || IsGone(_gone, neighbor))
      {
        continue;
      }
      seen[neighbor] = 1;
      ++read;
      open.push_back(neighbor);
      if (std::ranges::binary_search(beside, neighbor) && ++found == beside.size())
      {
        return true;
      }
    }
  }
  return false;
}

NeuronCore::RigidTransform PlaceOf(const VoxelBody& _body, Float3 _position, const NeuronCore::Rotation& _rotation) noexcept
{
  return {_rotation, _position - NeuronCore::RotateVector(_rotation, _body.Middle())};
}

ShotOutcome SweepShot(const Shot& _shot, std::span<const SweptEntity> _entities, std::vector<SpentDamage>& _spent)
{
  // Every voxel the shot enters, of every entity it may meet, in the order it enters them: each entity's by the shot's
  // motion relative to it, over the same span of time, so that their fractions compare.
  struct Meeting
  {
    double fraction;
    std::uint32_t id;
    std::size_t entity;
    std::uint32_t voxel;
  };
  std::vector<Meeting> meetings;
  for (std::size_t index = 0; index < _entities.size(); ++index)
  {
    const SweptEntity& entity = _entities[index];
    if (entity.id == _shot.shooter)
    {
      continue;
    }
    const Float3 relative = _shot.displacement - entity.velocity * _shot.seconds;
    if (SegmentDistance(_shot.origin, relative,
                        entity.place.translation + NeuronCore::RotateVector(entity.place.rotation, entity.body->Middle())) >
        entity.body->Radius() + SPHERE_MARGIN)
    {
      continue;
    }
    const Float3 origin = NeuronCore::InverseTransformPoint(entity.place, _shot.origin);
    const Float3 direction = NeuronCore::UnrotateVector(entity.place.rotation, relative);
    for (const NeuronCore::SegmentCell& cell : NeuronCore::SegmentCells(entity.body->Grid(), Doubled(origin), Doubled(direction), 0.0, 1.0))
    {
      if (!IsGone(entity.gone, cell.value))
      {
        meetings.push_back({cell.entry, entity.id, index, cell.value});
      }
    }
  }
  std::ranges::sort(meetings, [](const Meeting& _a, const Meeting& _b)
                    { return std::tie(_a.fraction, _a.id, _a.voxel) < std::tie(_b.fraction, _b.id, _b.voxel); });

  ShotOutcome outcome{false, 1.0, false};
  float remaining = _shot.damage;
  std::vector<Spending> spending;
  const auto spend = [&spending, &outcome, &_shot, _entities](std::size_t _entity, std::uint32_t _voxel, float _damage, bool _destroyed)
  {
    Spending* spent = FindSpending(spending, _entity, _voxel);
    if (spent == nullptr)
    {
      spending.push_back({_entity, _voxel, 0.0f, false});
      spent = &spending.back();
    }
    spent->damage += _damage;
    spent->destroyed = spent->destroyed || _destroyed;
    outcome.damagedTarget = outcome.damagedTarget || _entities[_entity].id == _shot.target;
  };
  const auto health = [&spending, _entities](std::size_t _entity, std::uint32_t _voxel)
  {
    const SweptEntity& entity = _entities[_entity];
    const Spending* spent = FindSpending(spending, _entity, _voxel);
    return entity.toughness[_voxel] - (entity.damage.empty() ? 0.0f : entity.damage[_voxel]) - (spent != nullptr ? spent->damage : 0.0f);
  };
  for (const Meeting& meeting : meetings)
  {
    const Spending* before = FindSpending(spending, meeting.entity, meeting.voxel);
    if (before != nullptr && before->destroyed)
    {
      continue;
    }
    const SweptEntity& entity = _entities[meeting.entity];
    if (!entity.damageable || entity.side == _shot.side)
    {
      outcome.stopped = true;
      outcome.stopFraction = meeting.fraction;
      break;
    }

    // Voxel by voxel along its line, each taking what it has left, while the shot has that much.
    const float left = health(meeting.entity, meeting.voxel);
    if (remaining >= left)
    {
      spend(meeting.entity, meeting.voxel, left, true);
      remaining -= left;
      if (remaining > 0.0f)
      {
        continue;
      }
      outcome.stopped = true;
      outcome.stopFraction = meeting.fraction;
      break;
    }

    // It stops here, and what it has left spreads evenly over this voxel and the others within its reach of it.
    const Int3 landed = entity.body->Cells()[meeting.voxel];
    std::vector<std::uint32_t> reached;
    for (const ReachOffset& offset : ReachOffsets(_shot.reachUnits))
    {
      const std::uint32_t voxel = entity.body->Grid().VoxelAt(landed + offset.offset);
      const Spending* spent = voxel != NeuronCore::NO_VOXEL ? FindSpending(spending, meeting.entity, voxel) : nullptr;
      if (voxel != NeuronCore::NO_VOXEL && !IsGone(entity.gone, voxel) && (spent == nullptr || !spent->destroyed))
      {
        reached.push_back(voxel);
      }
    }
    std::ranges::sort(reached);
    const float share = remaining / static_cast<float>(reached.size());
    for (const std::uint32_t voxel : reached)
    {
      const float voxelLeft = health(meeting.entity, voxel);
      spend(meeting.entity, voxel, std::min(share, voxelLeft), share >= voxelLeft);
    }
    remaining = 0.0f;
    outcome.stopped = true;
    outcome.stopFraction = meeting.fraction;
    break;
  }
  for (const Spending& spent : spending)
  {
    _spent.push_back({spent.entity, spent.voxel, spent.damage, spent.destroyed});
  }
  return outcome;
}

bool LineBlocked(Float3 _from, Float3 _to, std::uint8_t _side, std::span<const SweptEntity> _entities)
{
  const Float3 displacement = _to - _from;
  for (const SweptEntity& entity : _entities)
  {
    if (entity.side != _side ||
        SegmentDistance(_from, displacement,
                        entity.place.translation + NeuronCore::RotateVector(entity.place.rotation, entity.body->Middle())) >
          entity.body->Radius() + SPHERE_MARGIN)
    {
      continue;
    }
    const Float3 origin = NeuronCore::InverseTransformPoint(entity.place, _from);
    const Float3 direction = NeuronCore::UnrotateVector(entity.place.rotation, displacement);
    for (const NeuronCore::SegmentCell& cell : NeuronCore::SegmentCells(entity.body->Grid(), Doubled(origin), Doubled(direction), 0.0, 1.0))
    {
      if (!IsGone(entity.gone, cell.value))
      {
        return true;
      }
    }
  }
  return false;
}

std::optional<Lead> LeadShot(Float3 _muzzle, Float3 _point, Float3 _velocity, float _speed) noexcept
{
  // |d + v t| = s t, for the least t > 0: (v.v - s^2) t^2 + 2 (d.v) t + d.d = 0.
  const std::array<double, 3> d = Doubled(_point - _muzzle);
  const std::array<double, 3> v = Doubled(_velocity);
  const double speed = _speed;
  const double a = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - speed * speed;
  const double b = 2.0 * (d[0] * v[0] + d[1] * v[1] + d[2] * v[2]);
  const double c = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
  double seconds = -1.0;
  if (std::abs(a) < 1.0e-9)
  {
    seconds = b < 0.0 ? -c / b : -1.0;
  }
  else
  {
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant >= 0.0)
    {
      const double root = std::sqrt(discriminant);
      const double first = (-b - root) / (2.0 * a);
      const double second = (-b + root) / (2.0 * a);
      const double low = std::min(first, second);
      const double high = std::max(first, second);
      seconds = low > 0.0 ? low : high;
    }
  }
  if (!(seconds > 0.0) || !std::isfinite(seconds))
  {
    return std::nullopt;
  }
  const std::array<double, 3> aim{d[0] + v[0] * seconds, d[1] + v[1] * seconds, d[2] + v[2] * seconds};
  const double length = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
  if (!(length > 0.0))
  {
    return std::nullopt;
  }
  return Lead{{static_cast<float>(aim[0] / length), static_cast<float>(aim[1] / length), static_cast<float>(aim[2] / length)},
              static_cast<float>(seconds)};
}

} // namespace GameLogic
