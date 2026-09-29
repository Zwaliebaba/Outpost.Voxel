#pragma once

#include "ServerHost.h"
#include "World.h"

#include "Message.h"
#include "Transport.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace NeuronServerTests
{

using Bytes = std::vector<std::uint8_t>;

// The test world's two ships, side 1's and side 2's, and an id it does not have.
inline constexpr std::uint32_t SHIP = 7;
inline constexpr std::uint32_t NO_SUCH_ENTITY = 8;
inline constexpr std::uint32_t ENEMY = 9;

// A world of two sides, each with one ship: side 1's moves one unit along x each tick, and side 2's stands at x = 100. Each
// side sees its own ship, and the other's only once the test reveals it; the observer sees both (Design/ADR/ADR-032). A
// command that names the other side's ship is refused, and so is a game command whose first byte is 0xFF
// (Design/ADR/ADR-033). The world remembers what the host asked of it, and its welcome carries a payload the host must
// pass on unread.
class TestWorld final : public NeuronServer::World
{
public:
  explicit TestWorld(float _startX = 0.0f);

  [[nodiscard]] std::uint32_t TickRate() const noexcept override;
  [[nodiscard]] const NeuronCore::WorldSettings& Settings() const noexcept override;
  [[nodiscard]] std::span<const NeuronCore::ManifestEntry> Manifest() const noexcept override;
  [[nodiscard]] std::span<const NeuronCore::CompositeModel> Composites() const noexcept override;
  [[nodiscard]] std::span<const NeuronCore::SideColor> Sides() const noexcept override;
  [[nodiscard]] std::span<const std::uint8_t> WelcomePayload() const noexcept override;
  void Advance(std::uint64_t _worldTick) override;
  void Detonate(std::uint32_t _entity, std::uint64_t _worldTick) override;
  void Restore(std::uint32_t _entity) override;
  [[nodiscard]] std::optional<NeuronServer::CommandRefusal> Refuses(const NeuronCore::Command& _command, std::uint8_t _side) const override;
  void ApplyGameCommand(std::span<const std::uint8_t> _payload, std::uint8_t _side) override;
  void Describe(NeuronCore::Snapshot& _snapshot, std::uint8_t _side) const override;

  // The world ticks Advance was called with, in order.
  std::vector<std::uint64_t> advanced;

  // The game's commands the world was handed, and the sides they came from, in order.
  std::vector<Bytes> gameCommands;
  std::vector<std::uint8_t> gameCommandSides;

  // Whether each side sees the other's ship.
  bool revealed = false;

private:
  NeuronCore::WorldSettings m_settings{
    {0.0f, 1.0f, 0.0f}, {0.7f, 0.7f, 0.7f}, 0.0047f, {0.05f, 0.05f, 0.05f}, {0.05f, 0.05f, 0.05f}, 1, {0.0f, 0.0f, 0.0f, 1.0f}};
  std::vector<NeuronCore::ManifestEntry> m_manifest{{"Frigate", 0x1234u}};
  std::vector<NeuronCore::CompositeModel> m_composites{{{{0, {0, 0, 0}, {0.0f, 0.0f, 0.0f, 1.0f}}}}};
  std::vector<NeuronCore::SideColor> m_sides{{200, 40, 40}, {40, 40, 200}};
  std::vector<std::uint8_t> m_payload{1, 2, 3};
  NeuronCore::Float3 m_position;
  std::optional<NeuronCore::DetonationEvent> m_detonation;
};

// The client's end of a loopback: what it has received, decoded against the test world's composite and two sides.
struct Client
{
  std::unique_ptr<NeuronCore::Transport> transport;

  void Send(const NeuronCore::Message& _message) const;

  // Every message waiting, decoded, and the bytes they came in.
  [[nodiscard]] std::vector<NeuronCore::Message> ReceiveAll(std::vector<Bytes>* _bytes = nullptr) const;
};

// A client of _host that plays side _side and has said Hello, in _protocolVersion.
[[nodiscard]] Client Join(NeuronServer::ServerHost& _host, std::uint8_t _side,
                          std::uint32_t _protocolVersion = NeuronCore::PROTOCOL_VERSION);

[[nodiscard]] std::vector<NeuronCore::Snapshot> SnapshotsOf(const std::vector<NeuronCore::Message>& _messages);

// Whether _call throws an Exception.
template <typename Exception, typename Fn> [[nodiscard]] bool Throws(const Fn& _call)
{
  try
  {
    _call();
  }
  catch (const Exception&)
  {
    return true;
  }
  return false;
}

} // namespace NeuronServerTests
