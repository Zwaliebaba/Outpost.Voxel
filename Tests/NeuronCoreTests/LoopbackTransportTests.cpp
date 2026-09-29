#include "pch.h"

#include "SeededRandom.h"

#include "LoopbackTransport.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using Bytes = std::vector<std::uint8_t>;

// A message that says which it is: its sequence number, little-endian, then as many bytes as the number modulo 50.
[[nodiscard]] Bytes Numbered(std::uint32_t _number)
{
  Bytes message{static_cast<std::uint8_t>(_number), static_cast<std::uint8_t>(_number >> 8), static_cast<std::uint8_t>(_number >> 16),
                static_cast<std::uint8_t>(_number >> 24)};
  message.resize(4 + _number % 50, static_cast<std::uint8_t>(_number * 7));
  return message;
}

// Receives _count messages from _transport, yielding while none waits, and checks that each is the next number.
void ReceiveNumbered(NeuronCore::Transport& _transport, std::uint32_t _count, bool& _inOrder)
{
  for (std::uint32_t expected = 0; expected < _count;)
  {
    const std::optional<Bytes> message = _transport.Receive();
    if (!message)
    {
      std::this_thread::yield();
      continue;
    }
    _inOrder = _inOrder && *message == Numbered(expected);
    ++expected;
  }
}

} // namespace

// Design/Archive/SpaceScene.md §6.1, §6.3 and §15: the loopback delivers every message, whole and in order, both ways; closing
// either end closes both; and two threads can use its ends at once.
TEST_CLASS(LoopbackTransportTests)
{
public:
  TEST_METHOD(DeliversInOrderBothWays)
  {
    const NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
    Assert::IsFalse(pair.server->Receive().has_value(), L"nothing waits before anything is sent");
    for (std::uint32_t i = 0; i < 100; ++i)
    {
      Assert::IsTrue(pair.client->Send(Numbered(i)));
      Assert::IsTrue(pair.server->Send(Numbered(1000 + i)));
    }
    for (std::uint32_t i = 0; i < 100; ++i)
    {
      Assert::IsTrue(pair.server->Receive() == Numbered(i), std::format(L"message {} to the server", i).c_str());
      Assert::IsTrue(pair.client->Receive() == Numbered(1000 + i), std::format(L"message {} to the client", i).c_str());
    }
    Assert::IsFalse(pair.server->Receive().has_value(), L"and nothing more");
    Assert::IsFalse(pair.client->Receive().has_value(), L"either way");
  }

  // A large message, and an empty one, arrive byte for byte.
  TEST_METHOD(DeliversEveryByte)
  {
    SeededRandom random(20261016u);
    Bytes large(1u << 20);
    for (std::uint8_t& byte : large)
    {
      byte = static_cast<std::uint8_t>(random.Below(256));
    }
    const NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
    Assert::IsTrue(pair.client->Send(large));
    Assert::IsTrue(pair.client->Send(Bytes{}));
    Assert::IsTrue(pair.server->Receive() == large, L"a megabyte");
    Assert::IsTrue(pair.server->Receive() == Bytes{}, L"nothing at all");
  }

  // Closing one end closes both: neither sends, and what was sent before the close is still received.
  TEST_METHOD(ClosingEndsBothDirections)
  {
    const NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
    Assert::IsTrue(pair.client->IsOpen() && pair.server->IsOpen());
    for (std::uint32_t i = 0; i < 3; ++i)
    {
      Assert::IsTrue(pair.client->Send(Numbered(i)));
    }
    pair.client->Close();
    Assert::IsFalse(pair.client->IsOpen(), L"the end that closed");
    Assert::IsFalse(pair.server->IsOpen(), L"the other end");
    Assert::IsFalse(pair.client->Send(Numbered(3)), L"the closed end sends nothing");
    Assert::IsFalse(pair.server->Send(Numbered(4)), L"nor does the other");
    for (std::uint32_t i = 0; i < 3; ++i)
    {
      Assert::IsTrue(pair.server->Receive() == Numbered(i), std::format(L"message {}, sent before the close", i).c_str());
    }
    Assert::IsFalse(pair.server->Receive().has_value(), L"then nothing");
    Assert::IsFalse(pair.client->Receive().has_value(), L"nothing came back");
  }

  TEST_METHOD(DestroyingAnEndClosesTheOther)
  {
    NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
    Assert::IsTrue(pair.client->Send(Numbered(9)));
    pair.client.reset();
    Assert::IsFalse(pair.server->IsOpen());
    Assert::IsTrue(pair.server->Receive() == Numbered(9), L"what was sent before still arrives");
  }

  // Two threads, one at each end, each sending and receiving at once. Nothing is timed: each only waits for the number
  // of messages it expects.
  TEST_METHOD(TwoThreadsTalkAtOnce)
  {
    constexpr std::uint32_t COUNT = 20000;
    const NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
    bool serverInOrder = true;
    bool clientSent = true;
    bool clientInOrder = true;
    {
      std::jthread server(
        [&pair, &serverInOrder]
        {
          for (std::uint32_t i = 0; i < COUNT; ++i)
          {
            static_cast<void>(pair.server->Send(Numbered(i)));
          }
          ReceiveNumbered(*pair.server, COUNT, serverInOrder);
        });
      // Nothing here may throw while the other thread runs, or its join would wait on messages never sent.
      for (std::uint32_t i = 0; i < COUNT; ++i)
      {
        clientSent = pair.client->Send(Numbered(i)) && clientSent;
      }
      ReceiveNumbered(*pair.client, COUNT, clientInOrder);
    }
    Assert::IsTrue(clientSent, L"the client sent every message");
    Assert::IsTrue(clientInOrder, L"the client received the server's messages in order");
    Assert::IsTrue(serverInOrder, L"the server received the client's messages in order");
    Assert::IsFalse(pair.client->Receive().has_value() || pair.server->Receive().has_value(), L"and no more than were sent");
  }
};

} // namespace NeuronCoreTests
