#include "pch.h"

#include "Orders.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{
namespace
{

using Bytes = std::vector<std::uint8_t>;

// Where the sample payload below puts the fields the refusals change: its first state's, then its shell's and its beam's.
constexpr std::size_t FIRST_STATE_SHIP = 3;
constexpr std::size_t FIRST_STATE_STATE = 7;
constexpr std::size_t FIRST_STATE_RESERVED = 9;
constexpr std::size_t FIRST_STATE_TARGET = 11;
constexpr std::size_t SHELL_COUNT = 43;
constexpr std::size_t SHELL_VELOCITY = 57;
constexpr std::size_t SHELL_SIDE = 69;
constexpr std::size_t SHELL_RESERVED = 71;
constexpr std::size_t BEAM_TO = 87;
constexpr std::size_t BEAM_SIDE = 99;

// The name of the refusal of an order's _bytes, or "none" for one that decodes.
[[nodiscard]] std::string OrderRefusalOf(const Bytes& _bytes)
{
  const auto order = GameCore::DecodeOrder(_bytes);
  return order ? std::string("none") : std::string(GameCore::OrderErrorName(order.error()));
}

[[nodiscard]] std::string PayloadRefusalOf(const Bytes& _bytes)
{
  const auto payload = GameCore::DecodeSnapshotPayload(_bytes);
  return payload ? std::string("none") : std::string(GameCore::OrderErrorName(payload.error()));
}

// _bytes with the byte at _offset set to _value.
[[nodiscard]] Bytes With(Bytes _bytes, std::size_t _offset, std::uint8_t _value)
{
  _bytes[_offset] = _value;
  return _bytes;
}

// _bytes with the four bytes at _offset set to a NaN.
[[nodiscard]] Bytes WithNan(Bytes _bytes, std::size_t _offset)
{
  constexpr std::uint32_t NAN_BITS = 0x7FC00000u;
  for (std::size_t i = 0; i < 4; ++i)
  {
    _bytes[_offset + i] = static_cast<std::uint8_t>(NAN_BITS >> (8u * i));
  }
  return _bytes;
}

// A payload of two order states, a move and an attack, a shell and a beam.
[[nodiscard]] GameCore::SnapshotPayload SamplePayload()
{
  return {{{4, GameCore::ShipState::Moving, 100.5f, -3.0f}, {6, GameCore::ShipState::Attacking, 0.0f, 0.0f, 12}},
          {{{1.0f, 2.0f, 3.0f}, {300.0f, 0.0f, -0.5f}, 2}},
          {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 250.0f}, 1}}};
}

} // namespace

// Design/ADR/ADR-033 and Design/ADR/ADR-035: an order as the client sends it in a command's payload, and what a side's
// snapshots carry back in theirs, spelled as the ADRs spell them and refused by name.
TEST_CLASS(OrdersTests)
{
public:
  TEST_METHOD(SpellsOrdersAsTheAdrDoes)
  {
    const Bytes move{2, 1, 2,    0,                // version 2, a move, two ships
                     5, 0, 0,    0,    2, 0, 0, 0, // ships 5 and 2, in the order given
                     0, 0, 0xC9, 0x42,             // x 100.5
                     0, 0, 0x40, 0xC0};            // z -3
    Assert::IsTrue(GameCore::EncodeOrder({GameCore::OrderKind::Move, {5, 2}, 100.5f, -3.0f}) == move, L"a move");
    const Bytes stop{2, 2, 1, 0, 9, 0, 0, 0}; // version 2, a stop, one ship: 9; no target
    Assert::IsTrue(GameCore::EncodeOrder({GameCore::OrderKind::Stop, {9}, 0.0f, 0.0f}) == stop, L"a stop");
    const Bytes attack{2, 4, 1, 0, 9, 0, 0, 0, 12, 0, 0, 0}; // version 2, an attack, ship 9, on entity 12
    Assert::IsTrue(GameCore::EncodeOrder({GameCore::OrderKind::Attack, {9}, 0.0f, 0.0f, 12}) == attack, L"an attack");
    const Bytes attackMove{2, 5, 1, 0, 9, 0, 0, 0, 0, 0, 0, 0xC1, 0, 0, 0x80, 0x3E}; // an attack-move of ship 9 to (-8, 0.25)
    Assert::IsTrue(GameCore::EncodeOrder({GameCore::OrderKind::AttackMove, {9}, -8.0f, 0.25f}) == attackMove, L"an attack-move");

    const Bytes payload{2, 2, 0,                                                              // version 2, two states
                        4, 0, 0,    0,    1, 0, 0, 0,    0,  0, 0,    0,    0, 0, 0xC9, 0x42, // ship 4 moving, no target,
                        0, 0, 0x40, 0xC0,                                                     // to (100.5, -3)
                        6, 0, 0,    0,    3, 0, 0, 0,    12, 0, 0,    0,    0, 0, 0,    0,    // ship 6 attacking 12
                        0, 0, 0,    0,                                                        //
                        1, 0,                                                                 // one shell
                        0, 0, 0x80, 0x3F, 0, 0, 0, 0x40, 0,  0, 0x40, 0x40,                   // at (1, 2, 3)
                        0, 0, 0x96, 0x43, 0, 0, 0, 0,    0,  0, 0,    0xBF, 2, 0, 0,    0,    // at (300, 0, -0.5), side 2
                        1, 0,                                                                 // one beam
                        0, 0, 0,    0,    0, 0, 0, 0,    0,  0, 0,    0,                      // from (0, 0, 0)
                        0, 0, 0,    0,    0, 0, 0, 0,    0,  0, 0x7A, 0x43, 1, 0, 0,    0};   // to (0, 0, 250), side 1
    Assert::IsTrue(GameCore::EncodeSnapshotPayload(SamplePayload()) == payload, L"a snapshot's payload");
  }

  TEST_METHOD(DecodesWhatWasEncoded)
  {
    for (const GameCore::OrderKind kind : {GameCore::OrderKind::Move, GameCore::OrderKind::Stop, GameCore::OrderKind::Hold,
                                           GameCore::OrderKind::Attack, GameCore::OrderKind::AttackMove})
    {
      const bool point = kind == GameCore::OrderKind::Move || kind == GameCore::OrderKind::AttackMove;
      const float target = point ? -1234.25f : 0.0f;
      const GameCore::Order order{kind, {3, 1, 2}, target, 2.0f * target, kind == GameCore::OrderKind::Attack ? 0xFFFFFFFFu : 0u};
      const auto decoded = GameCore::DecodeOrder(GameCore::EncodeOrder(order));
      Assert::IsTrue(decoded.has_value(), L"it decodes");
      Assert::IsTrue(decoded->kind == kind && decoded->ships == order.ships && decoded->targetX == order.targetX &&
                       decoded->targetZ == order.targetZ && decoded->target == order.target,
                     L"as it was");
    }
    const auto none = GameCore::DecodeSnapshotPayload(GameCore::EncodeSnapshotPayload({}));
    Assert::IsTrue(none.has_value() && none->orders.empty() && none->shells.empty() && none->beams.empty(), L"nothing to say");

    const GameCore::SnapshotPayload sample = SamplePayload();
    const auto decoded = GameCore::DecodeSnapshotPayload(GameCore::EncodeSnapshotPayload(sample));
    Assert::IsTrue(decoded.has_value(), L"the sample decodes");
    Assert::AreEqual(sample.orders.size(), decoded->orders.size());
    Assert::IsTrue(decoded->orders[1].state == GameCore::ShipState::Attacking && decoded->orders[1].target == 12, L"the attack");
    Assert::AreEqual(sample.orders[0].destinationX, decoded->orders[0].destinationX, L"the move's destination");
    Assert::AreEqual(std::size_t{1}, decoded->shells.size());
    Assert::AreEqual(sample.shells[0].velocity.z, decoded->shells[0].velocity.z, L"the shell's velocity");
    Assert::IsTrue(decoded->shells[0].side == 2, L"the shell's side");
    Assert::AreEqual(std::size_t{1}, decoded->beams.size());
    Assert::AreEqual(sample.beams[0].to.z, decoded->beams[0].to.z, L"the beam's end");
    Assert::IsTrue(decoded->beams[0].side == 1, L"the beam's side");
  }

  TEST_METHOD(RefusesOrdersByName)
  {
    const Bytes move = GameCore::EncodeOrder({GameCore::OrderKind::Move, {5, 2}, 100.5f, -3.0f});
    const Bytes attack = GameCore::EncodeOrder({GameCore::OrderKind::Attack, {5, 2}, 0.0f, 0.0f, 7});
    Bytes tooMany{2, 2, 1, 1}; // 257 ships
    for (std::uint32_t ship = 1; ship <= GameCore::MAX_ORDER_SHIPS + 1; ++ship)
    {
      tooMany.insert(tooMany.end(), {static_cast<std::uint8_t>(ship), static_cast<std::uint8_t>(ship >> 8u), 0, 0});
    }
    Bytes stopWithTarget = GameCore::EncodeOrder({GameCore::OrderKind::Stop, {9}, 0.0f, 0.0f});
    stopWithTarget.insert(stopWithTarget.end(), 8, std::uint8_t{0});

    struct Case
    {
      const wchar_t* what;
      Bytes bytes;
      const char* expected;
    };
    const std::vector<Case> cases{{L"nothing", {}, "Truncated"},
                                  {L"a header cut short", {2, 1, 2}, "Truncated"},
                                  {L"version 1", With(move, 0, 1), "UnsupportedVersion"},
                                  {L"another version", With(move, 0, 3), "UnsupportedVersion"},
                                  {L"no kind", With(move, 1, 0), "MalformedOrder"},
                                  {L"a kind past the last", With(move, 1, 6), "MalformedOrder"},
                                  {L"no ship", {2, 2, 0, 0}, "MalformedOrder"},
                                  {L"more ships than an order names", tooMany, "MalformedOrder"},
                                  {L"ship 0", With(move, 4, 0), "MalformedOrder"},
                                  {L"a ship twice", With(move, 8, 5), "MalformedOrder"},
                                  {L"a target that is not finite", WithNan(move, 12), "MalformedOrder"},
                                  {L"a byte to spare",
                                   [&move]
                                   {
                                     Bytes more = move;
                                     more.push_back(0);
                                     return more;
                                   }(),
                                   "MalformedOrder"},
                                  {L"a stop with a target", stopWithTarget, "MalformedOrder"},
                                  {L"ids cut short", Bytes(move.begin(), move.begin() + 9), "Truncated"},
                                  {L"a target cut short", Bytes(move.begin(), move.end() - 1), "Truncated"},
                                  {L"an attack on nothing", With(attack, 12, 0), "MalformedOrder"},
                                  {L"an attack on one of its own ships", With(attack, 12, 2), "MalformedOrder"},
                                  {L"an attack cut short", Bytes(attack.begin(), attack.end() - 1), "Truncated"}};
    for (const Case& refused : cases)
    {
      Assert::AreEqual(std::string(refused.expected), OrderRefusalOf(refused.bytes), refused.what);
    }
    Assert::AreEqual(std::string("none"), OrderRefusalOf(move), L"the move the others corrupt");
    Assert::AreEqual(std::string("none"), OrderRefusalOf(attack), L"and the attack");
  }

  TEST_METHOD(RefusesPayloadsByName)
  {
    const Bytes payload = GameCore::EncodeSnapshotPayload(SamplePayload());
    struct Case
    {
      const wchar_t* what;
      Bytes bytes;
      const char* expected;
    };
    const std::vector<Case> cases{{L"nothing", {}, "Truncated"},
                                  {L"version 1", With(payload, 0, 1), "UnsupportedVersion"},
                                  {L"ship 0", With(payload, FIRST_STATE_SHIP, 0), "MalformedOrder"},
                                  {L"no state", With(payload, FIRST_STATE_STATE, 0), "MalformedOrder"},
                                  {L"a state past the last", With(payload, FIRST_STATE_STATE, 5), "MalformedOrder"},
                                  {L"a reserved byte set", With(payload, FIRST_STATE_RESERVED, 1), "MalformedOrder"},
                                  {L"a move with a target", With(payload, FIRST_STATE_TARGET, 1), "MalformedOrder"},
                                  {L"an attack without one", With(payload, FIRST_STATE_STATE, 3), "MalformedOrder"},
                                  {L"more states than bytes", With(payload, 2, 1), "Truncated"},
                                  {L"more shells than bytes", With(payload, SHELL_COUNT + 1, 1), "Truncated"},
                                  {L"a shell of no side", With(payload, SHELL_SIDE, 0), "MalformedOrder"},
                                  {L"a shell's reserved byte set", With(payload, SHELL_RESERVED, 1), "MalformedOrder"},
                                  {L"a shell's velocity that is not finite", WithNan(payload, SHELL_VELOCITY), "MalformedOrder"},
                                  {L"a beam of no side", With(payload, BEAM_SIDE, 0), "MalformedOrder"},
                                  {L"a beam's end that is not finite", WithNan(payload, BEAM_TO), "MalformedOrder"},
                                  {L"a beam cut short", Bytes(payload.begin(), payload.end() - 1), "Truncated"},
                                  {L"a byte to spare",
                                   [&payload]
                                   {
                                     Bytes more = payload;
                                     more.push_back(0);
                                     return more;
                                   }(),
                                   "MalformedOrder"}};
    for (const Case& refused : cases)
    {
      Assert::AreEqual(std::string(refused.expected), PayloadRefusalOf(refused.bytes), refused.what);
    }
    Assert::AreEqual(std::string("none"), PayloadRefusalOf(payload), L"the payload the others corrupt");
  }

  // The encoders write nothing their decoders would refuse.
  TEST_METHOD(EncodesOnlyWhatDecodes)
  {
    const auto throws = [](const GameCore::Order& _order)
    {
      try
      {
        static_cast<void>(GameCore::EncodeOrder(_order));
      }
      catch (const std::invalid_argument&)
      {
        return true;
      }
      return false;
    };
    Assert::IsTrue(throws({GameCore::OrderKind::Move, {}, 0.0f, 0.0f}), L"no ship");
    Assert::IsTrue(throws({GameCore::OrderKind::Move, {1, 1}, 0.0f, 0.0f}), L"a ship twice");
    Assert::IsTrue(throws({GameCore::OrderKind::Move, {0}, 0.0f, 0.0f}), L"ship 0");
    Assert::IsTrue(throws({GameCore::OrderKind::Move, {1}, std::numeric_limits<float>::infinity(), 0.0f}), L"a target at infinity");
    Assert::IsTrue(throws({GameCore::OrderKind::AttackMove, {1}, 0.0f, std::numeric_limits<float>::quiet_NaN()}),
                   L"an attack-move to nowhere");
    Assert::IsTrue(throws({static_cast<GameCore::OrderKind>(9), {1}, 0.0f, 0.0f}), L"a kind the game does not have");
    Assert::IsTrue(throws({GameCore::OrderKind::Attack, {1}, 0.0f, 0.0f, 0}), L"an attack on nothing");
    Assert::IsTrue(throws({GameCore::OrderKind::Attack, {1, 2}, 0.0f, 0.0f, 2}), L"an attack on its own ship");
    Assert::IsFalse(throws({GameCore::OrderKind::Hold, {1}, std::numeric_limits<float>::quiet_NaN(), 0.0f}),
                    L"a hold's target is not written, so it is not held to anything");

    const auto payloadThrows = [](const GameCore::SnapshotPayload& _payload)
    {
      try
      {
        static_cast<void>(GameCore::EncodeSnapshotPayload(_payload));
      }
      catch (const std::invalid_argument&)
      {
        return true;
      }
      return false;
    };
    Assert::IsTrue(payloadThrows({{{1, GameCore::ShipState::Attacking, 0.0f, 0.0f, 0}}, {}, {}}), L"an attack on nothing");
    Assert::IsTrue(payloadThrows({{}, {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0}}, {}}), L"a shell of no side");
    Assert::IsTrue(payloadThrows({{}, {}, {{{0.0f, 0.0f, 0.0f}, {std::numeric_limits<float>::infinity(), 0.0f, 0.0f}, 1}}}),
                   L"a beam to infinity");
    Assert::IsTrue(payloadThrows({{}, std::vector<GameCore::ShellState>(0x10000, {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 1}), {}}),
                   L"more shells than a u16 counts");
  }
};

} // namespace GameCoreTests
