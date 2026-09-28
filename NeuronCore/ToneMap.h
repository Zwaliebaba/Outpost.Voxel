#pragma once

#include "Float3.h"

namespace NeuronCore
{

// Stephen Hill's fit of the ACES reference rendering and output device transforms (Design/Archive/SampleRenderer.md §11), from
// his BakingLab sample: linear Rec. 709 in, linear Rec. 709 out, clamped to [0, 1]. The twin of AcesFitted in
// ToneMap.hlsli (R15).
[[nodiscard]] Float3 AcesFitted(Float3 _color) noexcept;

// What the tone map pass writes before the sRGB view encodes it: the exposure, then the fit (§11).
[[nodiscard]] Float3 ToneMap(Float3 _color, float _exposure) noexcept;

} // namespace NeuronCore
