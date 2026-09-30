#include "pch.h"

#include "Orders.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <string>

namespace GameCore
{
namespace
{

// A state's reserved bytes, after its u8 state, and a shot's, after its u8 side.
constexpr std::size_t STATE_RESERVED_BYTES = 3;
constexpr std::size_t SHOT_RESERVED_BYTES = 3;

// The largest count a u16 holds.
constexpr std::size_t MAX_COUNT = 0xFFFF;

void PutU8(std::vector<std::uint8_t>& _bytes, std::uint8_t _value)
{
  _bytes.push_back(_value);
}

void PutU16(std::vector<std::uint8_t>& _bytes, std::uint16_t _value)
{
  _bytes.push_back(static_cast<std::uint8_t>(_value));
  _bytes.push_back(static_cast<std::uint8_t>(_value >> 8u));
}

void PutU32(std::vector<std::uint8_t>& _bytes, std::uint32_t _value)
{
  for (std::uint32_t shift = 0; shift < 32u; shift += 8u)
  {
    _bytes.push_back(static_cast<std::uint8_t>(_value >> shift));
  }
}

void PutF32(std::vector<std::uint8_t>& _bytes, float _value)
{
  PutU32(_bytes, std::bit_cast<std::uint32_t>(_value));
}

void PutVector(std::vector<std::uint8_t>& _bytes, NeuronCore::Float3 _value)
{
  PutF32(_bytes, _value.x);
  PutF32(_bytes, _value.y);
  PutF32(_bytes, _value.z);
}

// Reads little-endian values in turn. Reading past the end sets it failed, and every read after gives zeros.
class OrderReader
{
public:
  explicit OrderReader(std::span<const std::uint8_t> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  [[nodiscard]] std::uint32_t Unsigned(std::size_t _bytes) noexcept
  {
    if (m_failed || _bytes > m_bytes.size() - m_offset)
    {
      m_failed = true;
      return 0;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < _bytes; ++i)
    {
      value |= std::uint32_t{m_bytes[m_offset + i]} << (8u * i);
    }
    m_offset += _bytes;
    return value;
  }

  [[nodiscard]] std::uint8_t U8() noexcept
  {
    return static_cast<std::uint8_t>(Unsigned(1));
  }

  [[nodiscard]] std::uint16_t U16() noexcept
  {
    return static_cast<std::uint16_t>(Unsigned(2));
  }

  [[nodiscard]] std::uint32_t U32() noexcept
  {
    return Unsigned(4);
  }

  [[nodiscard]] float F32() noexcept
  {
    return std::bit_cast<float>(Unsigned(4));
  }

  [[nodiscard]] NeuronCore::Float3 Vector() noexcept
  {
    const float x = F32();
    const float y = F32();
    const float z = F32();
    return {x, y, z};
  }

  // The next _count reserved bytes, ORed together: 0 when every one is.
  [[nodiscard]] std::uint32_t Reserved(std::size_t _count) noexcept
  {
    std::uint32_t reserved = 0;
    for (std::size_t byte = 0; byte < _count; ++byte)
    {
      reserved |= U8();
    }
    return reserved;
  }

  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed;
  }

  [[nodiscard]] bool Exhausted() const noexcept
  {
    return !m_failed && m_offset == m_bytes.size();
  }

private:
  std::span<const std::uint8_t> m_bytes;
  std::size_t m_offset = 0;
  bool m_failed = false;
};

[[nodiscard]] bool IsOrderKind(std::uint8_t _kind) noexcept
{
  return _kind >= static_cast<std::uint8_t>(OrderKind::Move) && _kind <= static_cast<std::uint8_t>(OrderKind::AttackMove);
}

[[nodiscard]] bool IsShipState(std::uint8_t _state) noexcept
{
  return _state >= static_cast<std::uint8_t>(ShipState::Moving) && _state <= static_cast<std::uint8_t>(ShipState::AttackMoving);
}

// Whether an order of _kind goes to a point, and so carries one.
[[nodiscard]] bool GoesToPoint(OrderKind _kind) noexcept
{
  return _kind == OrderKind::Move || _kind == OrderKind::AttackMove;
}

[[nodiscard]] bool IsFinite(NeuronCore::Float3 _value) noexcept
{
  return std::isfinite(_value.x) && std::isfinite(_value.y) && std::isfinite(_value.z);
}

// Whether a state of _state names a target, and only then.
[[nodiscard]] bool IsStateTarget(ShipState _state, std::uint32_t _target) noexcept
{
  return (_state == ShipState::Attacking) == (_target != 0);
}

// Whether _ships names between one and MAX_ORDER_SHIPS ships, none of them 0 and none twice.
[[nodiscard]] bool AreOrderShips(std::span<const std::uint32_t> _ships)
{
  if (_ships.empty() || _ships.size() > MAX_ORDER_SHIPS || std::ranges::find(_ships, 0u) != _ships.end())
  {
    return false;
  }
  std::vector<std::uint32_t> sorted(_ships.begin(), _ships.end());
  std::ranges::sort(sorted);
  return std::ranges::adjacent_find(sorted) == sorted.end();
}

} // namespace

const char* OrderErrorName(OrderError _error) noexcept
{
  switch (_error)
  {
  case OrderError::Truncated:
    return "Truncated";
  case OrderError::UnsupportedVersion:
    return "UnsupportedVersion";
  case OrderError::MalformedOrder:
    return "MalformedOrder";
  }
  return "Unknown";
}

std::vector<std::uint8_t> EncodeOrder(const Order& _order)
{
  const bool move = GoesToPoint(_order.kind);
  const bool attack = _order.kind == OrderKind::Attack;
  if (!IsOrderKind(static_cast<std::uint8_t>(_order.kind)) || !AreOrderShips(_order.ships) ||
      (move && (!std::isfinite(_order.targetX) || !std::isfinite(_order.targetZ))) ||
      (attack && (_order.target == 0 || std::ranges::find(_order.ships, _order.target) != _order.ships.end())))
  {
    throw std::invalid_argument("An order of " + std::to_string(_order.ships.size()) + " ships that its decoder would refuse.");
  }
  std::vector<std::uint8_t> bytes;
  PutU8(bytes, ORDER_VERSION);
  PutU8(bytes, static_cast<std::uint8_t>(_order.kind));
  PutU16(bytes, static_cast<std::uint16_t>(_order.ships.size()));
  for (const std::uint32_t ship : _order.ships)
  {
    PutU32(bytes, ship);
  }
  if (move)
  {
    PutF32(bytes, _order.targetX);
    PutF32(bytes, _order.targetZ);
  }
  if (attack)
  {
    PutU32(bytes, _order.target);
  }
  return bytes;
}

std::expected<Order, OrderError> DecodeOrder(std::span<const std::uint8_t> _bytes)
{
  OrderReader reader(_bytes);
  const std::uint8_t version = reader.U8();
  const std::uint8_t kind = reader.U8();
  const std::uint16_t count = reader.U16();
  if (reader.Failed())
  {
    return std::unexpected(OrderError::Truncated);
  }
  if (version != ORDER_VERSION)
  {
    return std::unexpected(OrderError::UnsupportedVersion);
  }
  if (!IsOrderKind(kind) || count == 0 || count > MAX_ORDER_SHIPS)
  {
    return std::unexpected(OrderError::MalformedOrder);
  }
  Order order{static_cast<OrderKind>(kind), {}, 0.0f, 0.0f};
  order.ships.reserve(count);
  for (std::uint16_t i = 0; i < count; ++i)
  {
    order.ships.push_back(reader.U32());
  }
  if (GoesToPoint(order.kind))
  {
    order.targetX = reader.F32();
    order.targetZ = reader.F32();
  }
  const bool attack = order.kind == OrderKind::Attack;
  if (attack)
  {
    order.target = reader.U32();
  }
  if (reader.Failed())
  {
    return std::unexpected(OrderError::Truncated);
  }
  if (!reader.Exhausted() || !AreOrderShips(order.ships) || !std::isfinite(order.targetX) || !std::isfinite(order.targetZ) ||
      (attack && (order.target == 0 || std::ranges::find(order.ships, order.target) != order.ships.end())))
  {
    return std::unexpected(OrderError::MalformedOrder);
  }
  return order;
}

std::vector<std::uint8_t> EncodeSnapshotPayload(const SnapshotPayload& _payload)
{
  const auto validState = [](const ShipOrderState& _state)
  {
    return _state.ship != 0 && IsShipState(static_cast<std::uint8_t>(_state.state)) && IsStateTarget(_state.state, _state.target) &&
           std::isfinite(_state.destinationX) && std::isfinite(_state.destinationZ);
  };
  const auto validShell = [](const ShellState& _shell)
  { return _shell.side != 0 && IsFinite(_shell.position) && IsFinite(_shell.velocity); };
  const auto validBeam = [](const BeamState& _beam) { return _beam.side != 0 && IsFinite(_beam.from) && IsFinite(_beam.to); };
  if (_payload.orders.size() > MAX_COUNT || _payload.shells.size() > MAX_COUNT || _payload.beams.size() > MAX_COUNT ||
      !std::ranges::all_of(_payload.orders, validState) || !std::ranges::all_of(_payload.shells, validShell) ||
      !std::ranges::all_of(_payload.beams, validBeam))
  {
    throw std::invalid_argument("A snapshot's payload that its decoder would refuse, of " + std::to_string(_payload.orders.size()) +
                                " order states, " + std::to_string(_payload.shells.size()) + " shells and " +
                                std::to_string(_payload.beams.size()) + " beams.");
  }
  std::vector<std::uint8_t> bytes;
  PutU8(bytes, ORDER_VERSION);
  PutU16(bytes, static_cast<std::uint16_t>(_payload.orders.size()));
  for (const ShipOrderState& state : _payload.orders)
  {
    PutU32(bytes, state.ship);
    PutU8(bytes, static_cast<std::uint8_t>(state.state));
    bytes.insert(bytes.end(), STATE_RESERVED_BYTES, std::uint8_t{0});
    PutU32(bytes, state.target);
    PutF32(bytes, state.destinationX);
    PutF32(bytes, state.destinationZ);
  }
  PutU16(bytes, static_cast<std::uint16_t>(_payload.shells.size()));
  for (const ShellState& shell : _payload.shells)
  {
    PutVector(bytes, shell.position);
    PutVector(bytes, shell.velocity);
    PutU8(bytes, shell.side);
    bytes.insert(bytes.end(), SHOT_RESERVED_BYTES, std::uint8_t{0});
  }
  PutU16(bytes, static_cast<std::uint16_t>(_payload.beams.size()));
  for (const BeamState& beam : _payload.beams)
  {
    PutVector(bytes, beam.from);
    PutVector(bytes, beam.to);
    PutU8(bytes, beam.side);
    bytes.insert(bytes.end(), SHOT_RESERVED_BYTES, std::uint8_t{0});
  }
  return bytes;
}

std::expected<SnapshotPayload, OrderError> DecodeSnapshotPayload(std::span<const std::uint8_t> _bytes)
{
  OrderReader reader(_bytes);
  const std::uint8_t version = reader.U8();
  if (reader.Failed())
  {
    return std::unexpected(OrderError::Truncated);
  }
  if (version != ORDER_VERSION)
  {
    return std::unexpected(OrderError::UnsupportedVersion);
  }
  // Each count is read before what it counts, and each record read whole, so that bytes cut short are refused as such
  // before anything is judged.
  SnapshotPayload payload;
  bool malformed = false;
  const std::uint16_t orders = reader.U16();
  for (std::uint16_t i = 0; i < orders && !reader.Failed(); ++i)
  {
    const std::uint32_t ship = reader.U32();
    const std::uint8_t state = reader.U8();
    const std::uint32_t reserved = reader.Reserved(STATE_RESERVED_BYTES);
    const std::uint32_t target = reader.U32();
    const float x = reader.F32();
    const float z = reader.F32();
    malformed = malformed || ship == 0 || !IsShipState(state) || reserved != 0 || !std::isfinite(x) || !std::isfinite(z) ||
                (IsShipState(state) && !IsStateTarget(static_cast<ShipState>(state), target));
    payload.orders.push_back({ship, static_cast<ShipState>(state), x, z, target});
  }
  const std::uint16_t shells = reader.U16();
  for (std::uint16_t i = 0; i < shells && !reader.Failed(); ++i)
  {
    const NeuronCore::Float3 position = reader.Vector();
    const NeuronCore::Float3 velocity = reader.Vector();
    const std::uint8_t side = reader.U8();
    const std::uint32_t reserved = reader.Reserved(SHOT_RESERVED_BYTES);
    malformed = malformed || side == 0 || reserved != 0 || !IsFinite(position) || !IsFinite(velocity);
    payload.shells.push_back({position, velocity, side});
  }
  const std::uint16_t beams = reader.U16();
  for (std::uint16_t i = 0; i < beams && !reader.Failed(); ++i)
  {
    const NeuronCore::Float3 from = reader.Vector();
    const NeuronCore::Float3 to = reader.Vector();
    const std::uint8_t side = reader.U8();
    const std::uint32_t reserved = reader.Reserved(SHOT_RESERVED_BYTES);
    malformed = malformed || side == 0 || reserved != 0 || !IsFinite(from) || !IsFinite(to);
    payload.beams.push_back({from, to, side});
  }
  if (reader.Failed())
  {
    return std::unexpected(OrderError::Truncated);
  }
  if (malformed || !reader.Exhausted())
  {
    return std::unexpected(OrderError::MalformedOrder);
  }
  return payload;
}

} // namespace GameCore
