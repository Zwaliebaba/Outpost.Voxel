#pragma once

#include "Float3.h"
#include "Placement.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// One placement as the shaders read it, an element of the frame's structured buffer of placements (R16,
// Design/SpaceScene.md §7.2, §7.3): its rigid transform, the records it draws, the id of its first voxel and its palette.
// This struct is the truth; Shader/PlacementConstants.hlsli mirrors it, and the layout echo in NeuronClientTests proves
// the two agree. Each float3 shares 16 bytes with a word, so there is no padding and the stride is 64 bytes.
struct PlacementConstants
{
  NeuronCore::Float3 axisX; // the rotation's columns: the part's axes in the world
  std::uint32_t firstRecord;
  NeuronCore::Float3 axisY;
  std::uint32_t recordCount;
  NeuronCore::Float3 axisZ;
  std::uint32_t firstVoxel;
  NeuronCore::Float3 translation;
  std::uint32_t paletteIndex;
};

static_assert(sizeof(PlacementConstants) == 64);
static_assert(offsetof(PlacementConstants, axisX) == 0);
static_assert(offsetof(PlacementConstants, firstRecord) == 12);
static_assert(offsetof(PlacementConstants, axisY) == 16);
static_assert(offsetof(PlacementConstants, recordCount) == 28);
static_assert(offsetof(PlacementConstants, axisZ) == 32);
static_assert(offsetof(PlacementConstants, firstVoxel) == 44);
static_assert(offsetof(PlacementConstants, translation) == 48);
static_assert(offsetof(PlacementConstants, paletteIndex) == 60);

[[nodiscard]] PlacementConstants MakePlacementConstants(const NeuronCore::Placement& _placement) noexcept;

} // namespace NeuronClient
