#pragma once

#include "Float3.h"
#include "Hash.h"

#include <cstdint>

namespace NeuronCore
{

// What the debug view pass shows in place of the lit image (Design/Archive/SampleRenderer.md §11), in the order of the keys
// that select them. The values are the DEBUG_VIEW_* constants of NeuronClient/Shader/DebugView.hlsli.
enum class DebugView : std::uint32_t
{
  Albedo,
  Normal,
  VoxelIndex,
  ShadowMap,
  Overdraw
};

inline constexpr std::uint32_t DEBUG_VIEW_COUNT = 5;

// The overdraw view is white above this many invocations per pixel, and a ramp from blue at one to red here below it.
inline constexpr std::uint32_t OVERDRAW_VIEW_SATURATION = 64;

// The linear color the debug view pass writes for a pixel of the visibility buffer in the views that show it: albedo,
// normal and voxel index. A pixel no voxel covers is black. The twin of DebugViewColor in DebugView.hlsli (R15).
[[nodiscard]] Float3 DebugViewColor(DebugView _view, std::uint32_t _voxel, Float3 _normal, Float3 _albedo) noexcept;

// The shadow-map view shows the map as a square as tall as the view's shorter side, in the middle of the view. This
// finds the texel under pixel (x, y), in integers so that the shader finds the same one, and returns false outside
// the square. The twin of ShadowMapViewTexel in DebugView.hlsli (R15).
[[nodiscard]] bool ShadowMapViewTexel(std::uint32_t _pixelX, std::uint32_t _pixelY, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                                      std::uint32_t _mapWidthPixels, std::uint32_t _mapHeightPixels, std::uint32_t& _texelX,
                                      std::uint32_t& _texelY) noexcept;

// The gray the shadow-map view shows for a depth: the depth itself, black at the sun's near plane and white at the far
// one, which is where the map holds nothing.
[[nodiscard]] constexpr Float3 ShadowMapViewColor(float _depth) noexcept
{
  return {_depth, _depth, _depth};
}

// The heat map the overdraw view shows for the splat pixel-shader invocations a pixel counted (§11): black for none,
// then a ramp through blue, cyan, green, yellow and red, evenly spaced in log2 of the count from 1 to
// OVERDRAW_VIEW_SATURATION, and white above it. Between two powers of two the position is linear in the count, which
// puts the colors at 1, 3, 8, 24 and 64 and lets the shader compute the ramp exactly as the twin does, with no logarithm. The twin of OverdrawViewColor in DebugView.hlsli
// (R15).
[[nodiscard]] Float3 OverdrawViewColor(std::uint32_t _invocations) noexcept;

} // namespace NeuronCore
