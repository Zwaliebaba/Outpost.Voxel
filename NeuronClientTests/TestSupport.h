#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "GraphicsDevice.h"
#include "SplatPass.h"
#include "UploadRing.h"
#include "VoxelScene.h"

#include "Box.h"
#include "Message.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "Placement.h"
#include "VoxModel.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace NeuronClientTests
{

// GameData/_fileName, from above the working directory or above the test sources. A missing or refused asset fails the
// test.
[[nodiscard]] NeuronCore::VoxModel LoadGameData(const wchar_t* _fileName);

// The station, as LoadGameData finds it.
[[nodiscard]] NeuronCore::VoxModel LoadMilitaryStation();

// The GameData folder LoadGameData finds its files in, where a client session reads the models a welcome names.
[[nodiscard]] std::filesystem::path GameDataDirectory();

// _model's parts drawn whole, each unturned at its origin, as the application draws its scene (Design/SpaceScene.md §7):
// the one model of a scene, so that every voxel's id is its record's index, and its box the one RecordBox gives.
[[nodiscard]] std::vector<NeuronCore::Placement> WholePlacements(const NeuronCore::VoxModel& _model);

// Part _part of _model, model _modelIndex of a scene whose record buffer holds its records from _modelFirstRecord on,
// turned by _rotation about the middle of its box, which it puts at _center. Its id is AssignVoxelIds's to give.
[[nodiscard]] NeuronCore::Placement PlaceCentered(const NeuronCore::VoxModel& _model, std::uint32_t _modelIndex,
                                                  std::uint32_t _modelFirstRecord, std::uint32_t _part,
                                                  const NeuronCore::Rotation& _rotation, NeuronCore::Float3 _center);

// _placements detonated _timeSeconds ago by _parameters, whose blast origin and inherited velocity are in the world: each
// placement gets them in its part's space (§7.7).
[[nodiscard]] std::vector<NeuronCore::Placement> DetonatePlacements(std::vector<NeuronCore::Placement> _placements,
                                                                    const NeuronCore::ExplosionParameters& _parameters, float _timeSeconds);

// Every box _placements draw where the twin puts them, oriented, with half-extents of ½ + _change: grown boxes are hit
// wherever a ray passes near an edge, shrunk ones only well inside. In the order of the voxels' ids, which
// AssignVoxelIds gave, so that a box's index is its voxel's id. _records is the scene's record buffer.
[[nodiscard]] std::vector<NeuronCore::Box> PlacedBoxes(std::span<const std::uint32_t> _records,
                                                       std::span<const NeuronCore::Placement> _placements, float _change);

// Runs _body on a WARP device at feature level 12_1, with the debug layer where it is installed. The suite fails rather
// than skips when WARP cannot be made (Design/Archive/SampleRenderer.md §14). A failed HRESULT becomes a test failure that
// names it, where it was checked and, after a device removal, what DRED recorded; anything the debug layer reports
// fails the test too.
void RunGpuTest(const std::function<void(NeuronClient::GraphicsDevice&)>& _body);

// What the view splat wrote for one view: per pixel, row by row, the visibility buffer's two words and the depth, and
// what the overdraw variant counted.
struct SplatImage
{
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  std::vector<std::uint32_t> visibility; // voxel index, then packed normal
  std::vector<float> depth;
  std::vector<std::uint32_t> overdraw; // pixel-shader invocations; empty unless the pass counts them
};

// How a test draws its placements: each through the permutation it calls for, as the renderer does, or every one through
// the oriented permutation, a whole one at rest. Design/SpaceScene.md §15 holds the two to the same image where a
// placement is whole and turned by a symmetry of the cube.
enum class Permutations : std::uint8_t
{
  AsPlaced,
  AllOriented
};

// Clears, splats every one of _placements of _scene for _view with a view splat pass, in their order and unculled, and
// reads its targets back, the overdraw count among them when the pass counts it.
[[nodiscard]] SplatImage RenderSplat(NeuronClient::GraphicsDevice& _device, const NeuronClient::VoxelScene& _scene,
                                     std::span<const NeuronCore::Placement> _placements, const NeuronClient::SplatPass& _pass,
                                     const NeuronCore::PerspectiveView& _view, Permutations _permutations = Permutations::AsPlaced);

// Clears a shadow map as wide as _view, splats every one of _placements of _scene into it with a shadow splat pass, in
// their order and unculled, and reads it back: standard depth per texel, row by row. _view must be square.
[[nodiscard]] std::vector<float> RenderShadowSplat(NeuronClient::GraphicsDevice& _device, const NeuronClient::VoxelScene& _scene,
                                                   std::span<const NeuronCore::Placement> _placements, const NeuronClient::SplatPass& _pass,
                                                   const NeuronCore::OrthographicView& _view,
                                                   Permutations _permutations = Permutations::AsPlaced);

// _placements pushed into _ring as the splat passes read them, every draw through the oriented permutation when
// _permutations asks for it.
[[nodiscard]] NeuronClient::SplatPlacements PushTestPlacements(NeuronClient::UploadRing& _ring,
                                                               std::span<const NeuronCore::Placement> _placements,
                                                               Permutations _permutations = Permutations::AsPlaced);

// A seeded, random 8 × 8 × 8 block of voxels, a little over a third of the cells full, its corner at the origin: small
// enough to check by brute force.
[[nodiscard]] NeuronCore::VoxModel RandomBlock();

// The box a record index is drawn as while the model is intact, through WholePlacements.
[[nodiscard]] NeuronCore::Box RecordBox(const NeuronCore::VoxModel& _model, std::uint32_t _record);

// The sun's view of _model, fitted as the application fits the intact model (§10): a square 2 × _halfExtent across,
// centred on the box every placed model's SIZE spans, and deep enough for that box.
[[nodiscard]] NeuronCore::OrthographicView TestShadowView(const NeuronCore::VoxModel& _model, NeuronCore::Float3 _toSun, float _halfExtent,
                                                          std::uint32_t _sizePixels);

// Copies _pixels into a 2D texture, rows packed tightly, _bytesPerPixel times its width to a row. The texture is in
// _state before and after.
void WriteTexture2D(NeuronClient::GraphicsDevice& _device, ID3D12Resource* _texture, D3D12_RESOURCE_STATES _state,
                    std::span<const std::byte> _pixels, std::uint32_t _bytesPerPixel);

// A world lit as the station's file lit the station until the lighting came from the world (Design/ADR/ADR-008,
// Design/SpaceScene.md §12.1): a white sun at 50 and 50 degrees at 0.7, and a hemisphere from the ground's sRGB 80 at
// 0.7 below to white at 0.7 above, brighter than the space scene's so that the ambient shows; with the sector's sun
// size, galactic plane and seed.
[[nodiscard]] NeuronCore::WorldSettings TestWorld() noexcept;

// How many halves apart two values a half-precision target holds lie, both at least zero, as light is: its measure of
// how far the GPU strays from a twin that stores as it does.
[[nodiscard]] std::uint32_t HalfSteps(float _expected, float _actual) noexcept;

// The vertical field of view and near plane every test camera uses: the application's (§3, §7.5).
inline constexpr float TEST_FOV_Y_RADIANS = 0.785398163f;
inline constexpr float TEST_NEAR_PLANE = 0.1f;

// Room for a test's constants.
inline constexpr std::uint64_t TEST_CONSTANTS_BYTES = std::uint64_t{64} * 1024;

} // namespace NeuronClientTests
