#pragma once

#include <chrono>

namespace NeuronClient
{

// Frame time: seconds between one tick and the next, on the steady clock.
class Clock
{
public:
  Clock() noexcept
    : m_last(std::chrono::steady_clock::now())
  {
  }

  // Seconds since the previous tick, or since the clock was made.
  [[nodiscard]] double Tick() noexcept
  {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = now - m_last;
    m_last = now;
    return elapsed.count();
  }

private:
  std::chrono::steady_clock::time_point m_last;
};

} // namespace NeuronClient
