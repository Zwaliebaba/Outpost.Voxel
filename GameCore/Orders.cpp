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

// A state's reserved bytes, after its u8 state.
constexpr std::size_t STATE_RESERVED_BYTES = 3;

// The largest count a u16 holds.
constexpr std::size_t MAX_STATES = 0xFFFF;

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
  return _kind == static_cast<std::uint8_t>(OrderKind::Move) || _kind == static_cast<std::uint8_t>(OrderKind::Stop) ||
         _kind == static_cast<std::uint8_t>(OrderKind::Hold);
}

[[nodiscard]] bool IsShipState(std::uint8_t _state) noexcept
{
  return _state == static_cast<std::uint8_t>(ShipState::Moving) || _state == static_cast<std::uint8_t>(ShipState::Holding);
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
  const bool move = _order.kind == OrderKind::Move;
  if (!IsOrderKind(static_cast<std::uint8_t>(_order.kind)) || !AreOrderShips(_order.ships) ||
      (move && (!std::isfinite(_order.targetX) || !std::isfinite(_order.targetZ))))
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
  if (order.kind == OrderKind::Move)
  {
    order.targetX = reader.F32();
    order.targetZ = reader.F32();
  }
  if (reader.Failed())
  {
    return std::unexpected(OrderError::Truncated);
  }
  if (!reader.Exhausted() || !AreOrderShips(order.ships) || !std::isfinite(order.targetX) || !std::isfinite(order.targetZ))
  {
    return std::unexpected(OrderError::MalformedOrder);
  }
  return order;
}

std::vector<std::uint8_t> EncodeOrderStates(std::span<const ShipOrderState> _states)
{
  const auto valid = [](const ShipOrderState& _state)
  {
    return _state.ship != 0 && IsShipState(static_cast<std::uint8_t>(_state.state)) && std::isfinite(_state.destinationX) &&
           std::isfinite(_state.destinationZ);
  };
  if (_states.size() > MAX_STATES || !std::ranges::all_of(_states, valid))
  {
    throw std::invalid_argument("Order states that their decoder would refuse, " + std::to_string(_states.size()) + " of them.");
  }
  std::vector<std::uint8_t> bytes;
  PutU8(bytes, ORDER_VERSION);
  PutU16(bytes, static_cast<std::uint16_t>(_states.size()));
  for (const ShipOrderState& state : _states)
  {
    PutU32(bytes, state.ship);
    PutU8(bytes, static_cast<std::uint8_t>(state.state));
    bytes.insert(bytes.end(), STATE_RESERVED_BYTES, std::uint8_t{0});
    PutF32(bytes, state.destinationX);
    PutF32(bytes, state.destinationZ);
  }
  return bytes;
}

std::expected<std::vector<ShipOrderState>, OrderError> DecodeOrderStates(std::span<const std::uint8_t> _bytes)
{
  OrderReader reader(_bytes);
  const std::uint8_t version = reader.U8();
  const std::uint16_t count = reader.U16();
  if (reader.Failed())
  {
    return std::unexpected(OrderError::Truncated);
  }
  if (version != ORDER_VERSION)
  {
    return std::unexpected(OrderError::UnsupportedVersion);
  }
  std::vector<ShipOrderState> states;
  bool malformed = false;
  for (std::uint16_t i = 0; i < count && !reader.Failed(); ++i)
  {
    const std::uint32_t ship = reader.U32();
    const std::uint8_t state = reader.U8();
    std::uint32_t reserved = 0;
    for (std::size_t byte = 0; byte < STATE_RESERVED_BYTES; ++byte)
    {
      reserved |= reader.U8();
    }
    const float x = reader.F32();
    const float z = reader.F32();
    malformed = malformed || ship == 0 || !IsShipState(state) || reserved != 0 || !std::isfinite(x) || !std::isfinite(z);
    states.push_back({ship, static_cast<ShipState>(state), x, z});
  }
  if (reader.Failed())
  {
    return std::unexpected(OrderError::Truncated);
  }
  if (malformed || !reader.Exhausted())
  {
    return std::unexpected(OrderError::MalformedOrder);
  }
  return states;
}

} // namespace GameCore
