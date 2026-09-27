#pragma once

// Stephen Hill's fit of the ACES reference rendering and output device transforms (Design/SampleRenderer.md §11), from
// his BakingLab sample: linear Rec. 709 in, linear Rec. 709 out, clamped to [0, 1]. The C++ twins are in
// NeuronCore/ToneMap.h (R15).

// sRGB to XYZ, D65 to D60, to AP1, and the RRT's saturation; row by row, applied as mul(matrix, color).
static const float3x3 ACES_INPUT = {0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840, 0.13383, 0.83777};

// The ODT's saturation, to XYZ, D60 to D65, and to sRGB.
static const float3x3 ACES_OUTPUT = {1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605, -0.00327, -0.07276, 1.07602};

// The RRT and ODT curves, fitted as one rational function per channel.
float3 RrtAndOdtFit(float3 _value)
{
  float3 numerator = _value * (_value + 0.0245786) - 0.000090537;
  float3 denominator = _value * (0.983729 * _value + 0.4329510) + 0.238081;
  return numerator / denominator;
}

float3 AcesFitted(float3 _color)
{
  return saturate(mul(ACES_OUTPUT, RrtAndOdtFit(mul(ACES_INPUT, _color))));
}

// The exposure, a multiplier, then the fit.
float3 ToneMap(float3 _color, float _exposure)
{
  return AcesFitted(_color * _exposure);
}
