#pragma once

#include "Float3.h"
#include "Ray.h"

#include <cstdint>

namespace NeuronCore
{

// The sun's view for the shadow map (Design/SampleRenderer.md §10): every ray travels along forward and starts on the
// near plane, and depth is standard Z, 0 at the near plane and 1 at the far one (§7.5).
struct OrthographicView
{
  Float3 origin; // the centre of the near plane
  Float3 right;  // orthonormal and right-handed: right x up = -forward
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

} // namespace NeuronCore
