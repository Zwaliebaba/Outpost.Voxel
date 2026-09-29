#pragma once

#include "Clearances.h"
#include "ShipMotion.h"

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

// The skirmish's parameters (Design/MvpPlan.md §2.1): its seed, from which GameCore lays it out, and the rate it ticks at.
struct SkirmishParameters
{
  std::uint32_t seed = 1;
  std::uint32_t tickRate = 30;
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
// 1, then its u32 seed and its u32 tick rate, little-endian.
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
// side's entity is refused (Design/ADR/ADR-032). A side orders its ships to move, stop or hold, and they fly to their
// orders on the plane, around the cores and the asteroids, a group as one (Design/ADR/ADR-033); each side's snapshots
// carry its ships' orders in their payload. An entity detonates and is restored only on command, as the sector's do.
class Skirmish final : public NeuronServer::World
{
public:
  // The skirmish _parameters describe, with its models from _gameData, or why not.
  [[nodiscard]] static std::expected<std::unique_ptr<Skirmish>, SkirmishError> Create(const SkirmishParameters& _parameters,
                                                                                      const std::filesystem::path& _gameData);

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

  // The entities the side sees, and in the payload its ships' order states (GameCore::EncodeOrderStates): every side's
  // for the observer.
  void Describe(NeuronCore::Snapshot& _snapshot, std::uint8_t _side) const override;

private:
  Skirmish() = default;

  // What a ship is doing: nothing, flying to a move's destination, or holding where it halted.
  enum class Stance : std::uint8_t
  {
    Idle,
    Moving,
    Holding
  };

  // What a ship design flies with on the plane (ADR-033): its limits, and its clearances of the cores and the asteroids.
  struct ShipDesign
  {
    ShipClass limits;
    Clearances clearances;
  };

  // A ship and its flight on the plane (ADR-033).
  struct Ship
  {
    std::size_t entity; // its index in m_entities
    std::size_t design; // its index in m_designs
    ShipMotion motion;
    Stance stance;
    std::vector<NeuronCore::Float3> path; // a move's: where it was ordered from, the corners, then its destination
    std::size_t next;                     // the point of the path it steers for
    float pace;                           // the speed its group's move holds it to
    ShipClass limits;                     // its design's, with its group's acceleration and turn while it moves with one
  };

  struct Entity
  {
    std::uint16_t composite;
    std::uint8_t side;
    NeuronCore::Float3 position; // where the middle of its composite's box stands
    NeuronCore::Quaternion rotation;
    float sensorRangeUnits; // its design's, and 0 for an asteroid, which senses nothing
    std::optional<NeuronCore::DetonationEvent> detonation;
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

  // What moving ship _ship steers by to keep apart from the other whole ships, from where they all are.
  [[nodiscard]] NeuronCore::Float3 Avoidance(std::size_t _ship) const noexcept;

  // Whether moving ship _ship has come as near its destination as a ship halted at it lets it.
  [[nodiscard]] bool DestinationTaken(std::size_t _ship) const noexcept;

  // Whether side _side sees _entity (G21, ADR-032): an observer sees everything, and a side its own entities and every
  // entity whose middle lies within the sensor range of one of its intact entities, measured from that entity's middle.
  [[nodiscard]] bool Sees(std::uint8_t _side, const Entity& _entity) const noexcept;

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
};

} // namespace GameLogic
