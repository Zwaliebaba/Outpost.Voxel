#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace NeuronCore
{

// A pipe of whole messages between a client and a server (Design/SpaceScene.md §6.1, Design/ADR/ADR-015): what crosses
// is bytes, never an object. Send takes one message's bytes, and Receive returns the next whole message, or nothing
// when none has arrived. Each end belongs to one thread at a time.
class Transport
{
public:
  Transport() = default;
  virtual ~Transport() = default;
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;
  Transport(Transport&&) = delete;
  Transport& operator=(Transport&&) = delete;

  // Sends a copy of _message. False once the transport is closed, from either end, and then nothing is sent.
  [[nodiscard]] virtual bool Send(std::span<const std::uint8_t> _message) = 0;

  // The next message, whole, or nothing when none is waiting. What was sent before a close can still be received.
  [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> Receive() = 0;

  // Closes both directions; the other end sees it too.
  virtual void Close() noexcept = 0;

  [[nodiscard]] virtual bool IsOpen() const noexcept = 0;
};

} // namespace NeuronCore
