#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace GameCore
{

// The skirmish's orders (Design/ADR/ADR-033): the game's own command, which the engine carries in a command's payload
// without reading it. Little-endian: a u8 version, 1; a u8 kind; a u16 count of ships, then that many u32 ids, each
// once; and for a move, the target on the plane as its f32 x and z.
inline constexpr std::uint8_t ORDER_VERSION = 1;

// The most ships one order names.
inline constexpr std::size_t MAX_ORDER_SHIPS = 256;

enum class OrderKind : std::uint8_t
{
  Move = 1, // to a point on the plane, the ships keeping their offsets from one another
  Stop = 2, // brake to a halt and forget the move
  Hold = 3  // brake to a halt and hold there
};

struct Order
{
  OrderKind kind;
  std::vector<std::uint32_t> ships;
  float targetX; // a move's target on the plane; 0 for a stop or a hold
  float targetZ;
};

// Why an order, or a snapshot's order states, did not decode.
enum class OrderError : std::uint8_t
{
  Truncated,          // bytes that end inside a field
  UnsupportedVersion, // another version
  // A kind or a state the game does not have, no ship or too many, an id of 0 or twice, a target that is not finite, a
  // reserved byte that is not 0, or bytes to spare.
  MalformedOrder
};

[[nodiscard]] const char* OrderErrorName(OrderError _error) noexcept;

// The bytes of _order. Throws std::invalid_argument for an order DecodeOrder would refuse.
[[nodiscard]] std::vector<std::uint8_t> EncodeOrder(const Order& _order);

[[nodiscard]] std::expected<Order, OrderError> DecodeOrder(std::span<const std::uint8_t> _bytes);

// What a side learns of its ships' orders, in each of its snapshots' payloads (ADR-033): a u8 version, 1; a u16 count;
// and for each ship that moves or holds, its u32 id, its u8 state, three reserved bytes of 0, and a move's destination
// as f32 x and z, 0 for a hold. A ship that does neither is left out.
enum class ShipState : std::uint8_t
{
  Moving = 1,
  Holding = 2
};

struct ShipOrderState
{
  std::uint32_t ship;
  ShipState state;
  float destinationX; // a move's; 0 while holding
  float destinationZ;
};

// The bytes of _states. Throws std::invalid_argument for more states than a u16 counts, or states DecodeOrderStates
// would refuse.
[[nodiscard]] std::vector<std::uint8_t> EncodeOrderStates(std::span<const ShipOrderState> _states);

[[nodiscard]] std::expected<std::vector<ShipOrderState>, OrderError> DecodeOrderStates(std::span<const std::uint8_t> _bytes);

} // namespace GameCore
