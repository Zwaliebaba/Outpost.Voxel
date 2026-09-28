#include "pch.h"

#include "Message.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace NeuronCore
{
namespace
{

// The type in a message's header (§6.2).
enum class MessageType : std::uint16_t
{
  Hello = 1,
  Welcome = 2,
  Snapshot = 3,
  Command = 4
};

// A snapshot's flags: bit 0 says the world is paused, and the others are reserved and zero.
constexpr std::uint32_t PAUSED_FLAG = 1u;

// The least a manifest entry can take: a name's length byte, one letter and the hash.
constexpr std::size_t MIN_MANIFEST_ENTRY_BYTES = 1 + 1 + 8;

// Where a header keeps the message's size.
constexpr std::size_t HEADER_SIZE_OFFSET = 4;

using Decoded = std::expected<Message, ProtocolError>;

// Appends little-endian values, whatever the machine's own order.
class ByteWriter
{
public:
  void U8(std::uint8_t _value)
  {
    m_bytes.push_back(_value);
  }

  void U16(std::uint16_t _value)
  {
    Unsigned(_value, 2);
  }

  void U32(std::uint32_t _value)
  {
    Unsigned(_value, 4);
  }

  void U64(std::uint64_t _value)
  {
    Unsigned(_value, 8);
  }

  void F32(float _value)
  {
    U32(std::bit_cast<std::uint32_t>(_value));
  }

  void Vector(Float3 _value)
  {
    F32(_value.x);
    F32(_value.y);
    F32(_value.z);
  }

  void Rotation(Quaternion _value)
  {
    F32(_value.x);
    F32(_value.y);
    F32(_value.z);
    F32(_value.w);
  }

  void Text(std::string_view _text)
  {
    m_bytes.insert(m_bytes.end(), _text.begin(), _text.end());
  }

  // The bytes written, with the header's size filled in.
  [[nodiscard]] std::vector<std::uint8_t> Finish() &&
  {
    const auto size = static_cast<std::uint32_t>(m_bytes.size());
    for (std::size_t i = 0; i < 4; ++i)
    {
      m_bytes[HEADER_SIZE_OFFSET + i] = static_cast<std::uint8_t>(size >> (8 * i));
    }
    return std::move(m_bytes);
  }

private:
  void Unsigned(std::uint64_t _value, std::size_t _count)
  {
    for (std::size_t i = 0; i < _count; ++i)
    {
      m_bytes.push_back(static_cast<std::uint8_t>(_value >> (8 * i)));
    }
  }

  std::vector<std::uint8_t> m_bytes;
};

// Reads little-endian values from a span. The first read past the end sets Failed(), and every later read returns
// zero, so that a decoder can read a whole record and check once, as the .vox reader does.
class ByteReader
{
public:
  explicit ByteReader(std::span<const std::uint8_t> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed;
  }

  [[nodiscard]] std::size_t Remaining() const noexcept
  {
    return m_bytes.size() - m_offset;
  }

  // Everything was read, and nothing more than there was.
  [[nodiscard]] bool Exhausted() const noexcept
  {
    return !m_failed && Remaining() == 0;
  }

  [[nodiscard]] std::span<const std::uint8_t> Bytes(std::size_t _count) noexcept
  {
    if (m_failed || _count > Remaining())
    {
      m_failed = true;
      return {};
    }
    const std::span<const std::uint8_t> bytes = m_bytes.subspan(m_offset, _count);
    m_offset += _count;
    return bytes;
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
    return static_cast<std::uint32_t>(Unsigned(4));
  }

  [[nodiscard]] std::uint64_t U64() noexcept
  {
    return Unsigned(8);
  }

  [[nodiscard]] float F32() noexcept
  {
    return std::bit_cast<float>(U32());
  }

  [[nodiscard]] Float3 Vector() noexcept
  {
    const float x = F32();
    const float y = F32();
    const float z = F32();
    return {x, y, z};
  }

  [[nodiscard]] Quaternion Rotation() noexcept
  {
    const float x = F32();
    const float y = F32();
    const float z = F32();
    const float w = F32();
    return {x, y, z, w};
  }

private:
  [[nodiscard]] std::uint64_t Unsigned(std::size_t _count) noexcept
  {
    std::uint64_t value = 0;
    const std::span<const std::uint8_t> bytes = Bytes(_count);
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
      value |= std::uint64_t{bytes[i]} << (8 * i);
    }
    return value;
  }

  std::span<const std::uint8_t> m_bytes;
  std::size_t m_offset = 0;
  bool m_failed = false;
};

[[nodiscard]] bool IsFinite(Float3 _value) noexcept
{
  return std::isfinite(_value.x) && std::isfinite(_value.y) && std::isfinite(_value.z);
}

[[nodiscard]] bool IsFinite(Quaternion _value) noexcept
{
  return std::isfinite(_value.x) && std::isfinite(_value.y) && std::isfinite(_value.z) && std::isfinite(_value.w);
}

[[nodiscard]] bool IsNonNegative(Float3 _value) noexcept
{
  return _value.x >= 0.0f && _value.y >= 0.0f && _value.z >= 0.0f;
}

// A model name: letters and digits, in ASCII, at least one (§6.2).
[[nodiscard]] bool IsModelName(std::span<const std::uint8_t> _name) noexcept
{
  return !_name.empty() && std::ranges::all_of(_name,
                                               [](std::uint8_t _char) {
                                                 return (_char >= '0' && _char <= '9') || (_char >= 'A' && _char <= 'Z') ||
                                                        (_char >= 'a' && _char <= 'z');
                                               });
}

void WriteHeader(ByteWriter& _writer, MessageType _type)
{
  _writer.U16(static_cast<std::uint16_t>(_type));
  _writer.U16(MESSAGE_LAYOUT_VERSION);
  _writer.U32(0); // the size, which Finish fills in
}

void Write(ByteWriter& _writer, const Hello& _hello)
{
  WriteHeader(_writer, MessageType::Hello);
  _writer.U32(_hello.protocolVersion);
}

void Write(ByteWriter& _writer, const Welcome& _welcome)
{
  WriteHeader(_writer, MessageType::Welcome);
  _writer.U32(_welcome.protocolVersion);
  _writer.U32(_welcome.tickRate);
  _writer.U64(_welcome.tick);
  const WorldSettings& settings = _welcome.settings;
  _writer.Vector(settings.toSun);
  _writer.Vector(settings.sunRadiance);
  _writer.F32(settings.sunAngularRadiusRadians);
  _writer.Vector(settings.ambientUpper);
  _writer.Vector(settings.ambientLower);
  _writer.U32(settings.skySeed);
  _writer.Rotation(settings.galacticPlane);
  _writer.U32(static_cast<std::uint32_t>(_welcome.manifest.size()));
  for (const ManifestEntry& entry : _welcome.manifest)
  {
    if (entry.name.size() > MAX_MODEL_NAME_CHARS)
    {
      throw std::invalid_argument("A model name longer than a welcome can carry: " + entry.name);
    }
    _writer.U8(static_cast<std::uint8_t>(entry.name.size()));
    _writer.Text(entry.name);
    _writer.U64(entry.hash);
  }
}

void Write(ByteWriter& _writer, const Snapshot& _snapshot)
{
  WriteHeader(_writer, MessageType::Snapshot);
  _writer.U64(_snapshot.tick);
  _writer.U64(_snapshot.worldTick);
  _writer.U32(_snapshot.paused ? PAUSED_FLAG : 0u);
  _writer.U32(static_cast<std::uint32_t>(_snapshot.entities.size()));
  _writer.U32(static_cast<std::uint32_t>(_snapshot.detonations.size()));
  for (const EntityState& entity : _snapshot.entities)
  {
    _writer.U32(entity.id);
    _writer.U16(entity.modelIndex);
    _writer.U16(0); // the reserved flags
    _writer.Vector(entity.position);
    _writer.Rotation(entity.rotation);
    _writer.Vector(entity.velocity);
  }
  for (const DetonationEvent& detonation : _snapshot.detonations)
  {
    _writer.U32(detonation.entity);
    _writer.U32(detonation.seed);
    _writer.U64(detonation.worldTick);
    _writer.Vector(detonation.velocity);
  }
}

void Write(ByteWriter& _writer, const Command& _command)
{
  WriteHeader(_writer, MessageType::Command);
  _writer.U32(static_cast<std::uint32_t>(_command.kind));
  _writer.U32(_command.entity);
}

[[nodiscard]] Decoded ReadHello(ByteReader& _reader)
{
  const Hello hello{_reader.U32()};
  if (!_reader.Exhausted())
  {
    return std::unexpected(ProtocolError::MalformedMessage);
  }
  return hello;
}

[[nodiscard]] Decoded ReadWelcome(ByteReader& _reader)
{
  Welcome welcome{};
  welcome.protocolVersion = _reader.U32();
  welcome.tickRate = _reader.U32();
  welcome.tick = _reader.U64();
  WorldSettings& settings = welcome.settings;
  settings.toSun = _reader.Vector();
  settings.sunRadiance = _reader.Vector();
  settings.sunAngularRadiusRadians = _reader.F32();
  settings.ambientUpper = _reader.Vector();
  settings.ambientLower = _reader.Vector();
  settings.skySeed = _reader.U32();
  settings.galacticPlane = _reader.Rotation();
  const std::uint32_t modelCount = _reader.U32();
  if (_reader.Failed() || modelCount > _reader.Remaining() / MIN_MANIFEST_ENTRY_BYTES)
  {
    return std::unexpected(ProtocolError::MalformedMessage);
  }
  bool badName = false;
  welcome.manifest.reserve(modelCount);
  for (std::uint32_t i = 0; i < modelCount; ++i)
  {
    const std::span<const std::uint8_t> name = _reader.Bytes(_reader.U8());
    const std::uint64_t hash = _reader.U64();
    badName = badName || !IsModelName(name);
    welcome.manifest.push_back({std::string(name.begin(), name.end()), hash});
  }
  if (!_reader.Exhausted())
  {
    return std::unexpected(ProtocolError::MalformedMessage);
  }

  if (!IsFinite(settings.toSun) || !IsFinite(settings.sunRadiance) || !std::isfinite(settings.sunAngularRadiusRadians) ||
      !IsFinite(settings.ambientUpper) || !IsFinite(settings.ambientLower) || !IsFinite(settings.galacticPlane))
  {
    return std::unexpected(ProtocolError::NotFinite);
  }
  if (!IsUnitRotation(settings.galacticPlane))
  {
    return std::unexpected(ProtocolError::NotUnitRotation);
  }
  constexpr float QUARTER_TURN_RADIANS = 0.5f * std::numbers::pi_v<float>;
  if (welcome.tickRate == 0 || std::abs(Length(settings.toSun) - 1.0f) > UNIT_ROTATION_TOLERANCE || !IsNonNegative(settings.sunRadiance) ||
      !(settings.sunAngularRadiusRadians > 0.0f && settings.sunAngularRadiusRadians < QUARTER_TURN_RADIANS) ||
      !IsNonNegative(settings.ambientUpper) || !IsNonNegative(settings.ambientLower))
  {
    return std::unexpected(ProtocolError::MalformedMessage);
  }
  if (badName)
  {
    return std::unexpected(ProtocolError::BadName);
  }
  return welcome;
}

[[nodiscard]] Decoded ReadSnapshot(ByteReader& _reader, std::size_t _modelCount)
{
  Snapshot snapshot{};
  snapshot.tick = _reader.U64();
  snapshot.worldTick = _reader.U64();
  const std::uint32_t flags = _reader.U32();
  const std::uint32_t entityCount = _reader.U32();
  const std::uint32_t detonationCount = _reader.U32();
  if (_reader.Failed() || (flags & ~PAUSED_FLAG) != 0 ||
      std::uint64_t{entityCount} * ENTITY_RECORD_BYTES + std::uint64_t{detonationCount} * DETONATION_RECORD_BYTES != _reader.Remaining())
  {
    return std::unexpected(ProtocolError::MalformedMessage);
  }
  snapshot.paused = (flags & PAUSED_FLAG) != 0;

  // The records are read whole before any is judged, so that one refusal does not hide behind another's order.
  bool reservedSet = false;
  snapshot.entities.reserve(entityCount);
  for (std::uint32_t i = 0; i < entityCount; ++i)
  {
    EntityState entity{};
    entity.id = _reader.U32();
    entity.modelIndex = _reader.U16();
    reservedSet = reservedSet || _reader.U16() != 0;
    entity.position = _reader.Vector();
    entity.rotation = _reader.Rotation();
    entity.velocity = _reader.Vector();
    snapshot.entities.push_back(entity);
  }
  snapshot.detonations.reserve(detonationCount);
  for (std::uint32_t i = 0; i < detonationCount; ++i)
  {
    DetonationEvent detonation{};
    detonation.entity = _reader.U32();
    detonation.seed = _reader.U32();
    detonation.worldTick = _reader.U64();
    detonation.velocity = _reader.Vector();
    snapshot.detonations.push_back(detonation);
  }

  for (const EntityState& entity : snapshot.entities)
  {
    if (!IsFinite(entity.position) || !IsFinite(entity.rotation) || !IsFinite(entity.velocity))
    {
      return std::unexpected(ProtocolError::NotFinite);
    }
  }
  for (const DetonationEvent& detonation : snapshot.detonations)
  {
    if (!IsFinite(detonation.velocity))
    {
      return std::unexpected(ProtocolError::NotFinite);
    }
  }
  for (const EntityState& entity : snapshot.entities)
  {
    if (!IsUnitRotation(entity.rotation))
    {
      return std::unexpected(ProtocolError::NotUnitRotation);
    }
    if (entity.id == 0 || reservedSet)
    {
      return std::unexpected(ProtocolError::MalformedMessage);
    }
    if (entity.modelIndex >= _modelCount)
    {
      return std::unexpected(ProtocolError::BadModelIndex);
    }
  }

  std::vector<std::uint32_t> ids;
  ids.reserve(snapshot.entities.size());
  for (const EntityState& entity : snapshot.entities)
  {
    ids.push_back(entity.id);
  }
  std::ranges::sort(ids);
  if (std::ranges::adjacent_find(ids) != ids.end())
  {
    return std::unexpected(ProtocolError::DuplicateEntity);
  }

  std::vector<std::uint32_t> detonated;
  detonated.reserve(snapshot.detonations.size());
  for (const DetonationEvent& detonation : snapshot.detonations)
  {
    if (!std::ranges::binary_search(ids, detonation.entity))
    {
      return std::unexpected(ProtocolError::UnknownEntity);
    }
    if (detonation.worldTick > snapshot.worldTick)
    {
      return std::unexpected(ProtocolError::MalformedMessage);
    }
    detonated.push_back(detonation.entity);
  }
  // An entity has at most one detonation whose debris lasts.
  std::ranges::sort(detonated);
  if (std::ranges::adjacent_find(detonated) != detonated.end())
  {
    return std::unexpected(ProtocolError::MalformedMessage);
  }
  return snapshot;
}

[[nodiscard]] Decoded ReadCommand(ByteReader& _reader)
{
  const std::uint32_t kind = _reader.U32();
  const std::uint32_t entity = _reader.U32();
  if (!_reader.Exhausted())
  {
    return std::unexpected(ProtocolError::MalformedMessage);
  }
  switch (static_cast<CommandKind>(kind))
  {
  case CommandKind::Pause:
  case CommandKind::Resume:
    if (entity != 0)
    {
      return std::unexpected(ProtocolError::MalformedMessage);
    }
    return Command{static_cast<CommandKind>(kind), entity};
  case CommandKind::Detonate:
  case CommandKind::Restore:
    if (entity == 0)
    {
      return std::unexpected(ProtocolError::MalformedMessage);
    }
    return Command{static_cast<CommandKind>(kind), entity};
  }
  return std::unexpected(ProtocolError::MalformedMessage);
}

} // namespace

const char* ProtocolErrorName(ProtocolError _error) noexcept
{
  switch (_error)
  {
  case ProtocolError::Truncated:
    return "Truncated";
  case ProtocolError::UnknownMessage:
    return "UnknownMessage";
  case ProtocolError::UnsupportedVersion:
    return "UnsupportedVersion";
  case ProtocolError::MalformedMessage:
    return "MalformedMessage";
  case ProtocolError::BadName:
    return "BadName";
  case ProtocolError::NotFinite:
    return "NotFinite";
  case ProtocolError::NotUnitRotation:
    return "NotUnitRotation";
  case ProtocolError::DuplicateEntity:
    return "DuplicateEntity";
  case ProtocolError::BadModelIndex:
    return "BadModelIndex";
  case ProtocolError::UnknownEntity:
    return "UnknownEntity";
  }
  return "Unknown";
}

std::vector<std::uint8_t> EncodeMessage(const Message& _message)
{
  ByteWriter writer;
  std::visit([&writer](const auto& _content) { Write(writer, _content); }, _message);
  return std::move(writer).Finish();
}

std::expected<Message, ProtocolError> DecodeMessage(std::span<const std::uint8_t> _bytes, std::size_t _modelCount)
{
  if (_bytes.size() < MESSAGE_HEADER_BYTES)
  {
    return std::unexpected(ProtocolError::Truncated);
  }
  ByteReader header(_bytes.first(MESSAGE_HEADER_BYTES));
  const std::uint16_t type = header.U16();
  const std::uint16_t version = header.U16();
  const std::uint32_t size = header.U32();
  if (type < static_cast<std::uint16_t>(MessageType::Hello) || type > static_cast<std::uint16_t>(MessageType::Command))
  {
    return std::unexpected(ProtocolError::UnknownMessage);
  }
  if (version != MESSAGE_LAYOUT_VERSION)
  {
    return std::unexpected(ProtocolError::UnsupportedVersion);
  }
  if (size < MESSAGE_HEADER_BYTES || _bytes.size() > size)
  {
    return std::unexpected(ProtocolError::MalformedMessage);
  }
  if (_bytes.size() < size)
  {
    return std::unexpected(ProtocolError::Truncated);
  }

  ByteReader content(_bytes.subspan(MESSAGE_HEADER_BYTES));
  switch (static_cast<MessageType>(type))
  {
  case MessageType::Hello:
    return ReadHello(content);
  case MessageType::Welcome:
    return ReadWelcome(content);
  case MessageType::Snapshot:
    return ReadSnapshot(content, _modelCount);
  case MessageType::Command:
    return ReadCommand(content);
  }
  return std::unexpected(ProtocolError::UnknownMessage);
}

} // namespace NeuronCore
