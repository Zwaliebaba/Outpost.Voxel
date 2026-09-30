#pragma once

#include "Float3.h"
#include "RigidTransform.h"
#include "VoxelGrid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace GameLogic
{

// How a tick's shots resolve (Design/GameConcept.md §8.2, G39, G72; Design/ADR/ADR-035): each is swept against every
// entity as the tick began, each spends its damage by the same rule against what it meets, and only then is the damage
// of every shot summed and applied. So no shot sees another's, and no side's resolve first.

// What a composite is, to the shots that meet it: its voxels' cells in the composite's order, a grid over them, and its
// box's middle and sphere. Its voxels are grouped in bricks, so that a query of the voxels near a point reads few.
class VoxelBody
{
public:
  explicit VoxelBody(std::vector<NeuronCore::Int3> _cells);

  [[nodiscard]] std::span<const NeuronCore::Int3> Cells() const noexcept
  {
    return m_cells;
  }

  [[nodiscard]] const NeuronCore::VoxelGrid& Grid() const noexcept
  {
    return m_grid;
  }

  // The middle of the composite's box, where an entity of it stands, and the radius of the sphere about it that holds
  // every voxel.
  [[nodiscard]] NeuronCore::Float3 Middle() const noexcept
  {
    return m_middle;
  }

  [[nodiscard]] float Radius() const noexcept
  {
    return m_radius;
  }

  // The voxels that share a face with voxel _voxel, NeuronCore::NO_VOXEL where none does: what connects a composite's
  // voxels (§8.3 of the concept).
  [[nodiscard]] const std::array<std::uint32_t, 6>& FaceNeighbors(std::uint32_t _voxel) const noexcept
  {
    return m_faceNeighbors[_voxel];
  }

  // The nearest distance from _point, in the composite's space, to the center of a voxel that _gone does not hold, if
  // one lies within _limit; else nothing. _gone is an EntityMask's bits, or empty.
  [[nodiscard]] std::optional<float> NearestVoxel(NeuronCore::Float3 _point, std::span<const std::uint8_t> _gone, float _limit) const;

private:
  struct Brick
  {
    NeuronCore::Float3 lower; // the box around its voxels' centers
    NeuronCore::Float3 upper;
    std::uint32_t first; // its voxels in m_brickVoxels
    std::uint32_t count;
  };

  std::vector<NeuronCore::Int3> m_cells;
  NeuronCore::VoxelGrid m_grid;
  NeuronCore::Float3 m_middle{};
  float m_radius = 0.0f;
  std::vector<Brick> m_bricks;
  std::vector<std::uint32_t> m_brickVoxels;
  std::vector<std::array<std::uint32_t, 6>> m_faceNeighbors;
};

// Whether voxel _voxel is gone by _gone, an EntityMask's bits.
[[nodiscard]] bool IsGone(std::span<const std::uint8_t> _gone, std::uint32_t _voxel) noexcept;

// Whether removing _removed from _body, whose voxels _gone now holds gone with them, can have cut nothing off: the voxels
// that remain beside them, face to face, all still connect to one another within a search of a few hundred voxels. Any way
// that ran through a removed voxel can then go around it. False does not say something was cut off, only that a search of
// the whole must say.
[[nodiscard]] bool CutsNothing(const VoxelBody& _body, std::span<const std::uint8_t> _gone, std::span<const std::uint32_t> _removed);

// An entity as a tick's shots meet it, as the tick began (G72).
struct SweptEntity
{
  std::uint32_t id;
  std::uint8_t side;                // 0 for none
  bool damageable;                  // a design's; an asteroid's voxels stop a shot and take nothing
  const VoxelBody* body;            // its composite's
  NeuronCore::RigidTransform place; // from its composite's space, about the box's middle, into the world
  NeuronCore::Float3 velocity;
  std::span<const std::uint8_t> gone; // its mask's bits; empty while it is whole
  std::span<const float> damage;      // each voxel's damage; empty while it has taken none
  std::span<const float> toughness;   // each voxel's
};

// Where an entity of _body standing at _position, turned by _rotation, puts its composite's space.
[[nodiscard]] NeuronCore::RigidTransform PlaceOf(const VoxelBody& _body, NeuronCore::Float3 _position,
                                                 const NeuronCore::Rotation& _rotation) noexcept;

// A shot over one tick: a shell's flight through it, or a beam's reach, at once.
struct Shot
{
  NeuronCore::Float3 origin;
  NeuronCore::Float3 displacement; // a shell's velocity times the tick, or a beam's direction times its range
  float seconds;                   // how long the sweep spans: the tick for a shell, and none for a beam
  float damage;
  float reachUnits;
  std::uint32_t shooter; // the entity that fired it, which it never meets
  std::uint8_t side;
  std::uint32_t target; // the entity it was fired at, 0 for none
};

// What a shot spent on one voxel: the entity's index among those swept, the voxel, the damage, and whether that was all
// the voxel had left, so that it goes whatever the rounding of the sum.
struct SpentDamage
{
  std::size_t entity;
  std::uint32_t voxel;
  float damage;
  bool destroyed;
};

struct ShotOutcome
{
  bool stopped;        // it met what it did not pass, or spent all its damage
  double stopFraction; // where along its sweep it stopped, 0 to 1; 1 when it did not
  bool damagedTarget;  // it spent damage on its target
};

// Sweeps _shot through _entities (G39): it meets their voxels in the order it enters them, moving relative to each as
// the tick began. It passes its shooter, and stops at its own side's voxels and an asteroid's without damage. It spends
// its damage on an enemy's voxels one by one along its line, each taking what it has left, and carries the rest on; at
// the first voxel it cannot take, it stops, and what it has left spreads evenly over that voxel and the others within its
// reach of it, each up to what it has left. Appends what it spent to _spent.
[[nodiscard]] ShotOutcome SweepShot(const Shot& _shot, std::span<const SweptEntity> _entities, std::vector<SpentDamage>& _spent);

// Whether the segment from _from to _to meets a voxel of an entity of _entities of side _side, other than those _gone
// holds: a line its own side's hulls block (G71).
[[nodiscard]] bool LineBlocked(NeuronCore::Float3 _from, NeuronCore::Float3 _to, std::uint8_t _side,
                               std::span<const SweptEntity> _entities);

// A shell's lead (G58): the direction a shell fired from _muzzle at _speed takes to meet a point now at _point moving at
// _velocity, and the seconds it flies; nothing when it cannot.
struct Lead
{
  NeuronCore::Float3 direction;
  float seconds;
};

[[nodiscard]] std::optional<Lead> LeadShot(NeuronCore::Float3 _muzzle, NeuronCore::Float3 _point, NeuronCore::Float3 _velocity,
                                           float _speed) noexcept;

} // namespace GameLogic
