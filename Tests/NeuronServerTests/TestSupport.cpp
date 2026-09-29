#include "pch.h"

#include "TestSupport.h"

#include "LoopbackTransport.h"

#include <utility>
#include <variant>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{
namespace
{

// Where side 2's ship stands, still.
constexpr NeuronCore::Float3 ENEMY_POSITION{100.0f, 0.0f, 0.0f};

} // namespace

TestWorld::TestWorld(float _startX)
  : m_position{_startX, 0.0f, 0.0f}
{
}

std::uint32_t TestWorld::TickRate() const noexcept
{
  return 30;
}

const NeuronCore::WorldSettings& TestWorld::Settings() const noexcept
{
  return m_settings;
}

std::span<const NeuronCore::ManifestEntry> TestWorld::Manifest() const noexcept
{
  return m_manifest;
}

std::span<const NeuronCore::CompositeModel> TestWorld::Composites() const noexcept
{
  return m_composites;
}

std::span<const NeuronCore::SideColor> TestWorld::Sides() const noexcept
{
  return m_sides;
}

std::span<const std::uint8_t> TestWorld::WelcomePayload() const noexcept
{
  return m_payload;
}

void TestWorld::Advance(std::uint64_t _worldTick)
{
  advanced.push_back(_worldTick);
  if (!m_detonation)
  {
    m_position.x += 1.0f;
  }
}

void TestWorld::Detonate(std::uint32_t _entity, std::uint64_t _worldTick)
{
  if (_entity == SHIP && !m_detonation)
  {
    m_detonation = NeuronCore::DetonationEvent{SHIP, 99, _worldTick, {30.0f, 0.0f, 0.0f}};
  }
}

void TestWorld::Restore(std::uint32_t _entity)
{
  if (_entity == SHIP)
  {
    m_detonation.reset();
  }
}

std::optional<NeuronServer::CommandRefusal> TestWorld::Refuses(const NeuronCore::Command& _command, std::uint8_t _side) const
{
  if (_command.kind == NeuronCore::CommandKind::Game)
  {
    return _command.payload.front() == 0xFF ? std::optional(NeuronServer::CommandRefusal::MalformedCommand) : std::nullopt;
  }
  const bool namesEntity = _command.kind == NeuronCore::CommandKind::Detonate || _command.kind == NeuronCore::CommandKind::Restore;
  const std::uint8_t owner = _command.entity == SHIP ? 1 : (_command.entity == ENEMY ? 2 : 0);
  if (namesEntity && _side != NeuronCore::OBSERVER_SIDE && owner != 0 && owner != _side)
  {
    return NeuronServer::CommandRefusal::OtherSidesEntity;
  }
  return std::nullopt;
}

void TestWorld::ApplyGameCommand(std::span<const std::uint8_t> _payload, std::uint8_t _side)
{
  gameCommands.emplace_back(_payload.begin(), _payload.end());
  gameCommandSides.push_back(_side);
}

void TestWorld::Describe(NeuronCore::Snapshot& _snapshot, std::uint8_t _side) const
{
  const bool everything = _side == NeuronCore::OBSERVER_SIDE || revealed;
  if (everything || _side == 1)
  {
    _snapshot.entities.push_back({SHIP, 0, 1, m_position, {0.0f, 0.0f, 0.0f, 1.0f}, {30.0f, 0.0f, 0.0f}});
    if (m_detonation)
    {
      _snapshot.detonations.push_back(*m_detonation);
    }
  }
  if (everything || _side == 2)
  {
    _snapshot.entities.push_back({ENEMY, 0, 2, ENEMY_POSITION, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}});
  }
}

void Client::Send(const NeuronCore::Message& _message) const
{
  Assert::IsTrue(transport->Send(NeuronCore::EncodeMessage(_message)), L"the client sends");
}

std::vector<NeuronCore::Message> Client::ReceiveAll(std::vector<Bytes>* _bytes) const
{
  std::vector<NeuronCore::Message> messages;
  while (std::optional<Bytes> bytes = transport->Receive())
  {
    const auto message = NeuronCore::DecodeMessage(*bytes, {1, 2});
    Assert::IsTrue(message.has_value(), L"the server's message decodes");
    messages.push_back(message.value_or(NeuronCore::Message{}));
    if (_bytes != nullptr)
    {
      _bytes->push_back(std::move(*bytes));
    }
  }
  return messages;
}

Client Join(NeuronServer::ServerHost& _host, std::uint8_t _side, std::uint32_t _protocolVersion)
{
  NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
  _host.AddSession(std::move(pair.server), _side);
  Client client{std::move(pair.client)};
  client.Send(NeuronCore::Hello{_protocolVersion});
  return client;
}

std::vector<NeuronCore::Snapshot> SnapshotsOf(const std::vector<NeuronCore::Message>& _messages)
{
  std::vector<NeuronCore::Snapshot> snapshots;
  for (const NeuronCore::Message& message : _messages)
  {
    if (const auto* snapshot = std::get_if<NeuronCore::Snapshot>(&message))
    {
      snapshots.push_back(*snapshot);
    }
  }
  return snapshots;
}

} // namespace NeuronServerTests
