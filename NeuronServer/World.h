#pragma once

#include "Message.h"

#include <cstdint>
#include <optional>
#include <span>

namespace NeuronServer
{

// Why a world refuses a command from a session's side (G34, Design/ADR/ADR-032). The host counts the refusal, and the
// session goes on.
enum class CommandRefusal : std::uint8_t
{
  OtherSidesEntity // a command that names an entity of a side other than the session's
};

// What a ServerHost simulates (Design/Archive/SpaceScene.md §6.1). The host owns the clock, the sessions and the messages; the
// world only advances a tick at a time, describes itself as each side sees it, and judges each side's commands
// (Design/ADR/ADR-032). The game's worlds are GameLogic's Sector and Skirmish, and the server's tests bring one of their own.
class World
{
public:
  World() = default;
  virtual ~World() = default;
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) = delete;
  World& operator=(World&&) = delete;

  // Ticks a second: the rate the host runs at, which its welcome announces.
  [[nodiscard]] virtual std::uint32_t TickRate() const noexcept = 0;

  [[nodiscard]] virtual const NeuronCore::WorldSettings& Settings() const noexcept = 0;

  // The models the world places, which its composites name by their index here.
  [[nodiscard]] virtual std::span<const NeuronCore::ManifestEntry> Manifest() const noexcept = 0;

  // What an entity may be drawn as, which it names by its index here (Design/ADR/ADR-029).
  [[nodiscard]] virtual std::span<const NeuronCore::CompositeModel> Composites() const noexcept = 0;

  // The sides' colors: an entity of side n is of Sides()[n - 1], and one of side 0 of none. A world of no sides has none.
  [[nodiscard]] virtual std::span<const NeuronCore::SideColor> Sides() const noexcept
  {
    return {};
  }

  // The game's own payload for the welcome, which the host sends without reading (ADR-029). A world with nothing to say
  // sends none.
  [[nodiscard]] virtual std::span<const std::uint8_t> WelcomePayload() const noexcept
  {
    return {};
  }

  // Advances the world by one tick, to world tick _worldTick. The host does not call it while the world is paused.
  virtual void Advance(std::uint64_t _worldTick) = 0;

  // Detonates entity _entity at world tick _worldTick. An entity the world does not have, or one already detonated, is
  // left as it is: a command can race a removal.
  virtual void Detonate(std::uint32_t _entity, std::uint64_t _worldTick) = 0;

  // Restores a detonated entity whole; anything else is left as it is.
  virtual void Restore(std::uint32_t _entity) = 0;

  // Why the world refuses a command from a session of the given side, or nothing when it applies it (G34). An observer's
  // commands are judged as no side's. A world that gives its sides no rules refuses nothing.
  [[nodiscard]] virtual std::optional<CommandRefusal> Refuses(const NeuronCore::Command& /*_command*/, std::uint8_t /*_side*/) const
  {
    return std::nullopt;
  }

  // Fills _snapshot's entities, in the order of their ids, and its detonations: the world as it is now, as side _side sees
  // it, or all of it for NeuronCore::OBSERVER_SIDE (G21, G30). A detonation goes with its entity.
  virtual void Describe(NeuronCore::Snapshot& _snapshot, std::uint8_t _side) const = 0;
};

} // namespace NeuronServer
