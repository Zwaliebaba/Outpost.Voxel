#pragma once

#include "Float3.h"

#include <cstdint>

namespace NeuronCore
{

// What the debug view pass shows (Design/SampleRenderer.md §11), in the order of the keys that select them. Until the
// lighting lands in M3, the first view is a headlight: albedo lit from the camera, enough to read the shape. The values
// are the DEBUG_VIEW_* constants of NeuronClient/Shader/DebugView.hlsli.
enum class DebugView : std::uint32_t
{
  Headlight,
  Albedo,
  Normal,
  VoxelIndex
};

inline constexpr std::uint32_t DEBUG_VIEW_COUNT = 4;

// The PCG hash of Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 9(3), 2020. The twin of PcgHash in
// DebugView.hlsli (R15).
[[nodiscard]] constexpr std::uint32_t PcgHash(std::uint32_t _value) noexcept
{
  const std::uint32_t state = _value * 747796405u + 2891336453u;
  const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

// The linear color the debug view pass writes for one pixel: the twin of DebugViewColor in DebugView.hlsli (R15).
// _forward is the camera's view direction; a pixel no voxel covers is black in every view.
[[nodiscard]] Float3 DebugViewColor(DebugView _view, std::uint32_t _voxel, Float3 _normal, Float3 _albedo, Float3 _forward) noexcept;

} // namespace NeuronCore
