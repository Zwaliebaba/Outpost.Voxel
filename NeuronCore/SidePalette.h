#pragma once

#include "Message.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace NeuronCore
{

// A side's color on its entities (G24, Design/ADR/ADR-029): every palette an entity of a side draws with shows the side's
// color in this entry, which the game keeps for it in every design's and every module's palette. Entry 16, the last, so
// that the fifteen before it stay the author's.
inline constexpr std::uint32_t SIDE_PALETTE_ENTRY = PALETTE_ENTRY_COUNT;

// _palette as a side of color _color draws it: its SIDE_PALETTE_ENTRY takes the side's color, and keeps its alpha and its
// material; every other entry is as it was.
[[nodiscard]] std::array<PaletteEntry, PALETTE_ENTRY_COUNT> SidePalette(const std::array<PaletteEntry, PALETTE_ENTRY_COUNT>& _palette,
                                                                        SideColor _color) noexcept;

// Where the palette of model _model, as side _side draws it, stands among a scene's palettes: each model's own, which side
// 0 draws with, then its variant for each of _sideCount sides, model after model.
[[nodiscard]] constexpr std::uint32_t SidePaletteIndex(std::uint32_t _model, std::uint32_t _side, std::size_t _sideCount) noexcept
{
  return _model * static_cast<std::uint32_t>(_sideCount + 1) + _side;
}

// Where the same palette stands as a remembered entity draws it, dimmed (Design/ADR/ADR-032): after the palettes of every
// one of the scene's _modelCount models, in the same order.
[[nodiscard]] constexpr std::uint32_t RememberedPaletteIndex(std::uint32_t _model, std::uint32_t _side, std::size_t _sideCount,
                                                             std::size_t _modelCount) noexcept
{
  return static_cast<std::uint32_t>(_modelCount * (_sideCount + 1)) + SidePaletteIndex(_model, _side, _sideCount);
}

} // namespace NeuronCore
