#include "pch.h"

#include "LoopbackTransport.h"

#include <array>
#include <deque>
#include <mutex>
#include <utility>

namespace NeuronCore
{

// One direction of a loopback: its messages, and whether the loopback is closed, both behind the one mutex. Nothing
// crosses between the ends through an atomic: ARM64 orders memory more weakly than x64, and CI runs x64 alone
// (Design/SpaceScene.md §6.3).
struct LoopbackQueue
{
  std::mutex mutex;
  std::deque<std::vector<std::uint8_t>> messages;
  bool closed = false;
};

struct LoopbackLink
{
  std::array<LoopbackQueue, 2> queues;
};

LoopbackTransport::LoopbackTransport(std::shared_ptr<LoopbackLink> _link, std::size_t _outgoing) noexcept
  : m_link(std::move(_link)),
    m_outgoing(_outgoing)
{
}

// An end that goes away closes the loopback, so that the other end learns of it.
LoopbackTransport::~LoopbackTransport()
{
  Close();
}

bool LoopbackTransport::Send(std::span<const std::uint8_t> _message)
{
  LoopbackQueue& queue = m_link->queues[m_outgoing];
  const std::scoped_lock lock(queue.mutex);
  if (queue.closed)
  {
    return false;
  }
  queue.messages.emplace_back(_message.begin(), _message.end());
  return true;
}

std::optional<std::vector<std::uint8_t>> LoopbackTransport::Receive()
{
  LoopbackQueue& queue = m_link->queues[1 - m_outgoing];
  const std::scoped_lock lock(queue.mutex);
  if (queue.messages.empty())
  {
    return std::nullopt;
  }
  std::vector<std::uint8_t> message = std::move(queue.messages.front());
  queue.messages.pop_front();
  return message;
}

void LoopbackTransport::Close() noexcept
{
  for (LoopbackQueue& queue : m_link->queues)
  {
    const std::scoped_lock lock(queue.mutex);
    queue.closed = true;
  }
}

bool LoopbackTransport::IsOpen() const noexcept
{
  LoopbackQueue& queue = m_link->queues[m_outgoing];
  const std::scoped_lock lock(queue.mutex);
  return !queue.closed;
}

LoopbackPair MakeLoopbackPair()
{
  auto link = std::make_shared<LoopbackLink>();
  return {std::make_unique<LoopbackTransport>(link, 0), std::make_unique<LoopbackTransport>(link, 1)};
}

} // namespace NeuronCore
