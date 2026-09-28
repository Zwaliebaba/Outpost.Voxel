#include "pch.h"

#include "Half.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

void AreEqualHalf(std::uint32_t _expected, float _value, const wchar_t* _what)
{
  Assert::AreEqual(_expected, static_cast<std::uint32_t>(NeuronCore::FloatToHalf(_value)), _what);
}

} // namespace

// Half precision as a DXGI_FORMAT_R16G16B16A16_FLOAT target stores it (Design/SpaceScene.md §12.2): the twins of the HDR
// color and of bloom's chain round through it, as the GPU does.
TEST_CLASS(HalfTests)
{
public:
  // Every half but a NaN comes back as itself, and a NaN as a NaN.
  TEST_METHOD(EveryHalfRoundTrips)
  {
    for (std::uint32_t bits = 0; bits <= 0xFFFFu; ++bits)
    {
      const float value = NeuronCore::HalfToFloat(static_cast<std::uint16_t>(bits));
      if (std::isnan(value))
      {
        Assert::IsTrue(std::isnan(NeuronCore::HalfToFloat(NeuronCore::FloatToHalf(value))), std::format(L"NaN {:04x}", bits).c_str());
        continue;
      }
      Assert::AreEqual(bits, static_cast<std::uint32_t>(NeuronCore::FloatToHalf(value)), std::format(L"half {:04x}", bits).c_str());
    }
  }

  TEST_METHOD(ReadsTheFormat)
  {
    Assert::AreEqual(1.0f, NeuronCore::HalfToFloat(0x3C00u));
    Assert::AreEqual(-2.0f, NeuronCore::HalfToFloat(0xC000u));
    Assert::AreEqual(65504.0f, NeuronCore::HalfToFloat(0x7BFFu), L"the largest half");
    Assert::AreEqual(0x1.0p-14f, NeuronCore::HalfToFloat(0x0400u), L"the least normal");
    Assert::AreEqual(0x1.0p-24f, NeuronCore::HalfToFloat(0x0001u), L"the least subnormal");
    Assert::AreEqual(std::numeric_limits<float>::infinity(), NeuronCore::HalfToFloat(0x7C00u));
  }

  // Between two neighboring halves, a float goes to the nearer, and one halfway goes to the one whose last bit is zero:
  // checked at every boundary, below it, on it and above it.
  TEST_METHOD(RoundsToTheNearestAndTiesToEven)
  {
    for (std::uint32_t bits = 0; bits < 0x7BFFu; ++bits)
    {
      const float lower = NeuronCore::HalfToFloat(static_cast<std::uint16_t>(bits));
      const float upper = NeuronCore::HalfToFloat(static_cast<std::uint16_t>(bits + 1u));
      // Exact in single precision: the two have eleven significant bits, and their midpoint twelve.
      const float middle = 0.5f * (lower + upper);
      const std::uint32_t even = (bits & 1u) == 0u ? bits : bits + 1u;
      AreEqualHalf(bits, std::nextafter(middle, 0.0f), std::format(L"below the middle of {:04x}", bits).c_str());
      AreEqualHalf(even, middle, std::format(L"the middle of {:04x}", bits).c_str());
      AreEqualHalf(bits + 1u, std::nextafter(middle, 1.0e6f), std::format(L"above the middle of {:04x}", bits).c_str());
      AreEqualHalf(bits | 0x8000u, -std::nextafter(middle, 0.0f), std::format(L"below the middle of {:04x}, negated", bits).c_str());
    }
  }

  TEST_METHOD(OverflowsToInfinityAndKeepsTheSign)
  {
    AreEqualHalf(0x7BFFu, 65519.0f, L"below the tie past 65504");
    AreEqualHalf(0x7C00u, 65520.0f, L"the tie past 65504 goes to the even infinity");
    AreEqualHalf(0x7C00u, 1.0e30f, L"far beyond");
    AreEqualHalf(0xFC00u, -1.0e30f, L"far below");
    AreEqualHalf(0x7C00u, std::numeric_limits<float>::infinity(), L"infinity");
    AreEqualHalf(0x8000u, -0.0f, L"negative zero");
    AreEqualHalf(0x0000u, 0x1.0p-26f, L"far below the least subnormal");
    Assert::IsTrue(std::isnan(NeuronCore::HalfToFloat(NeuronCore::FloatToHalf(std::numeric_limits<float>::quiet_NaN()))));
  }
};

} // namespace NeuronCoreTests
