#pragma once

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

struct SkirmishError
{
  SkirmishRefusal refusal;
  std::string detail; // which model or design, and why
};

// The MVP's world (Design/MvpPlan.md §5, phase 2; Design/ADR/ADR-030): the skirmish GameCore lays out from the seed. The
// cores and the ships are their designs' composites, of their sides; the asteroids are their models alone, of none. Its
// welcome names the models, the composites of every design of the catalogue and of every asteroid, the sides' colors, and
// in its payload their names. Until phase 4 nothing moves: the ships hold station, and an entity detonates and is restored
// only on command, as the sector's do.
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
  void Describe(NeuronCore::Snapshot& _snapshot) const override;

private:
  Skirmish() = default;

  struct Entity
  {
    std::uint16_t composite;
    std::uint8_t side;
    NeuronCore::Float3 position; // where the middle of its composite's box stands
    NeuronCore::Quaternion rotation;
    std::optional<NeuronCore::DetonationEvent> detonation;
  };

  SkirmishParameters m_parameters{};
  NeuronCore::WorldSettings m_settings{};
  std::vector<NeuronCore::ManifestEntry> m_manifest;
  std::vector<NeuronCore::CompositeModel> m_composites;
  std::vector<NeuronCore::SideColor> m_sides;
  std::vector<std::uint8_t> m_welcomePayload;
  std::vector<Entity> m_entities; // an entity's id is its index plus one
  std::uint32_t m_detonations = 0;
};

} // namespace GameLogic
