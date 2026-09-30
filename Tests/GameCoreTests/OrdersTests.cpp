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

// The name of the refusal of an order's _bytes, or "none" for one that decodes.
[[nodiscard]] std::string OrderRefusalOf(const Bytes& _bytes)
{
  const auto order = GameCore::DecodeOrder(_bytes);
  return order ? std::string("none") : std::string(GameCore::OrderErrorName(order.error()));
}

[[nodiscard]] std::string StatesRefusalOf(const Bytes& _bytes)
{
  const auto states = GameCore::DecodeOrderStates(_bytes);
  return states ? std::string("none") : std::string(GameCore::OrderErrorName(states.error()));
}

// _bytes with the byte at _offset set to _value.
[[nodiscard]] Bytes With(Bytes _bytes, std::size_t _offset, std::uint8_t _value)
{
  _bytes[_offset] = _value;
  return _bytes;
}

} // namespace

// Design/ADR/ADR-033: an order as the client sends it in a command's payload, and the order states a side's snapshots
// carry back, spelled as the ADR spells them and refused by name.
TEST_CLASS(OrdersTests)
{
public:
  TEST_METHOD(SpellsOrdersAsTheAdrDoes)
  {
    const Bytes move{1, 1, 2,    0,                // version 1, a move, two ships
                     5, 0, 0,    0,    2, 0, 0, 0, // ships 5 and 2, in the order given
                     0, 0, 0xC9, 0x42,             // x 100.5
                     0, 0, 0x40, 0xC0};            // z -3
    Assert::IsTrue(GameCore::EncodeOrder({GameCore::OrderKind::Move, {5, 2}, 100.5f, -3.0f}) == move, L"a move");
    const Bytes stop{1, 2, 1, 0, 9, 0, 0, 0}; // version 1, a stop, one ship: 9; no target
    Assert::IsTrue(GameCore::EncodeOrder({GameCore::OrderKind::Stop, {9}, 0.0f, 0.0f}) == stop, L"a stop");

    const Bytes states{1, 2, 0,                                                    // version 1, two states
                       4, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0xC9, 0x42, 0, 0, 0x40, 0xC0, // ship 4 moving to (100.5, -3)
                       6, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0,    0,    0, 0, 0,    0};   // ship 6 holding
    Assert::IsTrue(GameCore::EncodeOrderStates(std::vector<GameCore::ShipOrderState>{
                     {4, GameCore::ShipState::Moving, 100.5f, -3.0f}, {6, GameCore::ShipState::Holding, 0.0f, 0.0f}}) == states,
                   L"the order states");
  }

  TEST_METHOD(DecodesWhatWasEncoded)
  {
    for (const GameCore::OrderKind kind : {GameCore::OrderKind::Move, GameCore::OrderKind::Stop, GameCore::OrderKind::Hold})
    {
      const float target = kind == GameCore::OrderKind::Move ? -1234.25f : 0.0f;
      const GameCore::Order order{kind, {3, 1, 2}, target, 2.0f * target};
      const auto decoded = GameCore::DecodeOrder(GameCore::EncodeOrder(order));
      Assert::IsTrue(decoded.has_value(), L"it decodes");
      Assert::IsTrue(decoded->kind == kind && decoded->ships == order.ships && decoded->targetX == order.targetX &&
                       decoded->targetZ == order.targetZ,
                     L"as it was");
    }
    const auto none = GameCore::DecodeOrderStates(GameCore::EncodeOrderStates({}));
    Assert::IsTrue(none.has_value() && none->empty(), L"no ship moving or holding");
  }

  TEST_METHOD(RefusesOrdersByName)
  {
    const Bytes move = GameCore::EncodeOrder({GameCore::OrderKind::Move, {5, 2}, 100.5f, -3.0f});
    Bytes nan = move;
    const std::uint32_t nanBits = 0x7FC00000u;
    for (std::size_t i = 0; i < 4; ++i)
    {
      nan[12 + i] = static_cast<std::uint8_t>(nanBits >> (8u * i));
    }
    Bytes tooMany{1, 2, 1, 1}; // 257 ships
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
                                  {L"a header cut short", {1, 1, 2}, "Truncated"},
                                  {L"another version", With(move, 0, 2), "UnsupportedVersion"},
                                  {L"no kind", With(move, 1, 0), "MalformedOrder"},
                                  {L"a kind past the last", With(move, 1, 4), "MalformedOrder"},
                                  {L"no ship", {1, 2, 0, 0}, "MalformedOrder"},
                                  {L"more ships than an order names", tooMany, "MalformedOrder"},
                                  {L"ship 0", With(move, 4, 0), "MalformedOrder"},
                                  {L"a ship twice", With(move, 8, 5), "MalformedOrder"},
                                  {L"a target that is not finite", nan, "MalformedOrder"},
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
                                  {L"a target cut short", Bytes(move.begin(), move.end() - 1), "Truncated"}};
    for (const Case& refused : cases)
    {
      Assert::AreEqual(std::string(refused.expected), OrderRefusalOf(refused.bytes), refused.what);
    }
    Assert::AreEqual(std::string("none"), OrderRefusalOf(move), L"the order the others corrupt");
  }

  TEST_METHOD(RefusesStatesByName)
  {
    const Bytes states =
      GameCore::EncodeOrderStates(std::vector<GameCore::ShipOrderState>{{4, GameCore::ShipState::Moving, 100.5f, -3.0f}});
    struct Case
    {
      const wchar_t* what;
      Bytes bytes;
      const char* expected;
    };
    const std::vector<Case> cases{{L"nothing", {}, "Truncated"},
                                  {L"another version", With(states, 0, 2), "UnsupportedVersion"},
                                  {L"ship 0", With(states, 3, 0), "MalformedOrder"},
                                  {L"no state", With(states, 7, 0), "MalformedOrder"},
                                  {L"a state past the last", With(states, 7, 3), "MalformedOrder"},
                                  {L"a reserved byte set", With(states, 9, 1), "MalformedOrder"},
                                  {L"more states than bytes", With(states, 1, 2), "Truncated"},
                                  {L"a state cut short", Bytes(states.begin(), states.end() - 1), "Truncated"},
                                  {L"a byte to spare",
                                   [&states]
                                   {
                                     Bytes more = states;
                                     more.push_back(0);
                                     return more;
                                   }(),
                                   "MalformedOrder"}};
    for (const Case& refused : cases)
    {
      Assert::AreEqual(std::string(refused.expected), StatesRefusalOf(refused.bytes), refused.what);
    }
    Assert::AreEqual(std::string("none"), StatesRefusalOf(states), L"the states the others corrupt");
  }

  // The encoder writes nothing its decoder would refuse.
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
    Assert::IsTrue(throws({static_cast<GameCore::OrderKind>(9), {1}, 0.0f, 0.0f}), L"a kind the game does not have");
    Assert::IsFalse(throws({GameCore::OrderKind::Hold, {1}, std::numeric_limits<float>::quiet_NaN(), 0.0f}),
                    L"a hold's target is not written, so it is not held to anything");
  }
};

} // namespace GameCoreTests
