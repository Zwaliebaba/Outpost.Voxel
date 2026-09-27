#include "pch.h"

#include "ColorSpace.h"

#include <cmath>

namespace NeuronCore
{

float SrgbToLinear(std::uint8_t _encoded) noexcept
{
  // In double, so that the one rounding is the final one to float.
  const double encoded = static_cast<double>(_encoded) / 255.0;
  const double linear = encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
  return static_cast<float>(linear);
}

} // namespace NeuronCore
