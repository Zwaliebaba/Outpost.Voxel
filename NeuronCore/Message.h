#pragma once

#include "Float3.h"
#include "Quaternion.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace NeuronCore
{

// The messages that cross between server and client, and their encoding (Design/SpaceScene.md §6.2, Design/ADR/ADR-015).
// Client and server share these bytes and nothing else: every message is little-endian, and starts with a header of
// its type, its layout's version and its whole size in bytes.

// The protocol a Hello asks for and a Welcome answers with. A change of meaning bumps it.
inline constexpr std::uint32_t PROTOCOL_VERSION = 1;

// The version of every message's layout, in its header. A change of layout bumps it.
inline constexpr std::uint16_t MESSAGE_LAYOUT_VERSION = 1;

inline constexpr std::size_t MESSAGE_HEADER_BYTES = 8;
inline constexpr std::size_t ENTITY_RECORD_BYTES = 48;
inline constexpr std::size_t DETONATION_RECORD_BYTES = 28;

// A model's name is its file's stem, letters and digits, at most this many (§6.2).
inline constexpr std::size_t MAX_MODEL_NAME_CHARS = 255;

// Client to server: the protocol the client speaks.
struct Hello
{
  std::uint32_t protocolVersion;
};

// The world's settings, which the lighting and the sky take from the welcome (§6.2, §11, §12.1).
struct WorldSettings
{
  Float3 toSun;                  // unit length
  Float3 sunRadiance;            // linear, non-negative
  float sunAngularRadiusRadians; // above 0 and below a quarter turn
  Float3 ambientUpper;           // the hemisphere's two colors, linear, non-negative
  Float3 ambientLower;
  std::uint32_t skySeed;    // the star catalog's
  Quaternion galacticPlane; // the galaxy's frame in the world
};

// A model the world places, by name, with the 64-bit FNV-1a hash of its file (Fnv1aHash64).
struct ManifestEntry
{
  std::string name;
  std::uint64_t hash;
};

// Server to client, in answer to a Hello: the protocol, the clock, the world's settings and the models it places. An
// entity names its model by its index in the manifest.
struct Welcome
{
  std::uint32_t protocolVersion;
  std::uint32_t tickRate; // ticks a second
  std::uint64_t tick;     // the tick of the last snapshot sent, which the next one follows
  WorldSettings settings;
  std::vector<ManifestEntry> manifest;
};

// One entity in a snapshot: a fixed 48-byte record on the wire, its two flag bytes reserved and zero (§6.2). The
// position is where the center of its model's occupied box lies; the rotation turns the model into the world.
struct EntityState
{
  std::uint32_t id; // never 0, and never reused within a session
  std::uint16_t modelIndex;
  Float3 position;
  Quaternion rotation; // unit, w >= 0
  Float3 velocity;     // units a second
};

// A detonation whose debris still lasts (§5.5): the entity, the seed of its debris, the world tick it happened at, and
// the entity's velocity at that tick. The debris is a pure function of these and the model, so every client computes
// the same.
struct DetonationEvent
{
  std::uint32_t entity;
  std::uint32_t seed;
  std::uint64_t worldTick;
  Float3 velocity;
};

// Server to client, once a tick: the whole state of the world. The tick is the clock's and never stops; the world tick
// counts the ticks the world has advanced, and stands still while it is paused, so that debris freezes with it (§6.3,
// ADR-015).
struct Snapshot
{
  std::uint64_t tick;
  std::uint64_t worldTick;
  bool paused;
  std::vector<EntityState> entities;
  std::vector<DetonationEvent> detonations;
};

enum class CommandKind : std::uint32_t
{
  Pause = 1,
  Resume = 2,
  Detonate = 3,
  Restore = 4
};

// Client to server: pause or resume the world, or, for testing until fighting decides what destroys what, detonate or
// restore an entity (§5.5). The entity is 0 for a pause or a resume.
struct Command
{
  CommandKind kind;
  std::uint32_t entity;
};

using Message = std::variant<Hello, Welcome, Snapshot, Command>;

// Why a message was refused (§6.2). The decoder refuses by name, as the readers do, rather than guessing.
enum class ProtocolError : std::uint8_t
{
  Truncated,          // fewer bytes than a header, or than the header's size
  UnknownMessage,     // a type this protocol does not have
  UnsupportedVersion, // a layout version this decoder does not read
  MalformedMessage,   // a size or count that disagrees with the content, a nonzero reserved field, a value out of range
  BadName,            // a model name that is empty, too long, or not letters and digits
  NotFinite,          // a NaN or an infinity
  NotUnitRotation,    // a rotation further than UNIT_ROTATION_TOLERANCE from unit length, or with w < 0
  DuplicateEntity,    // two records of one entity in a snapshot
  BadModelIndex,      // a model index the receiver's manifest does not have
  UnknownEntity       // a detonation of an entity the snapshot does not hold
};

[[nodiscard]] const char* ProtocolErrorName(ProtocolError _error) noexcept;

// The bytes of _message. Throws std::invalid_argument for a model name longer than MAX_MODEL_NAME_CHARS; everything
// else it encodes as it is, and the decoder is what refuses.
[[nodiscard]] std::vector<std::uint8_t> EncodeMessage(const Message& _message);

// The message _bytes hold, which must be one whole message and nothing more. _modelCount is how many models the
// receiver's manifest names: a snapshot's model indices must fall below it.
[[nodiscard]] std::expected<Message, ProtocolError> DecodeMessage(std::span<const std::uint8_t> _bytes, std::size_t _modelCount);

} // namespace NeuronCore
