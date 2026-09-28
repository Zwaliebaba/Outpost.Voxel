#pragma once

#include "Float3.h"
#include "PerspectiveView.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// A perspective view as the splat and debug view shaders read it (R16, Design/Archive/SampleRenderer.md §7.4). This struct is the
// truth; Shader/ViewConstants.hlsli mirrors it, and the layout echo in NeuronClientTests proves the two agree. The
// members follow HLSL's packing, a float3 and one scalar to each 16 bytes, so the struct has no padding of its own.
struct ViewConstants
{
  NeuronCore::Float3 position;
  float tanHalfFovY;
  NeuronCore::Float3 right;
  float aspect;
  NeuronCore::Float3 up;
  float nearPlane;
  NeuronCore::Float3 forward;
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
};

static_assert(sizeof(ViewConstants) == 68);
static_assert(offsetof(ViewConstants, position) == 0);
static_assert(offsetof(ViewConstants, tanHalfFovY) == 12);
static_assert(offsetof(ViewConstants, right) == 16);
static_assert(offsetof(ViewConstants, aspect) == 28);
static_assert(offsetof(ViewConstants, up) == 32);
static_assert(offsetof(ViewConstants, nearPlane) == 44);
static_assert(offsetof(ViewConstants, forward) == 48);
static_assert(offsetof(ViewConstants, widthPixels) == 60);
static_assert(offsetof(ViewConstants, heightPixels) == 64);

[[nodiscard]] ViewConstants MakeViewConstants(const NeuronCore::PerspectiveView& _view) noexcept;

} // namespace NeuronClient
