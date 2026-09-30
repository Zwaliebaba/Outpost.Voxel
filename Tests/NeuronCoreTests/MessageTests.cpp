#include "pch.h"

#include "CubeSymmetry.h"

#include "Composite.h"
#include "Float3.h"
#include "Hash.h"
#include "Message.h"
#include "NvfImport.h"
#include "Quaternion.h"
#include "RigidTransform.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
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

// Where the layout of Design/Archive/SpaceScene.md §6.2 and Design/ADR/ADR-029 puts the fields the refusals below change.
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
constexpr std::size_t SNAPSHOT_MASK_COUNT = 36;
constexpr std::size_t SNAPSHOT_MASK_BYTES = 40;
constexpr std::size_t SNAPSHOT_PAYLOAD_BYTES = 44;
constexpr std::size_t SNAPSHOT_FIRST_ENTITY = 48;
constexpr std::size_t ENTITY_COMPOSITE = 4;
constexpr std::size_t ENTITY_SIDE = 6;
constexpr std::size_t ENTITY_FLAGS = 7;
constexpr std::size_t ENTITY_POSITION = 8;
constexpr std::size_t ENTITY_ROTATION = 20;
constexpr std::size_t ENTITY_VELOCITY = 36;
constexpr std::size_t DETONATION_WORLD_TICK = 8;
constexpr std::size_t DETONATION_VELOCITY = 16;
constexpr std::size_t MASK_VOXEL_COUNT = 4;
constexpr std::size_t MASK_HEADER_BYTES = 8;
constexpr std::size_t COMMAND_KIND = 8;
constexpr std::size_t COMMAND_ENTITY = 12;
constexpr std::size_t COMMAND_PAYLOAD_BYTES = 16;
constexpr std::size_t COMPONENT_RESERVED = 2;
constexpr std::size_t COMPONENT_TRANSLATION = 4;
constexpr std::size_t COMPONENT_ROTATION = 16;
constexpr std::size_t SIDE_RESERVED = 3;

// What the sample welcome names, which the sample snapshot's entities are held to: four composites and two sides.
constexpr NeuronCore::WelcomeCounts COUNTS{4, 2};

// One message of each type, encoded by hand from the layout with Python's struct module rather than by the encoder
// under test, so that a change of encoding shows here as a diff.
constexpr std::array<std::uint8_t, 12> GOLDEN_HELLO{
  0x01, 0x00, 0x05, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 211> GOLDEN_WELCOME{
  0x02, 0x00, 0x05, 0x00, 0xD3, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x3F,
  0x33, 0x33, 0x33, 0x3F, 0x00, 0x00, 0x80, 0x3E, 0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC,
  0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x01, 0x00, 0x00, 0x00, 0x07, 0x46, 0x72, 0x69, 0x67, 0x61, 0x74, 0x65, 0xEF, 0xCD,
  0xAB, 0x89, 0x67, 0x45, 0x23, 0x01, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xF3, 0x04, 0x35, 0x3F, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x04, 0x35, 0x3F, 0x02, 0x00, 0x00, 0x00, 0x28, 0x78, 0xDC, 0x00, 0xDC, 0x50,
  0x3C, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xCA, 0xFE, 0x01,
};
constexpr std::array<std::uint8_t, 136> GOLDEN_SNAPSHOT{
  0x03, 0x00, 0x05, 0x00, 0x88, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x02, 0x00,
  0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0xBF, 0x03, 0x00, 0x00, 0x00, 0xEF, 0xBE, 0xAD, 0xDE, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x03, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x21, 0x02, 0x5A, 0xA5,
};
constexpr std::array<std::uint8_t, 20> GOLDEN_COMMAND{
  0x04, 0x00, 0x05, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 23> GOLDEN_GAME_COMMAND{
  0x04, 0x00, 0x05, 0x00, 0x17, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
};

// A quarter turn about y, as the cube's rotations are stored: sin 45° and cos 45°, rounded to float.
constexpr NeuronCore::Quaternion QUARTER_TURN_ABOUT_Y{0.0f, 0.70710677f, 0.0f, 0.70710677f};

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
          .manifest = {{"Frigate", 0x0123456789ABCDEFull}},
          .composites = {{{{0, {0, 0, 0}, {0.0f, 0.0f, 0.0f, 1.0f}}, {0, {3, -2, 5}, QUARTER_TURN_ABOUT_Y}}}},
          .sides = {{40, 120, 220}, {220, 80, 60}},
          .sessionSide = 1,
          .payload = {0xCA, 0xFE, 0x01}};
}

[[nodiscard]] NeuronCore::Snapshot GoldenSnapshot()
{
  return {.tick = 10,
          .worldTick = 9,
          .paused = false,
          .entities = {{3, 1, 2, {1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.0f, -0.5f}}},
          .detonations = {{3, 0xDEADBEEFu, 8, {0.0f, 0.0f, 2.0f}}},
          .masks = {{3, 10, {0x21, 0x02}}}, // voxels 0, 5 and 9 of 10 gone
          .payload = {0x5A, 0xA5}};
}

// A welcome of three models, each a composite alone, and a fourth composite of all three turned and moved; and a
// snapshot of three entities, one detonated and two with voxels gone, with rotations off the axes, of both sides and of
// none.
[[nodiscard]] NeuronCore::Welcome SampleWelcome()
{
  NeuronCore::Welcome welcome = GoldenWelcome();
  welcome.settings.toSun = NeuronCore::Normalize({0.3f, 0.8f, -0.52f});
  welcome.settings.galacticPlane = {0.5f, 0.0f, 0.0f, 0.8660254f};
  welcome.manifest = {{"MilitaryStation", 1}, {"CapitalShip", 0xFFFFFFFFFFFFFFFFull}, {"Frigate2", 0}};
  welcome.composites = NeuronCore::SingleModelComposites(welcome.manifest.size());
  welcome.composites.push_back(
    {{{1, {0, 0, 0}, {0.0f, 0.0f, 0.0f, 1.0f}},
      {2, {-NeuronCore::MAX_COMPONENT_TRANSLATION, 7, NeuronCore::MAX_COMPONENT_TRANSLATION}, QUARTER_TURN_ABOUT_Y},
      {0, {12, -40, 3}, {0.5f, 0.5f, 0.5f, 0.5f}},
      {2, {0, 0, 1}, {1.0f, 0.0f, 0.0f, 0.0f}}}});
  welcome.payload = {0x00, 0xFF, 0x10, 0x80, 0x7F};
  return welcome;
}

// Where _welcome's composite count lies in its bytes: after the manifest, each entry a length byte, the name and the hash.
[[nodiscard]] std::size_t CompositesOffset(const NeuronCore::Welcome& _welcome)
{
  std::size_t offset = WELCOME_MODEL_COUNT + 4;
  for (const NeuronCore::ManifestEntry& entry : _welcome.manifest)
  {
    offset += 1 + entry.name.size() + 8;
  }
  return offset;
}

// Where component _component of _welcome's composite _composite starts in its bytes.
[[nodiscard]] std::size_t ComponentOffset(const NeuronCore::Welcome& _welcome, std::size_t _composite, std::size_t _component)
{
  std::size_t offset = CompositesOffset(_welcome) + 4;
  for (std::size_t composite = 0; composite < _composite; ++composite)
  {
    offset += 4 + _welcome.composites[composite].components.size() * NeuronCore::COMPONENT_RECORD_BYTES;
  }
  return offset + 4 + _component * NeuronCore::COMPONENT_RECORD_BYTES;
}

// Where _welcome's side count lies in its bytes, after its composites.
[[nodiscard]] std::size_t SidesOffset(const NeuronCore::Welcome& _welcome)
{
  return ComponentOffset(_welcome, _welcome.composites.size(), 0) - 4;
}

// Where the side _welcome's session plays lies in its bytes, after its sides (Design/ADR/ADR-032).
[[nodiscard]] std::size_t SessionSideOffset(const NeuronCore::Welcome& _welcome)
{
  return SidesOffset(_welcome) + 4 + _welcome.sides.size() * NeuronCore::SIDE_RECORD_BYTES;
}

// Where _welcome's payload size lies in its bytes, after the session's side and its three reserved bytes.
[[nodiscard]] std::size_t PayloadOffset(const NeuronCore::Welcome& _welcome)
{
  return SessionSideOffset(_welcome) + 4;
}

[[nodiscard]] NeuronCore::Snapshot SampleSnapshot()
{
  const NeuronCore::Quaternion turned = NeuronCore::QuaternionOf(NeuronCore::RotationOf({0.21f, -0.37f, 0.12f, 0.896f}));
  return {.tick = 0xFFFFFFFF12345678ull,
          .worldTick = 1000,
          .paused = true,
          .entities = {{1, 0, 0, {-2.5f, 0.0f, 16383.5f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
                       {2, 3, 2, {100.25f, -4.0f, 7.0f}, turned, {60.0f, -1.5f, 0.0f}},
                       {0xFFFFFFFFu, 1, 1, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -20.0f}}},
          .detonations = {{2, 7, 999, {60.0f, -1.5f, 0.0f}}},
          .masks = {{0xFFFFFFFFu, 8, {0x80}}, {2, 17, {0xFF, 0x00, 0x01}}},
          .payload = {1, 2, 3, 4, 5, 6, 7}};
}

// Every message the tests encode: each type, and each command.
[[nodiscard]] std::vector<Message> SampleMessages()
{
  NeuronCore::Welcome empty = GoldenWelcome();
  empty.manifest.clear();
  empty.composites.clear();
  empty.sides.clear();
  empty.sessionSide = NeuronCore::OBSERVER_SIDE;
  empty.payload.clear();
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
          NeuronCore::Command{NeuronCore::CommandKind::Restore, 0xFFFFFFFFu},
          NeuronCore::Command{NeuronCore::CommandKind::Game, 0, {1, 2, 3}},
          NeuronCore::Command{NeuronCore::CommandKind::Game, 0, Bytes(300, 0xAB)}};
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

void ExpectRefusal(ProtocolError _expected, const Bytes& _bytes, const std::wstring& _case, NeuronCore::WelcomeCounts _counts = COUNTS)
{
  const auto decoded = NeuronCore::DecodeMessage(_bytes, _counts);
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

// Where the sample snapshot's first mask starts, after its detonations.
[[nodiscard]] std::size_t FirstMask()
{
  return FirstDetonation() + SampleSnapshot().detonations.size() * NeuronCore::DETONATION_RECORD_BYTES;
}

} // namespace

// Design/Archive/SpaceScene.md §6.2 and §15: the messages round-trip, every truncation and every refusal is refused by name,
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
      const auto decoded = NeuronCore::DecodeMessage(bytes, COUNTS);
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
    const auto snapshot = NeuronCore::DecodeMessage(SnapshotBytes(), COUNTS);
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
    Assert::AreEqual(expected.entities[1].composite, decoded.entities[1].composite);
    Assert::IsTrue(expected.entities[1].side == decoded.entities[1].side, L"the side");
    Assert::IsTrue(expected.payload == decoded.payload, L"the snapshot's payload");
    Assert::AreEqual(expected.masks.size(), decoded.masks.size());
    for (std::size_t i = 0; i < expected.masks.size(); ++i)
    {
      Assert::AreEqual(expected.masks[i].entity, decoded.masks[i].entity, L"a mask's entity");
      Assert::AreEqual(expected.masks[i].voxelCount, decoded.masks[i].voxelCount, L"a mask's voxel count");
      Assert::IsTrue(expected.masks[i].gone == decoded.masks[i].gone, L"a mask's bits");
    }

    const auto welcome = NeuronCore::DecodeMessage(Encoded(SampleWelcome()), COUNTS);
    Assert::IsTrue(welcome.has_value() && std::holds_alternative<NeuronCore::Welcome>(*welcome));
    const NeuronCore::Welcome decodedWelcome = std::get<NeuronCore::Welcome>(welcome.value_or(Message{NeuronCore::Welcome{}}));
    const std::vector<NeuronCore::ManifestEntry>& manifest = decodedWelcome.manifest;
    Assert::AreEqual(std::size_t{3}, manifest.size());
    Assert::AreEqual(std::string("CapitalShip"), manifest[1].name);
    Assert::AreEqual(std::uint64_t{0xFFFFFFFFFFFFFFFFull}, manifest[1].hash);
    const NeuronCore::Welcome expectedWelcome = SampleWelcome();
    Assert::AreEqual(expectedWelcome.composites.size(), decodedWelcome.composites.size());
    const NeuronCore::CompositeComponent& distant = decodedWelcome.composites[3].components[1];
    Assert::AreEqual(std::uint16_t{2}, distant.model);
    Assert::AreEqual(-NeuronCore::MAX_COMPONENT_TRANSLATION, distant.translation.x, L"a translation's sign");
    Assert::AreEqual(NeuronCore::MAX_COMPONENT_TRANSLATION, distant.translation.z);
    Assert::AreEqual(QUARTER_TURN_ABOUT_Y.y, distant.rotation.y);
    Assert::AreEqual(expectedWelcome.sides.size(), decodedWelcome.sides.size());
    Assert::IsTrue(decodedWelcome.sides[1].red == 220 && decodedWelcome.sides[1].green == 80 && decodedWelcome.sides[1].blue == 60,
                   L"a side's color");
    Assert::IsTrue(decodedWelcome.sessionSide == expectedWelcome.sessionSide, L"the session's side");
    Assert::IsTrue(expectedWelcome.payload == decodedWelcome.payload, L"the welcome's payload");
  }

  // §15: one message of each type encodes to the bytes written above, and those bytes decode.
  TEST_METHOD(EncodesTheGoldenBytes)
  {
    Assert::IsTrue(std::ranges::equal(Encoded(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION}), GOLDEN_HELLO), L"Hello");
    Assert::IsTrue(std::ranges::equal(Encoded(GoldenWelcome()), GOLDEN_WELCOME), L"Welcome");
    Assert::IsTrue(std::ranges::equal(Encoded(GoldenSnapshot()), GOLDEN_SNAPSHOT), L"Snapshot");
    Assert::IsTrue(std::ranges::equal(Encoded(NeuronCore::Command{NeuronCore::CommandKind::Detonate, 7}), GOLDEN_COMMAND), L"Command");
    Assert::IsTrue(std::ranges::equal(Encoded(NeuronCore::Command{NeuronCore::CommandKind::Game, 0, {1, 2, 3}}), GOLDEN_GAME_COMMAND),
                   L"the game's command, with its payload");
    for (const std::span<const std::uint8_t> golden :
         {std::span<const std::uint8_t>(GOLDEN_HELLO), std::span<const std::uint8_t>(GOLDEN_WELCOME),
          std::span<const std::uint8_t>(GOLDEN_SNAPSHOT), std::span<const std::uint8_t>(GOLDEN_COMMAND),
          std::span<const std::uint8_t>(GOLDEN_GAME_COMMAND)})
    {
      Assert::IsTrue(NeuronCore::DecodeMessage(golden, COUNTS).has_value(), L"the golden bytes decode");
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
    for (const std::uint16_t version :
         {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{3}, std::uint16_t{4}, std::uint16_t{0xFFFF}})
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
      bytes[SNAPSHOT_FIRST_ENTITY + ENTITY_FLAGS] = 1;
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a reserved entity flag");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU32(bytes, SNAPSHOT_PAYLOAD_BYTES, static_cast<std::uint32_t>(SampleSnapshot().payload.size() + 1));
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a payload beyond the bytes");
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
    for (const std::uint32_t kind : {0u, 6u, 0xFFFFFFFFu})
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
    // Design/ADR/ADR-033: only the game's command carries a payload, it always carries one, and it names no entity.
    {
      Bytes bytes(GOLDEN_GAME_COMMAND.begin(), GOLDEN_GAME_COMMAND.end());
      PutU32(bytes, COMMAND_KIND, static_cast<std::uint32_t>(NeuronCore::CommandKind::Detonate));
      PutU32(bytes, COMMAND_ENTITY, 7);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a detonation with a payload");
    }
    {
      Bytes bytes(GOLDEN_GAME_COMMAND.begin(), GOLDEN_GAME_COMMAND.end());
      PutU32(bytes, COMMAND_ENTITY, 7);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"the game's command naming an entity");
    }
    {
      Bytes bytes = Encoded(NeuronCore::Command{NeuronCore::CommandKind::Game, 0, {}});
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"the game's command without a payload");
    }
    {
      Bytes bytes(GOLDEN_GAME_COMMAND.begin(), GOLDEN_GAME_COMMAND.end());
      PutU32(bytes, COMMAND_PAYLOAD_BYTES, 4);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a payload longer than the message");
      PutU32(bytes, COMMAND_PAYLOAD_BYTES, 2);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"and one shorter");
    }
  }

  // Design/ADR/ADR-029: a welcome's composites and sides that do not add up.
  TEST_METHOD(RefusesMalformedCompositesAndSides)
  {
    const NeuronCore::Welcome sample = SampleWelcome();
    {
      Bytes bytes = Encoded(sample);
      PutU32(bytes, CompositesOffset(sample), static_cast<std::uint32_t>(sample.composites.size() + 1));
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a composite count beyond the composites");
    }
    {
      Bytes bytes = Encoded(sample);
      PutU32(bytes, CompositesOffset(sample), 0xFFFFFFFFu);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a composite count no message could hold");
    }
    {
      Bytes bytes = Encoded(sample);
      PutU32(bytes, ComponentOffset(sample, 3, 0) - 4, 0xFFFFFFFFu);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a component count no message could hold");
    }
    {
      NeuronCore::Welcome welcome = sample;
      welcome.composites[1].components.clear();
      ExpectRefusal(ProtocolError::MalformedMessage, Encoded(welcome), L"a composite of no component");
    }
    {
      Bytes bytes = Encoded(sample);
      PutU16(bytes, ComponentOffset(sample, 3, 2) + COMPONENT_RESERVED, 1);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a component's reserved field");
    }
    for (const std::int32_t translation : {NeuronCore::MAX_COMPONENT_TRANSLATION + 1, -NeuronCore::MAX_COMPONENT_TRANSLATION - 1,
                                           std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()})
    {
      NeuronCore::Welcome welcome = sample;
      welcome.composites[3].components[2].translation.y = translation;
      ExpectRefusal(ProtocolError::MalformedMessage, Encoded(welcome), std::format(L"a component moved {} voxels", translation));
    }
    {
      Bytes bytes = Encoded(sample);
      PutU32(bytes, SidesOffset(sample), static_cast<std::uint32_t>(sample.sides.size() + 1));
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a side count beyond the sides");
    }
    {
      Bytes bytes = Encoded(sample);
      bytes[SidesOffset(sample) + 4 + NeuronCore::SIDE_RECORD_BYTES + SIDE_RESERVED] = 1;
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a side's reserved byte");
    }
    {
      NeuronCore::Welcome welcome = sample;
      welcome.sides.assign(NeuronCore::MAX_SIDES, {1, 2, 3});
      Assert::IsTrue(NeuronCore::DecodeMessage(Encoded(welcome), COUNTS).has_value(), L"as many sides as an entity can name");
      welcome.sides.push_back({1, 2, 3});
      ExpectRefusal(ProtocolError::MalformedMessage, Encoded(welcome), L"more sides than an entity can name");
    }
    {
      Bytes bytes = Encoded(sample);
      PutU32(bytes, PayloadOffset(sample), static_cast<std::uint32_t>(sample.payload.size() + 1));
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a payload beyond the bytes");
      PutU32(bytes, PayloadOffset(sample), static_cast<std::uint32_t>(sample.payload.size() - 1));
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a byte after the payload");
    }
  }

  // Design/ADR/ADR-033: the game's command carries its payload unread, byte for byte.
  TEST_METHOD(CarriesTheGamesCommand)
  {
    const auto decoded = NeuronCore::DecodeMessage(GOLDEN_GAME_COMMAND, COUNTS);
    const auto* command = decoded ? std::get_if<NeuronCore::Command>(&*decoded) : nullptr;
    Assert::IsTrue(command != nullptr, L"it decodes as a command");
    Assert::IsTrue(command->kind == NeuronCore::CommandKind::Game && command->entity == 0, L"the game's, naming no entity");
    Assert::IsTrue(command->payload == Bytes{1, 2, 3}, L"with its payload");
  }

  // Design/ADR/ADR-032: the welcome tells the session its side, an observer's 0, and one of the welcome's own sides or
  // none is refused as BadSide, after every other refusal.
  TEST_METHOD(TellsTheSessionItsSide)
  {
    const NeuronCore::Welcome sample = SampleWelcome();
    for (std::size_t side = 0; side <= sample.sides.size(); ++side)
    {
      NeuronCore::Welcome welcome = sample;
      welcome.sessionSide = static_cast<std::uint8_t>(side);
      const auto decoded = NeuronCore::DecodeMessage(Encoded(welcome), COUNTS);
      Assert::IsTrue(decoded.has_value(), std::format(L"side {}", side).c_str());
      Assert::IsTrue(std::get<NeuronCore::Welcome>(*decoded).sessionSide == side, std::format(L"side {} is read back", side).c_str());
    }
    {
      NeuronCore::Welcome welcome = sample;
      welcome.sessionSide = static_cast<std::uint8_t>(sample.sides.size() + 1);
      ExpectRefusal(ProtocolError::BadSide, Encoded(welcome), L"a side the welcome does not have");
      welcome.sides.clear();
      welcome.sessionSide = 1;
      ExpectRefusal(ProtocolError::BadSide, Encoded(welcome), L"a side of a world of none");
      welcome.composites[0].components[0].model = static_cast<std::uint16_t>(welcome.manifest.size());
      ExpectRefusal(ProtocolError::BadModelIndex, Encoded(welcome), L"a bad model index is judged before the session's side");
    }
    for (std::size_t reserved = 1; reserved < 4; ++reserved)
    {
      Bytes bytes = Encoded(sample);
      bytes[SessionSideOffset(sample) + reserved] = 1;
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, std::format(L"the session side's reserved byte {}", reserved));
    }
  }

  // A component turns by one of the cube's 24 rotations, stored as NVF stores a rotation, and names a model of the
  // manifest; each refusal comes in the order ADR-029 fixes.
  TEST_METHOD(RefusesComponentsThatAreNotPlaced)
  {
    constexpr float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();
    const NeuronCore::Welcome sample = SampleWelcome();
    for (std::size_t field = 0; field < 4; ++field)
    {
      Bytes bytes = Encoded(sample);
      PutF32(bytes, ComponentOffset(sample, 3, 1) + COMPONENT_ROTATION + 4 * field, NOT_A_NUMBER);
      ExpectRefusal(ProtocolError::NotFinite, bytes, std::format(L"a component's rotation, field {}", field));
    }
    {
      NeuronCore::Welcome welcome = sample;
      welcome.composites[3].components[0].rotation = {0.0f, 0.0f, 0.0f, 1.0002f};
      ExpectRefusal(ProtocolError::NotUnitRotation, Encoded(welcome), L"a component's rotation too long");
      welcome.composites[3].components[0].rotation = {0.0f, -0.70710677f, 0.0f, -0.70710677f};
      ExpectRefusal(ProtocolError::NotUnitRotation, Encoded(welcome), L"a component's rotation with w below 0");
    }
    {
      // An eighth of a turn about y, and a turn of 37 degrees off every axis: unit, and not the cube's.
      NeuronCore::Welcome welcome = sample;
      welcome.composites[0].components[0].rotation = {0.0f, 0.38268343f, 0.0f, 0.92387953f};
      ExpectRefusal(ProtocolError::NotCubeRotation, Encoded(welcome), L"an eighth of a turn");
      welcome.composites[0].components[0].rotation = NeuronCore::QuaternionOf(NeuronCore::RotationOf({0.21f, -0.37f, 0.12f, 0.896f}));
      ExpectRefusal(ProtocolError::NotCubeRotation, Encoded(welcome), L"a turn off the axes");
      welcome.composites[2].components.clear();
      ExpectRefusal(ProtocolError::NotCubeRotation, Encoded(welcome), L"before a composite of no component");
    }
    {
      NeuronCore::Welcome welcome = sample;
      welcome.composites[3].components[3].model = static_cast<std::uint16_t>(sample.manifest.size());
      ExpectRefusal(ProtocolError::BadModelIndex, Encoded(welcome), L"a component naming a model past the manifest");
      welcome.manifest.back().name = "Frigate_2";
      ExpectRefusal(ProtocolError::BadName, Encoded(welcome), L"a bad name before a bad model index");
    }
    {
      // Every one of the cube's 24 rotations, as the NVF importer's table spells it, is one.
      NeuronCore::Welcome welcome = sample;
      welcome.composites[3].components.clear();
      for (const CubeSymmetry& symmetry : CubeSymmetries())
      {
        if (symmetry.proper)
        {
          const std::optional<NeuronCore::Quaternion> quaternion = NeuronCore::CubeRotationQuaternion(symmetry.rotation);
          Assert::IsTrue(quaternion.has_value(), L"the importer's table holds it");
          welcome.composites[3].components.push_back({0, {0, 0, 0}, quaternion.value_or(NeuronCore::Quaternion{})});
        }
      }
      Assert::AreEqual(std::size_t{24}, welcome.composites[3].components.size(), L"the cube's rotations");
      Assert::IsTrue(NeuronCore::DecodeMessage(Encoded(welcome), COUNTS).has_value(), L"every one is accepted");
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
    Assert::IsTrue(NeuronCore::DecodeMessage(Encoded(longest), COUNTS).has_value(), L"a name of the longest length");
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

  // NVF's rule: unit length within 1e-4, and w >= 0 (Design/Archive/NeuronVoxelFormat.md).
  TEST_METHOD(RefusesRotationsThatAreNotUnit)
  {
    {
      NeuronCore::Snapshot snapshot = SampleSnapshot();
      snapshot.entities[0].rotation = {0.0f, 0.0f, 0.0f, 1.0002f};
      ExpectRefusal(ProtocolError::NotUnitRotation, Encoded(snapshot), L"a rotation too long");
      snapshot.entities[0].rotation = {0.0f, 0.0f, 0.0f, 0.9998f};
      ExpectRefusal(ProtocolError::NotUnitRotation, Encoded(snapshot), L"a rotation too short");
      snapshot.entities[0].rotation = {0.0f, 0.0f, 0.0f, 1.00009f};
      Assert::IsTrue(NeuronCore::DecodeMessage(Encoded(snapshot), COUNTS).has_value(), L"a rotation within the tolerance");
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
      PutU16(bytes, SNAPSHOT_FIRST_ENTITY + ENTITY_COMPOSITE, static_cast<std::uint16_t>(COUNTS.composites));
      ExpectRefusal(ProtocolError::BadCompositeIndex, bytes, L"a composite past the welcome's");
      ExpectRefusal(ProtocolError::BadCompositeIndex, SnapshotBytes(), L"a snapshot before any welcome", {0, 0});
    }
    {
      // Side 0 is none, and side n the welcome's nth.
      Bytes bytes = SnapshotBytes();
      bytes[SNAPSHOT_FIRST_ENTITY + ENTITY_SIDE] = static_cast<std::uint8_t>(COUNTS.sides);
      Assert::IsTrue(NeuronCore::DecodeMessage(bytes, COUNTS).has_value(), L"the welcome's last side");
      bytes[SNAPSHOT_FIRST_ENTITY + ENTITY_SIDE] = static_cast<std::uint8_t>(COUNTS.sides + 1);
      ExpectRefusal(ProtocolError::BadSide, bytes, L"a side past the welcome's");
      ExpectRefusal(ProtocolError::BadSide, SnapshotBytes(), L"a side of a welcome without sides", {COUNTS.composites, 0});
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

  // Design/ADR/ADR-035: an entity has at most one mask, of its whole composite's voxels, with a voxel gone and the last
  // byte's spare bits clear, so that each state has one encoding; and the masks fill the bytes the snapshot gives them.
  TEST_METHOD(RefusesMasksThatDoNotAddUp)
  {
    {
      NeuronCore::Snapshot snapshot = SampleSnapshot();
      snapshot.masks[1].entity = 4;
      ExpectRefusal(ProtocolError::UnknownEntity, Encoded(snapshot), L"a mask of an entity not in the snapshot");
    }
    {
      NeuronCore::Snapshot snapshot = SampleSnapshot();
      snapshot.masks.push_back(snapshot.masks.front());
      ExpectRefusal(ProtocolError::MalformedMessage, Encoded(snapshot), L"two masks of one entity");
    }
    {
      NeuronCore::Snapshot snapshot = SampleSnapshot();
      snapshot.masks[0] = {0xFFFFFFFFu, 0, {}};
      ExpectRefusal(ProtocolError::MalformedMessage, Encoded(snapshot), L"a mask of no voxel");
    }
    {
      NeuronCore::Snapshot snapshot = SampleSnapshot();
      snapshot.masks[1].gone = {0x00, 0x00, 0x00};
      ExpectRefusal(ProtocolError::MalformedMessage, Encoded(snapshot), L"a mask with no voxel gone");
    }
    {
      NeuronCore::Snapshot snapshot = SampleSnapshot();
      snapshot.masks[1].gone.back() = 0x02;
      ExpectRefusal(ProtocolError::MalformedMessage, Encoded(snapshot), L"a bit past the voxel count");
      snapshot.masks[1].voxelCount = 18;
      Assert::IsTrue(NeuronCore::DecodeMessage(Encoded(snapshot), COUNTS).has_value(), L"which is a voxel of a longer mask");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU32(bytes, SNAPSHOT_MASK_COUNT, static_cast<std::uint32_t>(SampleSnapshot().masks.size() + 1));
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a mask count beyond the masks");
      PutU32(bytes, SNAPSHOT_MASK_COUNT, 0xFFFFFFFFu);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a mask count no message could hold");
      PutU32(bytes, SNAPSHOT_MASK_COUNT, static_cast<std::uint32_t>(SampleSnapshot().masks.size() - 1));
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a mask count short of the masks");
    }
    {
      Bytes bytes = SnapshotBytes();
      PutU32(bytes, SNAPSHOT_MASK_BYTES, GetU32(bytes, SNAPSHOT_MASK_BYTES) + 1);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"mask bytes beyond the message");
    }
    {
      // The first mask's count moved by a byte's worth of voxels: its bits run into the next mask, or fall short of it.
      Bytes bytes = SnapshotBytes();
      PutU32(bytes, FirstMask() + MASK_VOXEL_COUNT, 9);
      ExpectRefusal(ProtocolError::MalformedMessage, bytes, L"a voxel count that takes the next mask's bytes");
      Bytes golden(GOLDEN_SNAPSHOT.begin(), GOLDEN_SNAPSHOT.end());
      const std::size_t goldenMask = GOLDEN_SNAPSHOT.size() - GoldenSnapshot().payload.size() - MASK_HEADER_BYTES - 2;
      PutU32(golden, goldenMask + MASK_VOXEL_COUNT, 17);
      ExpectRefusal(ProtocolError::MalformedMessage, golden, L"a voxel count beyond the mask bytes");
      PutU32(golden, goldenMask + MASK_VOXEL_COUNT, 8);
      ExpectRefusal(ProtocolError::MalformedMessage, golden, L"a voxel count short of the mask bytes");
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
