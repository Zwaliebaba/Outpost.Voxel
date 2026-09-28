#pragma once

#include "Message.h"

#include <cstdint>
#include <span>

namespace NeuronServer
{

// What a ServerHost simulates (Design/SpaceScene.md §6.1). The host owns the clock, the sessions and the messages; the
// world only advances a tick at a time and describes itself. The game's world is GameLogic's Sector, and the
// server's tests bring one of their own.
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

  // The models the world places, which an entity names by its index here.
  [[nodiscard]] virtual std::span<const NeuronCore::ManifestEntry> Manifest() const noexcept = 0;

  // Advances the world by one tick, to world tick _worldTick. The host does not call it while the world is paused.
  virtual void Advance(std::uint64_t _worldTick) = 0;

  // Detonates entity _entity at world tick _worldTick. An entity the world does not have, or one already detonated, is
  // left as it is: a command can race a removal.
  virtual void Detonate(std::uint32_t _entity, std::uint64_t _worldTick) = 0;

  // Restores a detonated entity whole; anything else is left as it is.
  virtual void Restore(std::uint32_t _entity) = 0;

  // Fills _snapshot's entities, in the order of their ids, and its detonations: the world as it is now.
  virtual void Describe(NeuronCore::Snapshot& _snapshot) const = 0;
};

} // namespace NeuronServer
