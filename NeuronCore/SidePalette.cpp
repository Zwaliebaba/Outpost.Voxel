#include "pch.h"

#include "SidePalette.h"

namespace NeuronCore
{

std::array<PaletteEntry, PALETTE_ENTRY_COUNT> SidePalette(const std::array<PaletteEntry, PALETTE_ENTRY_COUNT>& _palette,
                                                          SideColor _color) noexcept
{
  std::array<PaletteEntry, PALETTE_ENTRY_COUNT> palette = _palette;
  PaletteEntry& entry = palette[SIDE_PALETTE_ENTRY - 1];
  entry.red = _color.red;
  entry.green = _color.green;
  entry.blue = _color.blue;
  return palette;
}

} // namespace NeuronCore
