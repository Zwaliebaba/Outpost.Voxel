#pragma once

#include "Clearances.h"
#include "Combat.h"
#include "ShipMotion.h"

#include "CombatProfile.h"
#include "SkirmishLayout.h"

#include "World.h"

#include "Float3.h"
#include "Message.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace GameLogic
{

// The skirmish's parameters (Design/MvpPlan.md §2.1): its seed, from which GameCore lays it out, and the rate it ticks at;
// whether each side starts with a battle fleet of its whole command budget instead of its ships (Design/ADR/ADR-035); and
// whether ships under fire jink, which only a measure of jinking turns off (G58).
struct SkirmishParameters
{
  std::uint32_t seed = 1;
  std::uint32_t tickRate = 30;
  bool battle = false;
  bool jinking = true;
};

// Why a skirmish was refused, before anything runs.
enum class SkirmishRefusal : std::uint8_t
{
  BadParameter,   // a tick rate of 0
  ModelNotLoaded, // a model's file is missing, or NVF's reader refused it
  DesignRefused   // a design of the catalogue failed its validation, or its composite could not be made
};

[[nodiscard]] const char* SkirmishRefusalName(SkirmishRefusal _refusal) noexcept;

// The skirmish's description in its command log, from which a replay makes it again (Design/ADR/ADR-032): a u8 version,
// 2, then its u32 seed and its u32 tick rate, little-endian, and a u8 of flags: 1 for a battle, 2 for no jinking.
[[nodiscard]] std::vector<std::uint8_t> EncodeSkirmishParameters(const SkirmishParameters& _parameters);

// The parameters _bytes describe, or nothing for bytes of another version or length.
[[nodiscard]] std::optional<SkirmishParameters> DecodeSkirmishParameters(std::span<const std::uint8_t> _bytes) noexcept;

struct SkirmishError
{
  SkirmishRefusal refusal;
  std::string detail; // which model or design, and why
};

// The MVP's world (Design/MvpPlan.md §5, phase 2; Design/ADR/ADR-030): the skirmish GameCore lays out from the seed. The
// cores and the ships are their designs' composites, of their sides; the asteroids are their models alone, of none. Its
// welcome names the models, the composites of every design of the catalogue and of every asteroid, the sides' colors, and
// in its payload their names. Each side sees its own entities and what their sensors reach, and a command on the other
// side's entity is refused (Design/ADR/ADR-032). A side orders its ships to move, stop, hold, attack or attack-move, and
// they fly to their orders on the plane, around the cores and the asteroids, a group as one (Design/ADR/ADR-033).
//
// Ships and the cores' turrets fight (Design/ADR/ADR-035): each tick, every armed entity keeps or finds its targets
// among what its side sees (G71), each weapon fires within its arc along a line its own side's hulls leave clear, shells
// leading their targets (G58), and every shot of the tick is swept against the entities as the tick began (G72). Damage
// removes voxels; a module fails below GameCore::FAIL_SHARE of its voxels; a failed command module or reactor loses the
// entity, which detonates, and what is cut off from its command module becomes debris of its own. Debris lasts
// DEBRIS_SECONDS. Each side's snapshots carry each entity's mask, and in their payload its ships' orders and the shots its
// sensors see (G54). An entity detonates and is restored on command too, as the sector's are.
class Skirmish final : public NeuronServer::World
{
public:
  // How long debris lasts, from its detonation, before it leaves the world (§8.3 of the concept).
  static constexpr float DEBRIS_SECONDS = 60.0f;

  // The skirmish _parameters describe, with its models from _gameData, or why not.
  [[nodiscard]] static std::expected<std::unique_ptr<Skirmish>, SkirmishError> Create(const SkirmishParameters& _parameters,
                                                                                      const std::filesystem::path& _gameData);

  // The skirmish of _layout instead of the one _parameters' seed lays out, which the seed still draws for: the duels of
  // the tests and of the smoke check. _layout's units name designs of the catalogue.
  [[nodiscard]] static std::expected<std::unique_ptr<Skirmish>, SkirmishError>
  Create(const SkirmishParameters& _parameters, const std::filesystem::path& _gameData, const GameCore::SkirmishLayout& _layout);

  ~Skirmish() override = default;
  Skirmish(const Skirmish&) = delete;
  Skirmish& operator=(const Skirmish&) = delete;
  Skirmish(Skirmish&&) = delete;
  Skirmish& operator=(Skirmish&&) = delete;

  [[nodiscard]] std::uint32_t TickRate() const noexcept override;
  [[nodiscard]] const NeuronCore::WorldSettings& Settings() const noexcept override;
  [[nodiscard]] std::span<const NeuronCore::ManifestEntry> Manifest() const noexcept override;
  [[nodiscard]] std::span<const NeuronCore::CompositeModel> Composites() const noexcept override;
  [[nodiscard]] std::span<const NeuronCore::SideColor> Sides() const noexcept override;
  [[nodiscard]] std::span<const std::uint8_t> WelcomePayload() const noexcept override;
  void Advance(std::uint64_t _worldTick) override;
  void Detonate(std::uint32_t _entity, std::uint64_t _worldTick) override;
  void Restore(std::uint32_t _entity) override;

  // A detonation or a restore of the other side's entity (G34), and an order (ADR-033) that does not decode, names an
  // entity that is no ship, or names another side's ship. An observer is no side, and an asteroid no side's.
  [[nodiscard]] std::optional<NeuronServer::CommandRefusal> Refuses(const NeuronCore::Command& _command, std::uint8_t _side) const override;

  // An order, which Refuses has let pass: a ship detonated since is left out of it.
  void ApplyGameCommand(std::span<const std::uint8_t> _payload, std::uint8_t _side) override;

  // The entities the side sees, with their masks, and in the payload its ships' order states and the shots it sees
  // (GameCore::EncodeSnapshotPayload): every side's for the observer.
  void Describe(NeuronCore::Snapshot& _snapshot, std::uint8_t _side) const override;

  // How an entity was lost to combat: its command module failed, or a reactor did (§8.3).
  enum class LossKind : std::uint8_t
  {
    Command,
    Reactor
  };

  struct Loss
  {
    std::uint32_t entity;
    std::uint64_t worldTick;
    LossKind kind;
  };

  // What combat has done to and by one entity, for the tests and the smoke check.
  struct CombatRecord
  {
    std::uint32_t shellsFired; // by it
    std::uint32_t beamTicks;   // ticks its beams fired
    std::uint32_t shellsAt;    // fired at it
    std::uint32_t shellsHit;   // of those, the ones that damaged it
    std::uint32_t voxelsLost;  // to damage, and to what was cut off
  };

  // Every loss to combat so far, in the order they happened.
  [[nodiscard]] std::span<const Loss> Losses() const noexcept
  {
    return m_losses;
  }

  [[nodiscard]] CombatRecord Record(std::uint32_t _entity) const noexcept;

private:
  Skirmish() = default;

  // What a ship is doing: nothing, flying to a move's destination, holding where it halted, closing on and fighting an
  // attack's target, or moving and fighting what comes within range on the way.
  enum class Stance : std::uint8_t
  {
    Idle,
    Moving,
    Holding,
    Attacking,
    AttackMoving
  };

  // What a ship design flies with on the plane (ADR-033): its limits, and its clearances of the cores and the asteroids.
  struct ShipDesign
  {
    ShipClass limits;
    Clearances clearances;
  };

  // A ship and its flight on the plane (ADR-033, ADR-035).
  struct Ship
  {
    std::size_t entity; // its index in m_entities
    std::size_t design; // its index in m_designs
    ShipMotion motion;
    Stance stance;
    std::vector<NeuronCore::Float3> path;      // a move's: where it was ordered from, the corners, then its destination
    std::size_t next;                          // the point of the path it steers for
    float pace;                                // the speed its group's move holds it to
    ShipClass limits;                          // its design's, with its group's acceleration and turn while it moves with one
    float thrustShare;                         // of its engines' thrust, what its working engines give
    std::uint32_t attackTarget;                // an attack's entity; 0 otherwise
    NeuronCore::Float3 destination;            // an attack-move's
    bool engaged;                              // an attack-move's, fighting on its way
    std::optional<NeuronCore::Float3> station; // where it keeps near while it bears on a target
    std::uint64_t pathTick;                    // when its path toward the entity it closes on was found
    NeuronCore::Float3 slide;                  // its velocity across its heading, from jinking (G58)
  };

  // A weapon of an armed entity, by its design's weapons (GameCore::CombatProfile): what it fires at and where.
  struct WeaponState
  {
    std::uint32_t target;    // 0 for none
    std::uint32_t aim;       // the voxel of the target's composite it aims at, while aimed
    bool aimed;              // whether aim is drawn for its target
    bool working;            // its module has not failed
    std::uint32_t shots;     // fired so far, which its draws count
    std::uint64_t readyTick; // the world tick from which it may fire a shell again
  };

  struct Entity
  {
    std::uint16_t composite;
    std::uint8_t side;
    NeuronCore::Float3 position; // where the middle of its composite's box stands
    NeuronCore::Quaternion rotation;
    float sensorRangeUnits; // its design's while its sensor works, and 0 for an asteroid, which senses nothing
    std::optional<NeuronCore::DetonationEvent> detonation;
    std::vector<std::uint8_t> gone;       // its mask's bits (NeuronCore::EntityMask); empty while it is whole
    std::vector<float> damage;            // each voxel's; empty while it has taken none
    std::vector<std::uint32_t> remaining; // for a design, each component's voxels that remain
    std::uint32_t target;                 // what it fights, kept (G71); 0 for nothing
    std::vector<WeaponState> weapons;     // for a design, one for each of its weapons
    bool underFire;                       // an enemy weapon targets it this tick
    bool piece;                           // cut off another entity: debris from the start
    bool expired;                         // debris that has outlasted DEBRIS_SECONDS, gone from the world
  };

  // A shell in flight (G58): where it is, how it moves, and what it spends.
  struct Shell
  {
    NeuronCore::Float3 position;
    NeuronCore::Float3 velocity;
    std::uint8_t side;
    std::uint32_t shooter;
    std::uint32_t target;
    float damage;
    float reachUnits;
    std::uint32_t ticksLeft; // until it has flown its weapon's range
    bool hit;                // it has damaged its target
  };

  // A beam that fired this tick, from its weapon to where it stopped or ran out of range.
  struct Beam
  {
    NeuronCore::Float3 from;
    NeuronCore::Float3 to;
    std::uint8_t side;
  };

  // The index in m_ships of the whole ship entity _id is, or of none: an id the skirmish does not hold, an entity that is
  // no ship, or a ship that has detonated.
  [[nodiscard]] std::optional<std::size_t> WholeShip(std::uint32_t _id) const noexcept;

  // Moves the whole ships of _ships to (_targetX, _targetZ), as a group that keeps its members' offsets from its middle.
  void Move(std::span<const std::uint32_t> _ships, float _targetX, float _targetZ);

  // Halts the whole ships of _ships, which then idle or hold.
  void Halt(std::span<const std::uint32_t> _ships, Stance _stance);

  // Ends _ship's move, if it has one, and leaves it in _stance with its design's limits.
  void EndMove(Ship& _ship, Stance _stance) const noexcept;

  // What ship _ship, flying along _course, steers by to keep apart from the other whole ships, from where they all are.
  [[nodiscard]] NeuronCore::Float3 Avoidance(std::size_t _ship, NeuronCore::Float3 _course) const noexcept;

  // Whether moving ship _ship has come as near its destination as a ship halted at it lets it.
  [[nodiscard]] bool DestinationTaken(std::size_t _ship) const noexcept;

  // Whether side _side sees _entity (G21, ADR-032): an observer sees everything, and a side its own entities and every
  // entity whose middle lies within the sensor range of one of its intact entities, measured from that entity's middle.
  [[nodiscard]] bool Sees(std::uint8_t _side, const Entity& _entity) const noexcept;

  // Whether side _side's sensors cover _point, as they do an entity's middle.
  [[nodiscard]] bool Covers(std::uint8_t _side, NeuronCore::Float3 _point) const noexcept;

  // The creation's common part, from _layout.
  [[nodiscard]] static std::expected<std::unique_ptr<Skirmish>, SkirmishError>
  Make(const SkirmishParameters& _parameters, const std::filesystem::path& _gameData, const GameCore::SkirmishLayout& _layout);

  // Whether _entity is in the world and whole: neither detonated nor expired.
  [[nodiscard]] static bool Intact(const Entity& _entity) noexcept;

  // Whether _entity is a design's entity another side may fight: intact and no piece.
  [[nodiscard]] bool Fightable(const Entity& _entity) const noexcept;

  // The combat tick's steps (ADR-035), in their order: debris that outlasts its time leaves; every armed entity keeps or
  // finds its targets; the weapons fire; every shot is swept and its damage applied; failures and losses follow; and
  // the survivors fly.
  void Expire(std::uint64_t _worldTick);
  [[nodiscard]] std::vector<SweptEntity> Swept() const;
  void Target(std::span<const SweptEntity> _swept, std::span<const std::uint8_t> _seen);
  void Fire(std::uint64_t _worldTick, std::span<const SweptEntity> _swept, std::vector<Shot>& _beams,
            std::vector<std::pair<std::size_t, std::size_t>>& _beamWeapons);
  // What a tick's damage removed from one entity.
  struct Removal
  {
    std::size_t entity;
    std::vector<std::uint32_t> voxels;
  };
  [[nodiscard]] std::vector<Removal> Resolve(std::span<const SweptEntity> _swept, std::span<const Shot> _beams,
                                             std::span<const std::pair<std::size_t, std::size_t>> _beamWeapons, float _seconds);
  void Suffer(std::span<const Removal> _removals, std::uint64_t _worldTick);
  void Fly(std::uint64_t _worldTick, float _seconds);

  // Which sides see each entity, a bit for each side's number.
  [[nodiscard]] std::vector<std::uint8_t> SeenBySides() const;

  // Where weapon _weapon of entity _entity fires from, and the way it faces, in the world.
  [[nodiscard]] NeuronCore::Float3 Muzzle(std::size_t _entity, std::size_t _weapon) const noexcept;
  [[nodiscard]] NeuronCore::Float3 Facing(std::size_t _entity, std::size_t _weapon) const noexcept;

  // Whether a target whose index is _target lies within _rangeUnits of _point, to its nearest voxel (G47).
  [[nodiscard]] bool WithinReach(NeuronCore::Float3 _point, std::size_t _target, float _rangeUnits) const;

  // Whether weapon _weapon of entity _entity may fire at entity _target: within its range and arc.
  [[nodiscard]] bool Bears(std::size_t _entity, std::size_t _weapon, std::size_t _target) const;

  // The world position of voxel _voxel of entity _entity's composite, its center.
  [[nodiscard]] NeuronCore::Float3 VoxelPosition(std::size_t _entity, std::uint32_t _voxel) const noexcept;

  // An entity's velocity, and zero for one that does not fly.
  [[nodiscard]] NeuronCore::Float3 EntityVelocity(std::size_t _entity) const noexcept;

  // Loses entity _entity at world tick _worldTick: it detonates with its velocity, and a ship forgets its order.
  void Lose(std::size_t _entity, std::uint64_t _worldTick);

  // The next detonation's seed (Design/Archive/SpaceScene.md §5.5).
  [[nodiscard]] std::uint32_t NextDetonationSeed() noexcept;

  // Whether ship _ship lies within range of its attack's target with every working weapon, less a margin (G47).
  [[nodiscard]] bool InAttackRange(const Ship& _ship, std::size_t _target) const;

  // One tick of ship _ship turning to bear on entity _target where it stands (G33).
  void Bear(Ship& _ship, std::size_t _target, float _seconds);

  // One tick of ship _ship closing on entity _target along a path around the clearances, found anew now and then.
  void Close(Ship& _ship, std::size_t _target, NeuronCore::Float3 _avoid, std::uint64_t _worldTick, float _seconds);

  // One tick of ship _ship's slide toward its jink's side speed, within the side thrust its vectored engines give.
  void Slide(Ship& _ship, std::uint64_t _worldTick, float _seconds) const noexcept;

  // The side speed ship _ship slides at to jink (G58), across its heading and to starboard positive, at _worldTick: drawn
  // anew every JINK_SECONDS while it is under fire, back toward its station once it has slid JINK_LEASH off it, and 0
  // otherwise.
  [[nodiscard]] float JinkSpeed(const Ship& _ship, std::uint64_t _worldTick) const noexcept;

  // Ship _ship's velocity: along its heading, and its slide across it.
  [[nodiscard]] static NeuronCore::Float3 VelocityOf(const Ship& _ship) noexcept;

  // Ship _ship's limits as it flies now: its move's, with what its working engines give of their thrust.
  [[nodiscard]] static ShipClass FlyingLimits(const Ship& _ship) noexcept;

  SkirmishParameters m_parameters{};
  NeuronCore::WorldSettings m_settings{};
  std::vector<NeuronCore::ManifestEntry> m_manifest;
  std::vector<NeuronCore::CompositeModel> m_composites;
  std::vector<NeuronCore::SideColor> m_sides;
  std::vector<std::uint8_t> m_welcomePayload;
  std::vector<Entity> m_entities;    // an entity's id is its index plus one
  std::vector<ShipDesign> m_designs; // the catalogue's ship designs, in its order
  std::vector<Ship> m_ships;         // in the order of their entities
  std::vector<std::size_t> m_shipOf; // by entity: its index in m_ships, or the largest std::size_t for one that is no ship
  std::uint32_t m_detonations = 0;

  // By composite: what shots meet, each voxel's toughness, whether it is a design's, which combat fights, and a design's
  // combat profile and sensor range; another's profile is empty, and its range 0.
  std::vector<VoxelBody> m_bodies;
  std::vector<std::vector<float>> m_toughness;
  std::vector<bool> m_designed;
  std::vector<GameCore::CombatProfile> m_profiles;
  std::vector<float> m_sensorRanges;

  std::vector<Shell> m_shells; // in the order they were fired
  std::vector<Beam> m_beams;   // this tick's
  std::vector<Loss> m_losses;
  std::vector<CombatRecord> m_records; // by entity
};

} // namespace GameLogic
