#pragma once

#include "Float3.h"
#include "OrthographicView.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// The sun's orthographic view as the shadow splat, the lighting and the shadow-map view read it (R16,
// Design/SampleRenderer.md §7.4, §10). This struct is the truth; Shader/ShadowViewConstants.hlsli mirrors it, and the
// layout echo in NeuronClientTests proves the two agree. A float3 and one scalar to each 16 bytes, so no padding.
struct ShadowViewConstants
{
  NeuronCore::Float3 origin; // the centre of the near plane
  float halfWidth;
  NeuronCore::Float3 right;
  float halfHeight;
  NeuronCore::Float3 up;
  float depthRange;
  NeuronCore::Float3 forward;
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
};

static_assert(sizeof(ShadowViewConstants) == 68);
static_assert(offsetof(ShadowViewConstants, origin) == 0);
static_assert(offsetof(ShadowViewConstants, halfWidth) == 12);
static_assert(offsetof(ShadowViewConstants, right) == 16);
static_assert(offsetof(ShadowViewConstants, halfHeight) == 28);
static_assert(offsetof(ShadowViewConstants, up) == 32);
static_assert(offsetof(ShadowViewConstants, depthRange) == 44);
static_assert(offsetof(ShadowViewConstants, forward) == 48);
static_assert(offsetof(ShadowViewConstants, widthPixels) == 60);
static_assert(offsetof(ShadowViewConstants, heightPixels) == 64);

[[nodiscard]] ShadowViewConstants MakeShadowViewConstants(const NeuronCore::OrthographicView& _view) noexcept;

} // namespace NeuronClient
