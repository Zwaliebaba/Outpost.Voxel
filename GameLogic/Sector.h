#pragma once

#include "Route.h"
#include "ShipMotion.h"

#include "World.h"

#include "Float3.h"
#include "Message.h"
#include "RigidTransform.h"

#include <array>
#include <cstddef>
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

// The world's parameters, with their defaults (Design/SpaceScene.md §5.2, §5.4, Design/ADR/ADR-017).
struct SectorParameters
{
  std::uint32_t seed = 1;
  std::uint32_t stations = 4;
  std::uint32_t frigates = 40;
  std::uint32_t capitalShips = 8;
  float stationSpacing = 1000.0f;     // the least distance between two stations' centers
  float debrisLifetimeSeconds = 0.0f; // 0: debris lasts until it is restored (§5.5)
  std::uint32_t tickRate = 30;
};

// Why a parameter block was refused, before anything runs (§5.2).
enum class SectorRefusal : std::uint8_t
{
  BadParameter,         // a tick rate of 0, a spacing that is not positive, a lifetime below 0, or a value not finite
  ModelNotLoaded,       // a model's file is missing, or its reader refused it
  ShipsWithoutStations, // ships, and no station for their routes to orbit
  SpacingTooSmall,      // stations too close for an orbit to keep clear of the next station
  DoesNotFit            // a layout that would leave the world's bound (S12), or stations its region cannot hold apart
};

[[nodiscard]] const char* SectorRefusalName(SectorRefusal _refusal) noexcept;

struct SectorError
{
  SectorRefusal refusal;
  std::string detail; // which model, and why, or which value
};

// The manifest's models, by their index in it.
inline constexpr std::uint16_t STATION_MODEL = 0;
inline constexpr std::uint16_t CAPITAL_SHIP_MODEL = 1;
inline constexpr std::uint16_t FRIGATE_MODEL = 2;
inline constexpr std::size_t MODEL_COUNT = 3;

// Every entity lies within this far of the origin on every axis (S12, §7.5).
inline constexpr float WORLD_BOUND = 16384.0f;

// How far a ship keeps from a station's sphere, beyond its own (§5.2).
inline constexpr float KEEP_OUT_MARGIN = 50.0f;

// A sector of space, the space scene's world (§5): stations laid out from a seed, flights of ships on routes around and
// between them, and the detonations of either. It loads its models itself, to measure them and to hash their files for
// the welcome.
class Sector final : public NeuronServer::World
{
public:
  // The world _parameters describe, with its models from _modelDirectory, or why not.
  [[nodiscard]] static std::expected<std::unique_ptr<Sector>, SectorError> Create(const SectorParameters& _parameters,
                                                                                  const std::filesystem::path& _modelDirectory);

  ~Sector() override = default;
  Sector(const Sector&) = delete;
  Sector& operator=(const Sector&) = delete;
  Sector(Sector&&) = delete;
  Sector& operator=(Sector&&) = delete;

  [[nodiscard]] std::uint32_t TickRate() const noexcept override;
  [[nodiscard]] const NeuronCore::WorldSettings& Settings() const noexcept override;
  [[nodiscard]] std::span<const NeuronCore::ManifestEntry> Manifest() const noexcept override;
  void Advance(std::uint64_t _worldTick) override;
  void Detonate(std::uint32_t _entity, std::uint64_t _worldTick) override;
  void Restore(std::uint32_t _entity) override;
  void Describe(NeuronCore::Snapshot& _snapshot) const override;

  // The sphere of model _model, about the center of its occupied box.
  [[nodiscard]] float ModelRadius(std::uint16_t _model) const noexcept;

  // The class of the ships of model _shipModel, a capital ship or a frigate.
  [[nodiscard]] const ShipClass& ClassOf(std::uint16_t _shipModel) const noexcept;

  // How far a ship of model _shipModel keeps its center from a station's: the two spheres and the margin (§5.2).
  [[nodiscard]] float KeepOutRadius(std::uint16_t _shipModel) const noexcept;

  [[nodiscard]] std::size_t FlightCount() const noexcept;

  // The ids of flight _flight's ships, in rank order: its first leader first.
  [[nodiscard]] std::span<const std::uint32_t> FlightMembers(std::size_t _flight) const noexcept;

  // The id of the ship leading flight _flight, or 0 when none of its ships is whole.
  [[nodiscard]] std::uint32_t FlightLeader(std::size_t _flight) const noexcept;

  [[nodiscard]] const Route& FlightRoute(std::size_t _flight) const noexcept;

private:
  struct Entity
  {
    std::uint32_t id;
    std::uint16_t model;
    NeuronCore::Float3 position; // a station's; a ship's is its motion's
    NeuronCore::Rotation rotation;
    ShipMotion motion;
    std::size_t flight;
    std::optional<NeuronCore::DetonationEvent> detonation;
    bool removed;
  };

  struct Flight
  {
    std::vector<std::uint32_t> members;
    Route route;
    std::uint32_t leader;
  };

  Sector() = default;

  [[nodiscard]] bool IsShip(const Entity& _entity) const noexcept;
  [[nodiscard]] bool IsWhole(std::uint32_t _id) const noexcept;
  [[nodiscard]] Entity* Find(std::uint32_t _id) noexcept;

  SectorParameters m_parameters{};
  NeuronCore::WorldSettings m_settings{};
  std::vector<NeuronCore::ManifestEntry> m_manifest;
  std::array<float, MODEL_COUNT> m_radii{};
  std::array<ShipClass, MODEL_COUNT> m_classes{};
  std::vector<Entity> m_entities; // entity id is its index plus one
  std::vector<Flight> m_flights;
  std::uint64_t m_lifetimeTicks = 0; // 0: debris lasts
  std::uint32_t m_detonations = 0;
};

} // namespace GameLogic
