#pragma once

#include "Float3.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace GameCore
{

// The skirmish's orders (Design/ADR/ADR-033, Design/ADR/ADR-035): the game's own command, which the engine carries in a
// command's payload without reading it. Little-endian: a u8 version, 2; a u8 kind; a u16 count of ships, then that many
// u32 ids, each once; for a move or an attack-move, the target on the plane as its f32 x and z; and for an attack, the
// u32 id of the entity attacked, which is none of the ships.
inline constexpr std::uint8_t ORDER_VERSION = 2;

// The most ships one order names.
inline constexpr std::size_t MAX_ORDER_SHIPS = 256;

enum class OrderKind : std::uint8_t
{
  Move = 1,      // to a point on the plane, the ships keeping their offsets from one another
  Stop = 2,      // brake to a halt and forget the move
  Hold = 3,      // brake to a halt and hold there
  Attack = 4,    // close to range of an entity of another side and fight it (G47)
  AttackMove = 5 // move, and fight what comes within range on the way
};

struct Order
{
  OrderKind kind;
  std::vector<std::uint32_t> ships;
  float targetX; // a move's or an attack-move's point on the plane; 0 otherwise
  float targetZ;
  std::uint32_t target = 0; // an attack's entity; 0 otherwise
};

// Why an order, or a snapshot's payload, did not decode.
enum class OrderError : std::uint8_t
{
  Truncated,          // bytes that end inside a field
  UnsupportedVersion, // another version
  // A kind or a state the game does not have, no ship or too many, an id of 0 or twice, an attack on one of its own
  // ships, a value that is not finite, a shot of no side, a reserved byte that is not 0, or bytes to spare.
  MalformedOrder
};

[[nodiscard]] const char* OrderErrorName(OrderError _error) noexcept;

// The bytes of _order. Throws std::invalid_argument for an order DecodeOrder would refuse.
[[nodiscard]] std::vector<std::uint8_t> EncodeOrder(const Order& _order);

[[nodiscard]] std::expected<Order, OrderError> DecodeOrder(std::span<const std::uint8_t> _bytes);

// What a ship is doing, as its side learns it: a ship doing none of these is idle.
enum class ShipState : std::uint8_t
{
  Moving = 1,
  Holding = 2,
  Attacking = 3,
  AttackMoving = 4
};

struct ShipOrderState
{
  std::uint32_t ship;
  ShipState state;
  float destinationX; // a move's or an attack-move's; 0 otherwise
  float destinationZ;
  std::uint32_t target = 0; // an attack's; 0 otherwise
};

// A shell as a side sees it (G54): where it is at the snapshot's tick and how it moves, never where it was fired, and the
// side it flies for.
struct ShellState
{
  NeuronCore::Float3 position;
  NeuronCore::Float3 velocity; // units a second
  std::uint8_t side;
};

// A beam that fired this tick, as a side sees it: the part of it within the side's sensors, from its end nearer the
// weapon to its end nearer where it stopped.
struct BeamState
{
  NeuronCore::Float3 from;
  NeuronCore::Float3 to;
  std::uint8_t side;
};

// What a side learns in each of its snapshots' payloads (ADR-033, ADR-035): its ships' orders, and the shots it sees.
// Little-endian: a u8 version, 2; a u16 count of order states, each a ship's u32 id, its u8 state, three reserved bytes of
// 0, an attack's u32 target, and a move's or an attack-move's destination as f32 x and z; a u16 count of shells, each its
// position and its velocity as three f32 each, its u8 side and three reserved bytes; and a u16 count of beams, each its
// two ends as three f32 each, its u8 side and three reserved bytes.
struct SnapshotPayload
{
  std::vector<ShipOrderState> orders;
  std::vector<ShellState> shells;
  std::vector<BeamState> beams;
};

// The bytes of _payload. Throws std::invalid_argument for more of anything than a u16 counts, or for what
// DecodeSnapshotPayload would refuse.
[[nodiscard]] std::vector<std::uint8_t> EncodeSnapshotPayload(const SnapshotPayload& _payload);

[[nodiscard]] std::expected<SnapshotPayload, OrderError> DecodeSnapshotPayload(std::span<const std::uint8_t> _bytes);

} // namespace GameCore
