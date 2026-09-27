#pragma once

#include "Float3.h"
#include "Ray.h"

#include <cstdint>

namespace VoxelCore
{

// A pinhole camera as the view splat pass and the reference tracer both see it (Design/SampleRenderer.md §7.5): world
// space is right-handed with +Z up, the view looks along forward, and depth is reversed-Z with an infinite far plane.
struct PerspectiveView
{
  Float3 position;
  Float3 right; // orthonormal and right-handed: right x up = -forward
  Float3 up;
  Float3 forward;
  float tanHalfFovY;
  float aspect;    // width / height
  float nearPlane; // n, in world units
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
};

// Looks from _position at _target, with _worldUp as the up direction when it is not parallel to the view.
[[nodiscard]] PerspectiveView MakePerspectiveView(Float3 _position, Float3 _target, Float3 _worldUp, float _fovYRadians, float _nearPlane,
                                                  std::uint32_t _widthPixels, std::uint32_t _heightPixels) noexcept;

// The ray through the centre of pixel (x, y), y down. Its direction has a view-depth component of exactly one, so the
// ray parameter of a hit is its view depth (§9.3). The twin of the ray the view splat pixel shader builds (R15).
[[nodiscard]] Ray PerspectiveRay(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY) noexcept;

// Reversed-Z with an infinite far plane: depth = n / view depth (§7.5).
[[nodiscard]] constexpr float PerspectiveDepth(const PerspectiveView& _view, float _viewDepth) noexcept
{
  return _view.nearPlane / _viewDepth;
}

// Normalized device coordinates of a pixel's centre: x right and y up, both in [-1, 1].
[[nodiscard]] Float2 PixelCenterNdc(std::uint32_t _pixelX, std::uint32_t _pixelY, std::uint32_t _widthPixels,
                                    std::uint32_t _heightPixels) noexcept;

// An orthonormal right-handed basis looking along _forward, with _worldUp as up unless the two are parallel, in which
// case +Y stands in: a sun straight overhead still gets a basis.
void MakeViewBasis(Float3 _forward, Float3 _worldUp, Float3& _right, Float3& _up) noexcept;

} // namespace VoxelCore
