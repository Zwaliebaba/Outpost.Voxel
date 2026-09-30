#include "pch.h"

#include "SkirmishLayout.h"

#include "Hash.h"

#include <algorithm>
#include <cmath>

namespace GameCore
{
namespace
{

using NeuronCore::Int3;

// The layout's randomness: PcgHash of an index and a stream, offset by the seed, as the sector's is (Design/ADR/ADR-017).
constexpr std::uint32_t SEED_STEP = 0x9E3779B9u;
constexpr std::uint32_t STREAMS = 8;

enum class Stream : std::uint32_t
{
  OffsetX,
  OffsetY,
  OffsetZ,
  Model,
  Turn
};

// How many places the generator tries for an asteroid before it leaves the asteroid out. At the fields' counts and
// spacing a field is under a fifth full: over seeds 0 to 99,999 it left none out.
constexpr std::uint32_t PLACEMENT_TRIES = 256;

class LayoutRandom
{
public:
  explicit LayoutRandom(std::uint32_t _seed) noexcept
    : m_seed(_seed)
  {
  }

  [[nodiscard]] std::uint32_t Bits(std::uint32_t _index, Stream _stream) const noexcept
  {
    return NeuronCore::PcgHash(_index * STREAMS + static_cast<std::uint32_t>(_stream) + m_seed * SEED_STEP);
  }

  // Uniform in [-_reach, _reach], for a _reach not below 0, within a whisker of it: the top bits of a draw scaled by multiplication, as
  // SeededRandom's Below is, so every build computes the same integer.
  [[nodiscard]] std::int32_t Within(std::uint32_t _index, Stream _stream, std::int32_t _reach) const noexcept
  {
    const std::uint64_t span = 2 * static_cast<std::uint64_t>(_reach) + 1;
    return static_cast<std::int32_t>((static_cast<std::uint64_t>(Bits(_index, _stream)) * span) >> 32u) - _reach;
  }

  // Uniform in [0, _count).
  [[nodiscard]] std::uint32_t Below(std::uint32_t _index, Stream _stream, std::uint32_t _count) const noexcept
  {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(Bits(_index, _stream)) * _count) >> 32u);
  }

private:
  std::uint32_t m_seed;
};

[[nodiscard]] std::int64_t DistanceSquared(Int3 _a, Int3 _b) noexcept
{
  const Int3 d = _a - _b;
  return std::int64_t{d.x} * d.x + std::int64_t{d.y} * d.y + std::int64_t{d.z} * d.z;
}

// The cube's 24 rotations: every signed permutation of the axes that turns rather than reflects, in the order of the
// permutations and then of the signs.
[[nodiscard]] constexpr std::array<NeuronCore::Rotation, 24> MakeCubeRotations() noexcept
{
  constexpr std::array<std::array<std::uint32_t, 3>, 6> PERMUTATIONS{{{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
  const auto axis = [](std::uint32_t _axis, bool _negative)
  {
    const float sign = _negative ? -1.0f : 1.0f;
    return NeuronCore::Float3{_axis == 0u ? sign : 0.0f, _axis == 1u ? sign : 0.0f, _axis == 2u ? sign : 0.0f};
  };
  std::array<NeuronCore::Rotation, 24> rotations{};
  std::size_t next = 0;
  for (const std::array<std::uint32_t, 3>& permutation : PERMUTATIONS)
  {
    for (std::uint32_t signs = 0; signs < 8u; ++signs)
    {
      const NeuronCore::Rotation rotation{axis(permutation[0], (signs & 1u) != 0u), axis(permutation[1], (signs & 2u) != 0u),
                                          axis(permutation[2], (signs & 4u) != 0u)};
      if (NeuronCore::Dot(NeuronCore::Cross(rotation.axisX, rotation.axisY), rotation.axisZ) > 0.0f)
      {
        rotations[next++] = rotation;
      }
    }
  }
  return rotations;
}

constexpr std::array<NeuronCore::Rotation, 24> CUBE_ROTATIONS = MakeCubeRotations();

} // namespace

Anchor HalfTurn(const Anchor& _anchor) noexcept
{
  return {HalfTurn(_anchor.position), NeuronCore::ComposeRotations(HALF_TURN, _anchor.turn)};
}

std::span<const NeuronCore::Rotation, 24> CubeRotations() noexcept
{
  return CUBE_ROTATIONS;
}

SkirmishLayout MakeSkirmishLayout(std::uint32_t _seed)
{
  SkirmishLayout layout;

  // Side 1's core and ships, facing side 2, then side 2's, at their half turns.
  layout.units.push_back({CORE_DESIGN, 1, {CORE_ANCHOR, FACING_SIDE_2}});
  for (std::size_t ship = 0; ship < STARTING_SHIPS.size(); ++ship)
  {
    layout.units.push_back({STARTING_SHIPS[ship], 1, {STARTING_SHIP_ANCHORS[ship], FACING_SIDE_2}});
  }
  const std::size_t sideUnits = layout.units.size();
  for (std::size_t unit = 0; unit < sideUnits; ++unit)
  {
    const LayoutUnit& first = layout.units[unit];
    layout.units.push_back({first.design, 2, HalfTurn(first.anchor)});
  }

  // Each of the half's fields, asteroid by asteroid: a place within the field's radius on the plane and its height above
  // or below it, far enough from the field's other asteroids, then a model and a turn. An asteroid is drawn from the
  // index of its field, itself and its try, so that one asteroid's tries never shift another's.
  const LayoutRandom random(_seed);
  for (std::uint32_t field = 0; field < HALF_FIELDS.size(); ++field)
  {
    const FieldSpec& spec = HALF_FIELDS[field];
    const std::size_t firstAsteroid = layout.asteroids.size();
    for (std::uint32_t asteroid = 0; asteroid < spec.asteroids; ++asteroid)
    {
      for (std::uint32_t attempt = 0; attempt < PLACEMENT_TRIES; ++attempt)
      {
        const std::uint32_t index = (field * 64u + asteroid) * PLACEMENT_TRIES + attempt;
        const Int3 offset{random.Within(index, Stream::OffsetX, spec.radius), random.Within(index, Stream::OffsetY, ASTEROID_HEIGHT),
                          random.Within(index, Stream::OffsetZ, spec.radius)};
        if (std::int64_t{offset.x} * offset.x + std::int64_t{offset.z} * offset.z > std::int64_t{spec.radius} * spec.radius)
        {
          continue;
        }
        const Int3 position = spec.center + offset;
        const bool crowded =
          std::any_of(layout.asteroids.begin() + static_cast<std::ptrdiff_t>(firstAsteroid), layout.asteroids.end(),
                      [position](const LayoutAsteroid& _other)
                      { return DistanceSquared(position, _other.anchor.position) < std::int64_t{ASTEROID_SPACING} * ASTEROID_SPACING; });
        if (crowded)
        {
          continue;
        }
        layout.asteroids.push_back({random.Below(index, Stream::Model, static_cast<std::uint32_t>(ASTEROID_MODELS.size())),
                                    {position, CUBE_ROTATIONS[random.Below(index, Stream::Turn, 24u)]}});
        break;
      }
    }
  }
  const std::size_t halfAsteroids = layout.asteroids.size();
  for (std::size_t asteroid = 0; asteroid < halfAsteroids; ++asteroid)
  {
    const LayoutAsteroid& first = layout.asteroids[asteroid];
    layout.asteroids.push_back({first.model, HalfTurn(first.anchor)});
  }
  return layout;
}

SkirmishLayout MakeBattleLayout(std::uint32_t _seed)
{
  SkirmishLayout layout = MakeSkirmishLayout(_seed);
  layout.units.clear();
  layout.units.push_back({CORE_DESIGN, 1, {CORE_ANCHOR, FACING_SIDE_2}});
  for (std::size_t ship = 0; ship < BATTLE_FLEET.size(); ++ship)
  {
    layout.units.push_back({BATTLE_FLEET[ship], 1, {BATTLE_FLEET_ANCHORS[ship], FACING_SIDE_2}});
  }
  const std::size_t sideUnits = layout.units.size();
  for (std::size_t unit = 0; unit < sideUnits; ++unit)
  {
    const LayoutUnit& first = layout.units[unit];
    layout.units.push_back({first.design, 2, HalfTurn(first.anchor)});
  }
  return layout;
}

NeuronCore::Float3 AnchoredPosition(const Anchor& _anchor, NeuronCore::Float3 _middle) noexcept
{
  const NeuronCore::Float3 cell{std::floor(_middle.x), std::floor(_middle.y), std::floor(_middle.z)};
  const NeuronCore::Float3 anchor{static_cast<float>(_anchor.position.x), static_cast<float>(_anchor.position.y),
                                  static_cast<float>(_anchor.position.z)};
  return anchor + NeuronCore::RotateVector(_anchor.turn, _middle - cell);
}

} // namespace GameCore
