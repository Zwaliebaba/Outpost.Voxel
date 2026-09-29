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

// The messages that cross between server and client, and their encoding (Design/Archive/SpaceScene.md §6.2, Design/ADR/ADR-015,
// Design/ADR/ADR-029). Client and server share these bytes and nothing else: every message is little-endian, and starts
// with a header of its type, its layout's version and its whole size in bytes.

// The protocol a Hello asks for and a Welcome answers with. A change of meaning bumps it: version 2 places entities as
// composite models of sides, carries the game's own payload (ADR-029), and sends each session what its side sees
// (G21, G30, Design/ADR/ADR-032).
inline constexpr std::uint32_t PROTOCOL_VERSION = 2;

// The version of every message's layout, in its header. A change of layout bumps it: version 3's welcome tells the
// session the side it plays (ADR-032).
inline constexpr std::uint16_t MESSAGE_LAYOUT_VERSION = 3;

inline constexpr std::size_t MESSAGE_HEADER_BYTES = 8;
inline constexpr std::size_t ENTITY_RECORD_BYTES = 48;
inline constexpr std::size_t DETONATION_RECORD_BYTES = 28;
inline constexpr std::size_t COMPONENT_RECORD_BYTES = 32;
inline constexpr std::size_t SIDE_RECORD_BYTES = 4;

// A model's name is its file's stem, letters and digits, at most this many (§6.2).
inline constexpr std::size_t MAX_MODEL_NAME_CHARS = 255;

// An entity's side is a byte, and side 0 is none: a welcome names at most this many sides (ADR-029).
inline constexpr std::size_t MAX_SIDES = 255;

// The side of a session that plays none (ADR-032): an observer, which receives the whole world.
inline constexpr std::uint8_t OBSERVER_SIDE = 0;

// How far a component may be moved within its composite, on any axis: the .vox reader's bound on a translation, which keeps
// every voxel center exact in single precision (NeuronCore/VoxModel.h).
inline constexpr std::int32_t MAX_COMPONENT_TRANSLATION = 1 << 20;

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

// One model of a composite, placed in the composite's space: turned by one of the cube's 24 rotations and moved by whole
// voxels, so that a composite that stands aligned draws aligned (ADR-029). A point p of the model lies at
// translation + rotation(p) in the composite.
struct CompositeComponent
{
  std::uint16_t model; // its index in the manifest
  Int3 translation;    // whole voxels, at most MAX_COMPONENT_TRANSLATION on each axis
  Quaternion rotation; // one of the cube's 24, unit, w >= 0
};

// What an entity is drawn as: models placed together, at least one. The engine gives a component no meaning; the game's
// composite of a design is its hull and a module at each mount.
struct CompositeModel
{
  std::vector<CompositeComponent> components;
};

// A side's color, in sRGB, which every palette of the side's entities shows in its SIDE_PALETTE_ENTRY (G24, ADR-029).
struct SideColor
{
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};

// Server to client, in answer to a Hello: the protocol, the clock, the world's settings, the models it places, the
// composites an entity may be, the sides, the side the session plays, and the game's own payload, which the engine
// carries without reading (ADR-029, ADR-032). An entity names its composite by its index here, and its side by its
// number: 0 for none, and n for sides[n - 1].
struct Welcome
{
  std::uint32_t protocolVersion;
  std::uint32_t tickRate; // ticks a second
  std::uint64_t tick;     // the tick of the last snapshot sent, which the next one follows
  WorldSettings settings;
  std::vector<ManifestEntry> manifest;
  std::vector<CompositeModel> composites;
  std::vector<SideColor> sides;
  std::uint8_t sessionSide; // the side this session plays, n for sides[n - 1], or OBSERVER_SIDE
  std::vector<std::uint8_t> payload;
};

// One entity in a snapshot: a fixed 48-byte record on the wire, its last flag byte reserved and zero (§6.2, ADR-029). The
// position is where the center of its composite's occupied box lies; the rotation turns the composite into the world.
struct EntityState
{
  std::uint32_t id; // never 0, and never reused within a session
  std::uint16_t composite;
  std::uint8_t side; // 0 for none
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

// Server to client, once a tick: the world as the session's side sees it, whole, or all of it for an observer (ADR-032).
// The tick is the clock's and never stops; the world tick counts the ticks the world has advanced, and stands still while
// it is paused, so that debris freezes with it (§6.3, ADR-015). The payload is the game's, which the engine carries
// without reading (ADR-029).
struct Snapshot
{
  std::uint64_t tick;
  std::uint64_t worldTick;
  bool paused;
  std::vector<EntityState> entities;
  std::vector<DetonationEvent> detonations;
  std::vector<std::uint8_t> payload;
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
  BadModelIndex,      // a component naming a model the welcome's manifest does not have
  UnknownEntity,      // a detonation of an entity the snapshot does not hold
  NotCubeRotation,    // a component turned by other than one of the cube's 24 rotations
  BadCompositeIndex,  // an entity naming a composite the receiver's welcome does not have
  BadSide             // an entity, or a welcome's session, naming a side the welcome does not have
};

[[nodiscard]] const char* ProtocolErrorName(ProtocolError _error) noexcept;

// What a receiver's welcome says an entity may name: how many composites and how many sides. A snapshot's entities are
// held to it; a receiver without a welcome yet holds snapshots to none.
struct WelcomeCounts
{
  std::size_t composites;
  std::size_t sides;
};

// The bytes of _message. Throws std::invalid_argument for a model name longer than MAX_MODEL_NAME_CHARS; everything
// else it encodes as it is, and the decoder is what refuses.
[[nodiscard]] std::vector<std::uint8_t> EncodeMessage(const Message& _message);

// The message _bytes hold, which must be one whole message and nothing more. A snapshot's entities must name composites
// and sides within _counts, the receiver's welcome's.
[[nodiscard]] std::expected<Message, ProtocolError> DecodeMessage(std::span<const std::uint8_t> _bytes, WelcomeCounts _counts);

} // namespace NeuronCore
