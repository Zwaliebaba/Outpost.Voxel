#pragma once

#include "Float3.h"
#include "Ray.h"
#include "Sphere.h"

#include <cstdint>
#include <span>
#include <vector>

namespace NeuronCore
{

// The sun's view for the shadow map (Design/Archive/SampleRenderer.md §10): every ray travels along forward and starts on the
// near plane, and depth is standard Z, 0 at the near plane and 1 at the far one (§7.5).
struct OrthographicView
{
  Float3 origin; // the centre of the near plane
  Float3 right;  // orthonormal and left-handed: right × up = +forward
  Float3 up;
  Float3 forward;
  float halfWidth; // world units
  float halfHeight;
  float depthRange; // world units from the near plane to the far one
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
};

// Looks along _forward from the plane through _origin, with _worldUp as up unless the two are parallel (MakeViewBasis).
[[nodiscard]] OrthographicView MakeOrthographicView(Float3 _origin, Float3 _forward, Float3 _worldUp, float _halfWidth, float _halfHeight,
                                                    float _depthRange, std::uint32_t _widthPixels, std::uint32_t _heightPixels) noexcept;

// The ray from the centre of pixel (x, y) on the near plane, y down, travelling along forward with unit speed: the ray
// parameter of a hit is its distance from the near plane. The twin of the shadow splat pixel shader's ray (R15).
[[nodiscard]] Ray OrthographicRay(const OrthographicView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY) noexcept;

[[nodiscard]] constexpr float OrthographicDepth(const OrthographicView& _view, float _distance) noexcept
{
  return _distance / _view.depthRange;
}

// The two facts of standard Z that code outside this header may rely on (§7.5, Design/ADR/ADR-006): the far plane has
// depth 1, which is what a shadow map is cleared to, and a nearer point has the smaller depth. The shadow splat pass
// clears to the one and tests with the other.
inline constexpr float ORTHOGRAPHIC_FAR_DEPTH = 1.0f;

[[nodiscard]] constexpr bool IsNearerOrthographicDepth(float _depth, float _than) noexcept
{
  return _depth < _than;
}

// The sun's view of §10: 4096 texels across a 1,024-unit square, four to a voxel's edge. The detonation's defaults keep
// its envelope inside the square (Design/Archive/SpaceScene.md §5.5).
inline constexpr std::uint32_t SHADOW_MAP_PIXELS = 4096;
inline constexpr float SHADOW_HALF_EXTENT = 512.0f;

// The sun's view (§10): a square 2 × _halfExtent world units across, perpendicular to the sun and centred on _center,
// whose near plane lies a unit before every point of the box _lower-_upper and whose depth range reaches a unit past
// the last. The box is what casts and receives shadows; it never moves, so neither do the shadows.
[[nodiscard]] OrthographicView MakeShadowView(Float3 _toSun, Float3 _center, float _halfExtent, Float3 _lower, Float3 _upper,
                                              std::uint32_t _sizePixels) noexcept;

// Whether _view keeps _sphere (Design/Archive/SpaceScene.md §7.4): false only when the sphere lies more than CULL_MARGIN beyond
// one of the box's six faces. A sphere that touches the box is kept.
[[nodiscard]] bool IsInView(const OrthographicView& _view, const Sphere& _sphere) noexcept;

// What a shadow view draws (§7.4): the index of every sphere it keeps, in their order. Depth alone needs no other.
[[nodiscard]] std::vector<std::uint32_t> ListShadowDraws(const OrthographicView& _view, std::span<const Sphere> _spheres);

} // namespace NeuronCore
