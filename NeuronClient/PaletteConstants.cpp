#include "pch.h"

#include "PaletteConstants.h"

#include "ColorSpace.h"
#include "Lighting.h"

#include <cstddef>

namespace NeuronClient
{

PaletteConstants MakePaletteConstants(const std::array<NeuronCore::PaletteEntry, NeuronCore::PALETTE_ENTRY_COUNT>& _palette) noexcept
{
  PaletteConstants constants{};
  for (std::size_t i = 0; i < _palette.size(); ++i)
  {
    const NeuronCore::PaletteEntry& entry = _palette[i];
    constants.materials[i] = {
      {NeuronCore::SrgbToLinear(entry.red), NeuronCore::SrgbToLinear(entry.green), NeuronCore::SrgbToLinear(entry.blue)},
      NeuronCore::EmissiveScale(entry)};
  }
  return constants;
}

} // namespace NeuronClient
