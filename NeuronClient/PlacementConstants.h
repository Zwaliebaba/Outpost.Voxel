#pragma once

#include "Float3.h"
#include "Placement.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// What a placement's firstMaskWord holds when it draws every voxel (Design/ADR/ADR-035).
inline constexpr std::uint32_t NO_MASK = 0xFFFFFFFFu;

// One placement as the shaders read it, an element of the frame's structured buffer of placements (R16,
// Design/Archive/SpaceScene.md §7.2, §7.3): its rigid transform, the records it draws, the id of its first voxel, its palette,
// and where its mask starts in the frame's buffer of mask words, or NO_MASK (Design/ADR/ADR-035). This struct is the
// truth; Shader/PlacementConstants.hlsli mirrors it, and the layout echo in NeuronClientTests proves the two agree. Each
// float3 shares 16 bytes with a word, so there is no padding, and the mask's word follows: the stride is 68 bytes.
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
  std::uint32_t firstMaskWord;
};

static_assert(sizeof(PlacementConstants) == 68);
static_assert(offsetof(PlacementConstants, axisX) == 0);
static_assert(offsetof(PlacementConstants, firstRecord) == 12);
static_assert(offsetof(PlacementConstants, axisY) == 16);
static_assert(offsetof(PlacementConstants, recordCount) == 28);
static_assert(offsetof(PlacementConstants, axisZ) == 32);
static_assert(offsetof(PlacementConstants, firstVoxel) == 44);
static_assert(offsetof(PlacementConstants, translation) == 48);
static_assert(offsetof(PlacementConstants, paletteIndex) == 60);
static_assert(offsetof(PlacementConstants, firstMaskWord) == 64);

// _placement as the shaders read it, its mask starting at word _firstMaskWord of the frame's buffer of mask words, or
// NO_MASK while it draws every voxel.
[[nodiscard]] PlacementConstants MakePlacementConstants(const NeuronCore::Placement& _placement, std::uint32_t _firstMaskWord) noexcept;

} // namespace NeuronClient
