#pragma once

#include "Transport.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace NeuronCore
{

struct LoopbackLink;

// One end of a transport within one process (Design/SpaceScene.md §6.1, §6.3): two queues between two ends, each behind
// a mutex, so that the two threads share nothing else. It is reliable and ordered, and copies bytes in and out.
// MakeLoopbackPair makes the two ends.
class LoopbackTransport final : public Transport
{
public:
  // _outgoing is the queue at the link's index this end sends into; it receives from the other.
  LoopbackTransport(std::shared_ptr<LoopbackLink> _link, std::size_t _outgoing) noexcept;
  ~LoopbackTransport() override;
  LoopbackTransport(const LoopbackTransport&) = delete;
  LoopbackTransport& operator=(const LoopbackTransport&) = delete;
  LoopbackTransport(LoopbackTransport&&) = delete;
  LoopbackTransport& operator=(LoopbackTransport&&) = delete;

  [[nodiscard]] bool Send(std::span<const std::uint8_t> _message) override;
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> Receive() override;
  void Close() noexcept override;
  [[nodiscard]] bool IsOpen() const noexcept override;

private:
  std::shared_ptr<LoopbackLink> m_link;
  std::size_t m_outgoing;
};

// The two ends of one loopback: what the client holds, and what the server holds.
struct LoopbackPair
{
  std::unique_ptr<Transport> client;
  std::unique_ptr<Transport> server;
};

[[nodiscard]] LoopbackPair MakeLoopbackPair();

} // namespace NeuronCore
