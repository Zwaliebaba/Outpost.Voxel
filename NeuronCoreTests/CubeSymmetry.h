#pragma once

#include "Float3.h"
#include "RigidTransform.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace NeuronCoreTests
{

// One of the 48 signed permutations of the axes: the cube's 24 rotations and its 24 reflections.
struct CubeSymmetry
{
  NeuronCore::Rotation rotation;
  bool proper; // a rotation rather than a reflection
};

// All 48, each column a distinct axis, either way.
[[nodiscard]] inline std::array<CubeSymmetry, 48> CubeSymmetries() noexcept
{
  constexpr std::array<std::array<std::uint32_t, 3>, 6> PERMUTATIONS{{{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
  const auto axis = [](std::uint32_t _axis, bool _negative) noexcept
  {
    const float sign = _negative ? -1.0f : 1.0f;
    return NeuronCore::Float3{_axis == 0u ? sign : 0.0f, _axis == 1u ? sign : 0.0f, _axis == 2u ? sign : 0.0f};
  };
  std::array<CubeSymmetry, 48> symmetries{};
  std::size_t next = 0;
  for (const std::array<std::uint32_t, 3>& permutation : PERMUTATIONS)
  {
    for (std::uint32_t signs = 0; signs < 8u; ++signs)
    {
      const NeuronCore::Rotation rotation{axis(permutation[0], (signs & 1u) != 0u), axis(permutation[1], (signs & 2u) != 0u),
                                          axis(permutation[2], (signs & 4u) != 0u)};
      symmetries[next++] = {rotation, NeuronCore::Dot(NeuronCore::Cross(rotation.axisX, rotation.axisY), rotation.axisZ) > 0.0f};
    }
  }
  return symmetries;
}

} // namespace NeuronCoreTests
