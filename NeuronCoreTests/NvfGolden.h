#pragma once

#include "NvfModel.h"

namespace NeuronCoreTests
{

// Where the golden file lies, from the repository's root (Design/NeuronVoxelFormat.md §9).
inline constexpr const char* GOLDEN_NVF_PATH = "Tools/Golden/Golden.nvf";

// The model Tools/Golden/Golden.nvf holds, built field by field. It uses every field of the format: three parts, two
// levels deep; all sixteen palette entries, some emissive, one translucent, and one diffuse entry that still carries
// _emit and _flux, as the .vox reader keeps them; voxels in every entry and at every part's far corner; hardpoints with
// and without FromVox, one of three segments; rotations of 0, 90, 180 and 30 degrees; negative translations; and an
// authored pivot beside default ones. The hardpoints are listed out of name order, which the writer puts right.
// NvfFormat.py's tests hold the same values (N7).
[[nodiscard]] NeuronCore::NvfModel GoldenNvfModel();

} // namespace NeuronCoreTests
