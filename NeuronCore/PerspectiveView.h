#pragma once

#include "Float3.h"
#include "Ray.h"
#include "Sphere.h"

#include <cstdint>
#include <span>
#include <vector>

namespace NeuronCore
{

// A pinhole camera as the view splat pass and the reference tracer both see it (Design/Archive/SampleRenderer.md §7.5): world
// space is Direct3D's, left-handed with +Y up, the view looks along forward, and depth is reversed-Z with an infinite far
// plane.
struct PerspectiveView
{
  Float3 position;
  Float3 right; // orthonormal and left-handed: right × up = +forward
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

// The two facts of reversed-Z that code outside this header may rely on (§7.5): the far plane, at infinity, has depth 0,
// which is what a view is cleared to, and a nearer point has the greater depth. Nothing compares perspective depths
// another way; the view splat pass clears to the one and tests with the other.
inline constexpr float PERSPECTIVE_FAR_DEPTH = 0.0f;

[[nodiscard]] constexpr bool IsNearerPerspectiveDepth(float _depth, float _than) noexcept
{
  return _depth > _than;
}

// Normalized device coordinates of a pixel's centre: x right and y up, both in [-1, 1].
[[nodiscard]] Float2 PixelCenterNdc(std::uint32_t _pixelX, std::uint32_t _pixelY, std::uint32_t _widthPixels,
                                    std::uint32_t _heightPixels) noexcept;

// An orthonormal left-handed basis looking along _forward, with _worldUp as up unless the two are parallel, in which
// case +Z stands in: a sun straight overhead still gets a basis.
void MakeViewBasis(Float3 _forward, Float3 _worldUp, Float3& _right, Float3& _up) noexcept;

// Whether _view keeps _sphere (Design/SpaceScene.md §7.4): false only when the sphere lies more than CULL_MARGIN behind
// the near plane or beyond one of the four sides. The far plane is at infinity. A sphere that touches the frustum is
// kept, and so is one that lies just outside a corner, since each plane is tested on its own.
[[nodiscard]] bool IsInView(const PerspectiveView& _view, const Sphere& _sphere) noexcept;

// What the camera's view draws (§7.4): the index of every sphere it keeps, nearest first, by the distance from the eye
// to the sphere's nearest point, and in their order where two are as near. Drawn in that order, whatever occludes draws
// before what it hides, which is what the splat's early depth test needs.
[[nodiscard]] std::vector<std::uint32_t> ListViewDraws(const PerspectiveView& _view, std::span<const Sphere> _spheres);

} // namespace NeuronCore
