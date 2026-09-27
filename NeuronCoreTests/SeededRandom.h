#pragma once

#include "Float3.h"

#include <cmath>
#include <cstdint>
#include <random>

namespace NeuronCoreTests
{

// Random numbers that are the same on every standard library. The output of std::mt19937 is fixed by the standard and
// the distributions are not, so the mapping to floats happens here, and nothing in it goes through a transcendental
// function whose last bit could differ between libraries.
class SeededRandom
{
public:
  explicit SeededRandom(std::uint32_t _seed) noexcept
    : m_engine(_seed)
  {
  }

  // Uniform in [_lower, _upper), from the top 24 bits of one draw.
  [[nodiscard]] float Uniform(float _lower, float _upper) noexcept
  {
    const float unit = static_cast<float>(m_engine() >> 8u) * (1.0f / 16777216.0f);
    return _lower + (_upper - _lower) * unit;
  }

  // Uniform in [0, _count).
  [[nodiscard]] std::uint32_t Below(std::uint32_t _count) noexcept
  {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(m_engine()) * _count) >> 32u);
  }

  [[nodiscard]] NeuronCore::Float3 InBox(NeuronCore::Float3 _lower, NeuronCore::Float3 _upper) noexcept
  {
    return {Uniform(_lower.x, _upper.x), Uniform(_lower.y, _upper.y), Uniform(_lower.z, _upper.z)};
  }

  // A unit vector uniform over the sphere, by rejection from the cube.
  [[nodiscard]] NeuronCore::Float3 Direction() noexcept
  {
    while (true)
    {
      const NeuronCore::Float3 candidate = InBox({-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f});
      const float lengthSquared = NeuronCore::Dot(candidate, candidate);
      if (lengthSquared > 1.0e-4f && lengthSquared <= 1.0f)
      {
        return candidate * (1.0f / std::sqrt(lengthSquared));
      }
    }
  }

  // A rotation uniform over SO(3), as the three columns of its matrix: a unit quaternion by rejection from the 4-cube.
  void Rotation(NeuronCore::Float3& _axisX, NeuronCore::Float3& _axisY, NeuronCore::Float3& _axisZ) noexcept
  {
    float w = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float lengthSquared = 0.0f;
    do
    {
      w = Uniform(-1.0f, 1.0f);
      x = Uniform(-1.0f, 1.0f);
      y = Uniform(-1.0f, 1.0f);
      z = Uniform(-1.0f, 1.0f);
      lengthSquared = w * w + x * x + y * y + z * z;
    } while (lengthSquared < 1.0e-4f || lengthSquared > 1.0f);
    const float scale = 1.0f / std::sqrt(lengthSquared);
    w *= scale;
    x *= scale;
    y *= scale;
    z *= scale;
    _axisX = {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + w * z), 2.0f * (x * z - w * y)};
    _axisY = {2.0f * (x * y - w * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + w * x)};
    _axisZ = {2.0f * (x * z + w * y), 2.0f * (y * z - w * x), 1.0f - 2.0f * (x * x + y * y)};
  }

private:
  std::mt19937 m_engine;
};

} // namespace NeuronCoreTests
