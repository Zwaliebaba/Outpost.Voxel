#pragma once

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// What one dispatch of bloom's chain knows (R16, Design/SpaceScene.md §12.2), as root constants: the size of the level
// it writes, whether it takes Karis's average, as the first halving does, and on the way up the share of the tent over
// the level below. This struct is the truth; Shader/BloomConstants.hlsli mirrors it, and the layout echo in
// NeuronClientTests proves the two agree.
struct BloomConstants
{
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  std::uint32_t karis; // 1 for Karis's average, 0 for none
  float belowShare;
};

static_assert(sizeof(BloomConstants) == 16);
static_assert(offsetof(BloomConstants, widthPixels) == 0);
static_assert(offsetof(BloomConstants, heightPixels) == 4);
static_assert(offsetof(BloomConstants, karis) == 8);
static_assert(offsetof(BloomConstants, belowShare) == 12);

// The root constants' count, in 32-bit words.
inline constexpr std::uint32_t BLOOM_CONSTANT_WORDS = sizeof(BloomConstants) / sizeof(std::uint32_t);

} // namespace NeuronClient
