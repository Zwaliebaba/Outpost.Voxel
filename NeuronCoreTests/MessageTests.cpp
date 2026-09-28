#include "pch.h"

#include "Float3.h"
#include "Hash.h"
#include "Message.h"
#include "Quaternion.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Message;
using NeuronCore::ProtocolError;
using Bytes = std::vector<std::uint8_t>;

// Where the layout of Design/SpaceScene.md §6.2 puts the fields the refusals below change.
constexpr std::size_t TYPE_OFFSET = 0;
constexpr std::size_t VERSION_OFFSET = 2;
constexpr std::size_t SIZE_OFFSET = 4;
constexpr std::size_t HELLO_BYTES = 12;
constexpr std::size_t WELCOME_TICK_RATE = 12;
constexpr std::size_t WELCOME_TO_SUN = 24;
constexpr std::size_t WELCOME_RADIANCE = 36;
constexpr std::size_t WELCOME_ANGULAR_RADIUS = 48;
constexpr std::size_t WELCOME_AMBIENT_LOWER = 64;
constexpr std::size_t WELCOME_GALACTIC_PLANE = 80;
constexpr std::size_t WELCOME_MODEL_COUNT = 96;
constexpr std::size_t WELCOME_FIRST_NAME = 101;
constexpr std::size_t SNAPSHOT_WORLD_TICK = 16;
constexpr std::size_t SNAPSHOT_FLAGS = 24;
constexpr std::size_t SNAPSHOT_ENTITY_COUNT = 28;
constexpr std::size_t SNAPSHOT_FIRST_ENTITY = 36;
constexpr std::size_t ENTITY_MODEL_INDEX = 4;
constexpr std::size_t ENTITY_FLAGS = 6;
constexpr std::size_t ENTITY_POSITION = 8;
constexpr std::size_t ENTITY_ROTATION = 20;
constexpr std::size_t ENTITY_VELOCITY = 36;
constexpr std::size_t DETONATION_WORLD_TICK = 8;
constexpr std::size_t DETONATION_VELOCITY = 16;
constexpr std::size_t COMMAND_KIND = 8;
constexpr std::size_t COMMAND_ENTITY = 12;

// The models the sample snapshot's entities name.
constexpr std::size_t MODEL_COUNT = 3;

// One message of each type, encoded by hand from §6.2's layout with Python's struct module rather than by the encoder
// under test, so that a change of encoding shows here as a diff.
constexpr std::array<std::uint8_t, 12> GOLDEN_HELLO{
  0x01, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 116> GOLDEN_WELCOME{
  0x02, 0x00, 0x01, 0x00, 0x74, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x33, 0x33, 0x33, 0x3F,
  0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x3F, 0x00, 0x00, 0x80, 0x3E, 0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D,
  0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D, 0x01, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x01, 0x00, 0x00, 0x00,
  0x07, 0x46, 0x72, 0x69, 0x67, 0x61, 0x74, 0x65, 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,
};
constexpr std::array<std::uint8_t, 112> GOLDEN_SNAPSHOT{
  0x03, 0x00, 0x01, 0x00, 0x70, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x3F, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBF, 0x03, 0x00, 0x00, 0x00, 0xEF, 0xBE, 0xAD, 0xDE,
  0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
};
constexpr std::array<std::uint8_t, 16> GOLDEN_COMMAND{
  0x04, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
};

[[nodiscard]] NeuronCore::Welcome GoldenWelcome()
{
  return {.protocolVersion = NeuronCore::PROTOCOL_VERSION,
          .tickRate = 30,
          .tick = 5,
          .settings = {.toSun = {0.0f, 1.0f, 0.0f},
                       .sunRadiance = {0.7f, 0.7f, 0.7f},
                       .sunAngularRadiusRadians = 0.25f,
                       .ambientUpper = {0.05f, 0.05f, 0.05f},
                       .ambientLower = {0.05f, 0.05f, 0.05f},
                       .skySeed = 1,
                       .galacticPlane = {0.0f, 0.0f, 0.0f, 1.0f}},
          .manifest = {{"Frigate", 0x0123456789ABCDEFull}}};
}

[[nodiscard]] NeuronCore::Snapshot GoldenSnapshot()
{
  return {.tick = 10,
          .worldTick = 9,
          .paused = false,
          .entities = {{3, 1, {1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.0f, -0.5f}}},
          .detonations = {{3, 0xDEADBEEFu, 8, {0.0f, 0.0f, 2.0f}}}};
}

// A welcome of three models, and a snapshot of three entities, one detonated, with rotations off the axes.
[[nodiscard]] NeuronCore::Welcome SampleWelcome()
{
  NeuronCore::Welcome welcome = GoldenWelcome();
  welcome.settings.toSun = NeuronCore::Normalize({0.3f, 0.8f, -0.52f});
  welcome.settings.galacticPlane = {0.5f, 0.0f, 0.0f, 0.8660254f};
  welcome.manifest = {{"MilitaryStation", 1}, {"CapitalShip", 0xFFFFFFFFFFFFFFFFull}, {"Frigate2", 0}};
  return welcome;
}

[[nodiscard]] NeuronCore::Snapshot SampleSnapshot()
{
  const NeuronCore::Quaternion turned = NeuronCore::QuaternionOf(NeuronCore::RotationOf({0.21f, -0.37f, 0.12f, 0.896f}));
  return {.tick = 0xFFFFFFFF12345678ull,
          .worldTick = 1000,
          .paused = true,
          .entities = {{1, 0, {-2.5f, 0.0f, 16383.5f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
                       {2, 2, {100.25f, -4.0f, 7.0f}, turned, {60.0f, -1.5f, 0.0f}},
                       {0xFFFFFFFFu, 1, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -20.0f}}},
          .detonations = {{2, 7, 999, {60.0f, -1.5f, 0.0f}}}};
}

// Every message the tests encode: each type, and each command.
[[nodiscard]] std::vector<Message> SampleMessages()
{
  NeuronCore::Welcome empty = GoldenWelcome();
  empty.manifest.clear();
  return {NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION},
          NeuronCore::Hello{0xFFFFFFFFu},
          GoldenWelcome(),
          SampleWelcome(),
          empty,
          GoldenSnapshot(),
          SampleSnapshot(),
          NeuronCore::Snapshot{},
          NeuronCore::Command{NeuronCore::CommandKind::Pause, 0},
          NeuronCore::Command{NeuronCore::CommandKind::Resume, 0},
          NeuronCore::Command{NeuronCore::CommandKind::Detonate, 7},
          NeuronCore::Command{NeuronCore::CommandKind::Restore, 0xFFFFFFFFu}};
}

void PutU16(Bytes& _bytes, std::size_t _offset, std::uint16_t _value)
{
  _bytes[_offset] = static_cast<std::uint8_t>(_value);
  _bytes[_offset + 1] = static_cast<std::uint8_t>(_value >> 8);
}

void PutU32(Bytes& _bytes, std::size_t _offset, std::uint32_t _value)
{
  for (std::size_t i = 0; i < 4; ++i)
  {
    _bytes[_offset + i] = static_cast<std::uint8_t>(_value >> (8 * i));
  }
}

void PutU64(Bytes& _bytes, std::size_t _offset, std::uint64_t _value)
{
  for (std::size_t i = 0; i < 8; ++i)
  {
    _bytes[_offset + i] = static_cast<std::uint8_t>(_value >> (8 * i));
  }
}

void PutF32(Bytes& _bytes, std::size_t _offset, float _value)
{
  PutU32(_bytes, _offset, std::bit_cast<std::uint32_t>(_value));
}

[[nodiscard]] std::uint32_t GetU32(const Bytes& _bytes, std::size_t _offset)
{
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i)
  {
    value |= std::uint32_t{_bytes[_offset + i]} << (8 * i);
  }
  return value;
}

[[nodiscard]] std::wstring Widen(std::string_view _text)
{
  return {_text.begin(), _text.end()};
}

void ExpectRefusal(ProtocolError _expected, const Bytes& _bytes, const std::wstring& _case, std::size_t _modelCount = MODEL_COUNT)
{
  const auto decoded = NeuronCore::DecodeMessage(_bytes, _modelCount);
  Assert::IsFalse(decoded.has_value(), (_case + L": the message was accepted").c_str());
  Assert::AreEqual(NeuronCore::ProtocolErrorName(_expected), NeuronCore::ProtocolErrorName(decoded.error()), _case.c_str());
}

[[nodiscard]] Bytes Encoded(const Message& _message)
{
  return NeuronCore::EncodeMessage(_message);
}

// The sample snapshot's bytes, with its first entity's record at SNAPSHOT_FIRST_ENTITY.
[[nodiscard]] Bytes SnapshotBytes()
{
  return Encoded(SampleSnapshot());
}

// Where the sample snapshot's first detonation starts.
[[nodiscard]] std::size_t FirstDetonation()
{
  return SNAPSHOT_FIRST_ENTITY + SampleSnapshot().entities.size() * NeuronCore::ENTITY_RECORD_BYTES;
}

} // namespace

// Design/SpaceScene.md §6.2 and §15: the messages round-trip, every truncation and every refusal is refused by name,
// and one message of each type is pinned to its bytes.
TEST_CLASS(MessageTests)
{
public:
  // Each message decodes to one that encodes to the same bytes, the header's size is the message's, and each decodes
  // as its own type.
  TEST_METHOD(EveryMessageRoundTrips)
  {
    for (const Message& message : SampleMessages())
    {
      const Bytes bytes = Encoded(message);
      const std::wstring what = std::format(L"message type {}, {} bytes", message.index(), bytes.size());
      Assert::AreEqual(static_cast<std::uint32_t>(bytes.size()), GetU32(bytes, SIZE_OFFSET), what.c_str());
      const auto decoded = NeuronCore::DecodeMessage(bytes, MODEL_COUNT);
      Assert::IsTrue(
        decoded.has_value(),
        (what + L": refused as " + Widen(decoded.has_value() ? "nothing" : NeuronCore::ProtocolErrorName(decoded.error()))).c_str());
      const Message value = decoded.value_or(Message{});
      Assert::AreEqual(message.index(), value.index(), what.c_str());
      Assert::IsTrue(Encoded(value) == bytes, what.c_str());
    }
  }

  // The decoded fields are the ones encoded, down to the snapshot's pause, the tick's top bits and a name's case.
  TEST_METHOD(DecodesWhatWasEncoded)
  {
    const auto snapshot = NeuronCore::DecodeMessage(SnapshotBytes(), MODEL_COUNT);
    Assert::IsTrue(snapshot.has_value() && std::holds_alternative<NeuronCore::Snapshot>(*snapshot));
    const auto decoded = std::get<NeuronCore::Snapshot>(snapshot.value_or(Message{NeuronCore::Snapshot{}}));
    const NeuronCore::Snapshot expected = SampleSnapshot();
    Assert::AreEqual(expected.tick, decoded.tick);
    Assert::AreEqual(expected.worldTick, decoded.worldTick);
    Assert::IsTrue(decoded.paused);
    Assert::AreEqual(expected.entities.size(), decoded.entities.size());
    Assert::AreEqual(expected.entities[2].id, decoded.entities[2].id);
    Assert::AreEqual(expected.entities[1].rotation.x, decoded.entities[1].rotation.x);
    Assert::AreEqual(expected.entities[0].position.z, decoded.entities[0].position.z);
    Assert::AreEqual(expected.detonations[0].seed, decoded.detonations[0].seed);

    const auto welcome = NeuronCore::DecodeMessage(Encoded(SampleWelcome()), MODEL_COUNT);
    Assert::IsTrue(welcome.has_value() && std::holds_alternative<NeuronCore::Welcome>(*welcome));
    const std::vector<NeuronCore::ManifestEntry> manifest =
      std::get<NeuronCore::Welcome>(welcome.value_or(Message{NeuronCore::Welcome{}})).manifest;
    Assert::AreEqual(std::size_t{3}, manifest.size());
    Assert::AreEqual(std::string("CapitalShip"), manifest[1].name);
    Assert::AreEqual(std::uint64_t{0xFFFFFFFFFFFFFFFFull}, manifest[1].hash);
  }

  // §15: one message of each type encodes to the bytes written above, and those bytes decode.
  TEST_METHOD(EncodesTheGoldenBytes)
  {
    Assert::IsTrue(std::ranges::equal(Encoded(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION}), GOLDEN_HELLO), L"Hello");
    Assert::IsTrue(std::ranges::equal(Encoded(GoldenWelcome()), GOLDEN_WELCOME), L"Welcome");
    Assert::IsTrue(std::ranges::equal(Encoded(GoldenSnapshot()), GOLDEN_SNAPSHOT), L"Snapshot");
    Assert::IsTrue(std::ranges::equal(Encoded(NeuronCore::Command{NeuronCore::CommandKind::Detonate, 7}), GOLDEN_COMMAND), L"Command");
    for (const std::span<const std::uint8_t> golden :
         {std::span<const std::uint8_t>(GOLDEN_HELLO), std::span<const std::uint8_t>(GOLDEN_WELCOME),
          std::span<const std::uint8_t>(GOLDEN_SNAPSHOT), std::span<const std::uint8_t>(GOLDEN_COMMAND)})
    {
      Assert::IsTrue(NeuronCore::DecodeMessage(golden, MODEL_COUNT).has_value(), L"the golden bytes decode");
    }
  }

  // §15: every prefix of every message, down to nothing, is refused as truncated.
  TEST_METHOD(RefusesEveryTruncation)
  {
    for (const Message& message : SampleMessages())
    {
      const Bytes bytes = Encoded(message);
      for (std::size_t length = 0; length < bytes.size(); ++length)
      {
        ExpectRefusal(ProtocolError::Truncated, Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length)),
                      std::format(L"message type {} cut to {} of {} bytes", message.index(), length, bytes.size()));
      }
    }
  }

  TEST_METHOD(RefusesUnknownTypesAndVersions)
  {
    for (const std::uint16_t type : {std::uint16_t{0}, std::uint16_t{5}, std::uint16_t{0xFFFF}})
    {
      Bytes bytes(GOLDEN_HELLO.begin(), GOLDEN_HELLO.end());
      PutU16(bytes, TYPE_OFFSET, type);
      ExpectRefusal(ProtocolError::UnknownMessage, bytes, std::format(L"type {}", type));
    }
    for (const std::uint16_t version : {std::uint16_t{0}, std::uint16_t{2}, std::uint16_t{0xFFFF}})
    {
      Bytes bytes(GOLDEN_COMMAND.begin(), GOLDEN_COMMAND.end());
      PutU16(bytes, VERSION_OFFSET, version);
      ExpectRefusal(ProtocolError::UnsupportedVersion, bytes, std::format(L"version {}", version));
    }
  }

  // A size or count that disagrees with the content, and a reserved field that is not zero.
  TEST_METHOD(RefusesMalformedMessages)
  {
    {
      Bytes bytes(GOLDEN_HELLO.begin(), GOLDEN_HELLO.end());
      PutU32(bytes, SIZE_OFFSET, 7);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a size smaller than the header");
    }
    {
      Bytes bytes(GOLDEN_COMMAND.begin(), GOLDEN_COMMAND.end());
      bytes.push_back(0);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a byte after the size");
    }
    {
      Bytes bytes(GOLDEN_HELLO.begin(), GOLDEN_HELLO.end());
      bytes.pop_back();
      PutU32(bytes, SIZE_OFFSET, static_cast<std::uint32_t>(HELLO_BYTES - 1));
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a hello too short for its version");
    }
    {
      Bytes bytes(GOLDEN_HELLO.begin(), GOLDEN_HELLO.end());
      bytes.push_back(0);
      PutU32(bytes, SIZE_OFFSET, static_cast<std::uint32_t>(HELLO_BYTES + 1));
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a hello with a byte to spare");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU32(bytes, SNAPSHOT_ENTITY_COUNT, 4);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"an entity count beyond the records");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU32(bytes, SNAPSHOT_FLAGS, 2);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a reserved snapshot flag");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU16(bytes, SNAPSHOT_FIRST_ENTITY + ENTITY_FLAGS, 1);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a reserved entity flag");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU32(bytes, SNAPSHOT_FIRST_ENTITY, 0);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"entity 0");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU64(bytes, FirstDetonation() + DETONATION_WORLD_TICK, SampleSnapshot().worldTick + 1);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a detonation after the snapshot");
    }
    {
      NeuronCore::Snapshot twice = SampleSnapshot();
      twice.detonations.push_back(twice.detonations.front());
      ExpectRefusal(ProtocolError::MalformedMessage, Encoded(twice), L"two detonations of one entity");
    }
    {
      Bytes bytes = Encoded(SampleWelcome());
      PutU32(bytes, WELCOME_TICK_RATE, 0);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a tick rate of 0");
    }
    {
      Bytes bytes = Encoded(SampleWelcome());
      PutF32(bytes, WELCOME_TO_SUN, 2.0f);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a sun direction of more than unit length");
    }
    {
      Bytes bytes = Encoded(SampleWelcome());
      PutF32(bytes, WELCOME_RADIANCE + 4, -0.1f);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a negative radiance");
    }
    {
      Bytes bytes = Encoded(SampleWelcome());
      PutF32(bytes, WELCOME_AMBIENT_LOWER, -1.0f);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a negative ambient");
    }
    for (const float radius : {0.0f, -0.1f, 1.5708f, 4.0f})
    {
      Bytes bytes = Encoded(SampleWelcome());
      PutF32(bytes, WELCOME_ANGULAR_RADIUS, radius);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, std::format(L"a sun of angular radius {}", radius));
    }
    {
      Bytes bytes = Encoded(SampleWelcome());
      PutU32(bytes, WELCOME_MODEL_COUNT, 4);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a model count beyond the entries");
    }
    {
      Bytes bytes = Encoded(SampleWelcome());
      PutU32(bytes, WELCOME_MODEL_COUNT, 0xFFFFFFFFu);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a model count no message could hold");
    }
    for (const std::uint32_t kind : {0u, 5u, 0xFFFFFFFFu})
    {
      Bytes bytes(GOLDEN_COMMAND.begin(), GOLDEN_COMMAND.end());
      PutU32(bytes, COMMAND_KIND, kind);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, std::format(L"command kind {}", kind));
    }
    {
      Bytes bytes = Encoded(NeuronCore::Command{NeuronCore::CommandKind::Pause, 0});
      PutU32(bytes, COMMAND_ENTITY, 3);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a pause naming an entity");
    }
    {
      Bytes bytes(GOLDEN_COMMAND.begin(), GOLDEN_COMMAND.end());
      PutU32(bytes, COMMAND_ENTITY, 0);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a detonation naming no entity");
    }
  }

  TEST_METHOD(RefusesBadNames)
  {
    for (const std::uint8_t bad :
         {std::uint8_t{'_'}, std::uint8_t{'.'}, std::uint8_t{' '}, std::uint8_t{'/'}, std::uint8_t{0}, std::uint8_t{0xC3}})
    {
      Bytes bytes = Encoded(SampleWelcome());
      bytes[WELCOME_FIRST_NAME + 3] = bad;
      ExpectRefusal(ProtocolError::BadName, bytes, std::format(L"a name with byte {}", bad));
    }
    NeuronCore::Welcome empty = SampleWelcome();
    empty.manifest.back().name.clear();
    ExpectRefusal(ProtocolError::BadName, Encoded(empty), L"an empty name");
    NeuronCore::Welcome longest = SampleWelcome();
    longest.manifest.back().name = std::string(NeuronCore::MAX_MODEL_NAME_CHARS, 'a');
    Assert::IsTrue(NeuronCore::DecodeMessage(Encoded(longest), MODEL_COUNT).has_value(), L"a name of the longest length");
    longest.manifest.back().name.push_back('a');
    bool refused = false;
    try
    {
      static_cast<void>(Encoded(longest));
    }
    catch (const std::invalid_argument&)
    {
      refused = true;
    }
    Assert::IsTrue(refused, L"a name longer than a welcome can carry is not encoded");
  }

  TEST_METHOD(RefusesWhatIsNotFinite)
  {
    constexpr float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();
    constexpr float POSITIVE_INFINITY = std::numeric_limits<float>::infinity();
    for (const std::size_t field : {ENTITY_POSITION, ENTITY_POSITION + 8, ENTITY_ROTATION + 12, ENTITY_VELOCITY + 4})
    {
      for (const float value : {NOT_A_NUMBER, POSITIVE_INFINITY, -POSITIVE_INFINITY})
      {
        Bytes bytes = SnapshotBytes();
        PutF32(bytes, SNAPSHOT_FIRST_ENTITY + NeuronCore::ENTITY_RECORD_BYTES + field, value);
        ExpectRefusal(ProtocolError::NotFinite, bytes, std::format(L"entity field at {} set to {}", field, value));
      }
    }
    {
      Bytes bytes = SnapshotBytes();
      PutF32(bytes, FirstDetonation() + DETONATION_VELOCITY, NOT_A_NUMBER);
      ExpectRefusal(ProtocolError::NotFinite, bytes, L"a detonation's velocity");
    }
    for (const std::size_t field :
         {WELCOME_TO_SUN, WELCOME_RADIANCE, WELCOME_ANGULAR_RADIUS, WELCOME_AMBIENT_LOWER + 8, WELCOME_GALACTIC_PLANE})
    {
      Bytes bytes = Encoded(SampleWelcome());
      PutF32(bytes, field, POSITIVE_INFINITY);
      ExpectRefusal(ProtocolError::NotFinite, bytes, std::format(L"welcome field at {}", field));
    }
  }

  // NVF's rule: unit length within 1e-4, and w >= 0 (Design/NeuronVoxelFormat.md).
  TEST_METHOD(RefusesRotationsThatAreNotUnit)
  {
    {
      NeuronCore::Snapshot snapshot = SampleSnapshot();
      snapshot.entities[0].rotation = {0.0f, 0.0f, 0.0f, 1.0002f};
      ExpectRefusal(ProtocolError::NotUnitRotation, Encoded(snapshot), L"a rotation too long");
      snapshot.entities[0].rotation = {0.0f, 0.0f, 0.0f, 0.9998f};
      ExpectRefusal(ProtocolError::NotUnitRotation, Encoded(snapshot), L"a rotation too short");
      snapshot.entities[0].rotation = {0.0f, 0.0f, 0.0f, 1.00009f};
      Assert::IsTrue(NeuronCore::DecodeMessage(Encoded(snapshot), MODEL_COUNT).has_value(), L"a rotation within the tolerance");
      snapshot.entities[0].rotation = {0.0f, 0.6f, 0.0f, -0.8f};
      ExpectRefusal(ProtocolError::NotUnitRotation, Encoded(snapshot), L"w below 0");
    }
    {
      NeuronCore::Welcome welcome = SampleWelcome();
      welcome.settings.galacticPlane = {1.0f, 1.0f, 0.0f, 0.0f};
      ExpectRefusal(ProtocolError::NotUnitRotation, Encoded(welcome), L"the galactic plane");
    }
  }

  TEST_METHOD(RefusesEntitiesThatDoNotAddUp)
  {
    {
      NeuronCore::Snapshot snapshot = SampleSnapshot();
      snapshot.entities[2].id = snapshot.entities[0].id;
      ExpectRefusal(ProtocolError::DuplicateEntity, Encoded(snapshot), L"two records of one entity");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU16(bytes, SNAPSHOT_FIRST_ENTITY + ENTITY_MODEL_INDEX, static_cast<std::uint16_t>(MODEL_COUNT));
      ExpectRefusal(ProtocolError::BadModelIndex, bytes, L"a model index past the manifest");
      ExpectRefusal(ProtocolError::BadModelIndex, SnapshotBytes(), L"a snapshot before any manifest", 0);
    }
    {
      NeuronCore::Snapshot snapshot = SampleSnapshot();
      snapshot.detonations[0].entity = 4;
      ExpectRefusal(ProtocolError::UnknownEntity, Encoded(snapshot), L"a detonation of an entity not in the snapshot");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU64(bytes, SNAPSHOT_WORLD_TICK, 0);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a world tick before its detonation");
    }
  }

  // The manifest's hash is 64-bit FNV-1a, whose published test vectors pin it.
  TEST_METHOD(HashesFilesWithFnv1a)
  {
    const auto hash = [](std::string_view _text)
    { return NeuronCore::Fnv1aHash64({reinterpret_cast<const std::uint8_t*>(_text.data()), _text.size()}); };
    Assert::AreEqual(std::uint64_t{0xCBF29CE484222325ull}, hash(""));
    Assert::AreEqual(std::uint64_t{0xAF63DC4C8601EC8Cull}, hash("a"));
    Assert::AreEqual(std::uint64_t{0x85944171F73967E8ull}, hash("foobar"));
  }
};

} // namespace NeuronCoreTests
