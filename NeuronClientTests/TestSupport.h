#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "GraphicsDevice.h"
#include "SplatPass.h"
#include "VoxelScene.h"

#include "Box.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "VoxModel.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace NeuronClientTests
{

// The station, from above the working directory or above the test sources. A missing asset fails the test.
[[nodiscard]] NeuronCore::VoxModel LoadMilitaryStation();

// Runs _body on a WARP device at feature level 12_1, with the debug layer where it is installed. The suite fails rather
// than skips when WARP cannot be made (Design/SampleRenderer.md §14). A failed HRESULT becomes a test failure that
// names it, where it was checked and, after a device removal, what DRED recorded; anything the debug layer reports
// fails the test too.
void RunGpuTest(const std::function<void(NeuronClient::GraphicsDevice&)>& _body);

// What the view splat wrote for one view: per pixel, row by row, the visibility buffer's two words and the depth.
struct SplatImage
{
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  std::vector<std::uint32_t> visibility; // voxel index, then packed normal
  std::vector<float> depth;
};

// Clears, splats _scene for _view with a view splat pass and reads both targets back.
[[nodiscard]] SplatImage RenderSplat(NeuronClient::GraphicsDevice& _device, const NeuronClient::VoxelScene& _scene,
                                     const NeuronClient::SplatPass& _pass, const NeuronCore::PerspectiveView& _view);

// Clears a shadow map as wide as _view, splats _scene into it with a shadow splat pass and reads it back: standard
// depth per texel, row by row. _view must be square.
[[nodiscard]] std::vector<float> RenderShadowSplat(NeuronClient::GraphicsDevice& _device, const NeuronClient::VoxelScene& _scene,
                                                   const NeuronClient::SplatPass& _pass, const NeuronCore::OrthographicView& _view);

// The number a half-precision word holds, as an R16G16B16A16_FLOAT texture stores it.
[[nodiscard]] float HalfToFloat(std::uint16_t _half) noexcept;

// The box a record index is drawn as while the model is intact.
[[nodiscard]] NeuronCore::Box RecordBox(const NeuronCore::VoxModel& _model, std::uint32_t _record);

// The sun's view of _model, fitted as the application fits it (§10): a square 2 × _halfExtent across, centred on the box
// every placed model's SIZE spans, and deep enough for that box grown down to the ground.
[[nodiscard]] NeuronCore::OrthographicView TestShadowView(const NeuronCore::VoxModel& _model, NeuronCore::Float3 _toSun, float _halfExtent,
                                                          std::uint32_t _sizePixels);

// The vertical field of view and near plane every test camera uses: the application's (§3, §7.5).
inline constexpr float TEST_FOV_Y_RADIANS = 0.785398163f;
inline constexpr float TEST_NEAR_PLANE = 0.1f;

// Room for a test's constants.
inline constexpr std::uint64_t TEST_CONSTANTS_BYTES = std::uint64_t{64} * 1024;

} // namespace NeuronClientTests
