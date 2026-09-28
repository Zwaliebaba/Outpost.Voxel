#include "pch.h"

#include "ColorSpace.h"
#include "DebugView.h"
#include "Float3.h"
#include "TraceHit.h"

#include <array>
#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::DebugView;
using NeuronCore::Float3;

void AreEqualFloat3(Float3 _expected, Float3 _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _what);
  Assert::AreEqual(_expected.y, _actual.y, _what);
  Assert::AreEqual(_expected.z, _actual.z, _what);
}

} // namespace

// The debug view twin and the palette's color conversion (Design/SampleRenderer.md §7.2, §11). The GPU side is compared
// with the twin in NeuronClientTests; these pin the twin itself.
TEST_CLASS(DebugViewTests)
{
public:
  // Reference values from an independent implementation of Jarzynski and Olano's pcg_hash.
  TEST_METHOD(PcgHashMatchesTheReference)
  {
    static_assert(NeuronCore::PcgHash(0u) == 0x07BB2FE2u);
    Assert::AreEqual(0xA8BEEA3Cu, NeuronCore::PcgHash(1u));
    Assert::AreEqual(0x7A7ECC88u, NeuronCore::PcgHash(2u));
    Assert::AreEqual(0x7791161Au, NeuronCore::PcgHash(225047u));
    Assert::AreEqual(0x807A86C7u, NeuronCore::PcgHash(0xFFFFFFFEu));
  }

  TEST_METHOD(NoVoxelIsBlackInEveryView)
  {
    for (std::uint32_t view = 0; view < NeuronCore::DEBUG_VIEW_COUNT; ++view)
    {
      AreEqualFloat3(
        {0.0f, 0.0f, 0.0f},
        NeuronCore::DebugViewColor(static_cast<DebugView>(view), NeuronCore::NO_VOXEL, {0.0f, 0.0f, 1.0f}, {0.5f, 0.25f, 1.0f}),
        L"a pixel no voxel covers");
    }
  }

  TEST_METHOD(ShowsWhatEachViewNames)
  {
    const Float3 normal{0.0f, -1.0f, 0.0f};
    const Float3 albedo{0.5f, 0.25f, 1.0f};
    AreEqualFloat3(albedo, NeuronCore::DebugViewColor(DebugView::Albedo, 7u, normal, albedo), L"albedo");
    AreEqualFloat3({0.5f, 0.0f, 0.5f}, NeuronCore::DebugViewColor(DebugView::Normal, 7u, normal, albedo), L"normal");

    const std::uint32_t hash = NeuronCore::PcgHash(7u);
    const Float3 expected =
      Float3{static_cast<float>(hash & 0xFFu), static_cast<float>((hash >> 8u) & 0xFFu), static_cast<float>((hash >> 16u) & 0xFFu)} /
      Float3{255.0f, 255.0f, 255.0f};
    AreEqualFloat3(expected, NeuronCore::DebugViewColor(DebugView::VoxelIndex, 7u, normal, albedo), L"voxel index");

    // The shadow map is not the visibility buffer's to show: its view comes from ShadowMapViewColor.
    AreEqualFloat3({0.0f, 0.0f, 0.0f}, NeuronCore::DebugViewColor(DebugView::ShadowMap, 7u, normal, albedo), L"shadow map");
    AreEqualFloat3({0.25f, 0.25f, 0.25f}, NeuronCore::ShadowMapViewColor(0.25f), L"a depth as gray");
  }

  // §11: black for no invocation, the ramp's five colors exactly at 1, 3, 8, 24 and 64, a mix between them, and white
  // above the saturation.
  TEST_METHOD(OverdrawRampsFromBlueToRed)
  {
    AreEqualFloat3({0.0f, 0.0f, 0.0f}, NeuronCore::OverdrawViewColor(0), L"no invocation");
    AreEqualFloat3({0.0f, 0.0f, 1.0f}, NeuronCore::OverdrawViewColor(1), L"one: blue");
    AreEqualFloat3({0.0f, 4.0f / 6.0f, 1.0f}, NeuronCore::OverdrawViewColor(2), L"two: two thirds of the way to cyan");
    AreEqualFloat3({0.0f, 1.0f, 1.0f}, NeuronCore::OverdrawViewColor(3), L"three: cyan");
    AreEqualFloat3({0.0f, 1.0f, 0.0f}, NeuronCore::OverdrawViewColor(8), L"eight: green");
    AreEqualFloat3({1.0f, 1.0f, 0.0f}, NeuronCore::OverdrawViewColor(24), L"24: yellow");
    AreEqualFloat3({1.0f, 0.0f, 0.0f}, NeuronCore::OverdrawViewColor(NeuronCore::OVERDRAW_VIEW_SATURATION), L"the saturation: red");
    AreEqualFloat3({1.0f, 1.0f, 1.0f}, NeuronCore::OverdrawViewColor(NeuronCore::OVERDRAW_VIEW_SATURATION + 1), L"above it: white");
    AreEqualFloat3({0.0f, 0.0f, 0.0f}, NeuronCore::DebugViewColor(DebugView::Overdraw, 7u, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}),
                   L"the visibility buffer shows nothing in the overdraw view");
  }

  // The map as a square as tall as the view, in the middle of a 16:9 view: the first and last pixels of the square reach
  // the first and last texels, and the bands either side show nothing.
  TEST_METHOD(ShowsTheShadowMapAsACenteredSquare)
  {
    std::uint32_t texelX = 0;
    std::uint32_t texelY = 0;
    Assert::IsFalse(NeuronCore::ShadowMapViewTexel(419, 500, 1920, 1080, 4096, 4096, texelX, texelY), L"left of the square");
    Assert::IsFalse(NeuronCore::ShadowMapViewTexel(1500, 500, 1920, 1080, 4096, 4096, texelX, texelY), L"right of the square");

    Assert::IsTrue(NeuronCore::ShadowMapViewTexel(420, 0, 1920, 1080, 4096, 4096, texelX, texelY), L"the first pixel");
    Assert::AreEqual(1u, texelX, L"the texel under its centre, 0.5 × 4096 / 1080");
    Assert::AreEqual(1u, texelY);
    Assert::IsTrue(NeuronCore::ShadowMapViewTexel(1499, 1079, 1920, 1080, 4096, 4096, texelX, texelY), L"the last pixel");
    Assert::AreEqual(4094u, texelX, L"1079.5 × 4096 / 1080");
    Assert::AreEqual(4094u, texelY);

    // A tall view centres the square vertically instead.
    Assert::IsFalse(NeuronCore::ShadowMapViewTexel(50, 49, 100, 300, 4096, 4096, texelX, texelY), L"above the square");
    Assert::IsTrue(NeuronCore::ShadowMapViewTexel(50, 150, 100, 300, 4096, 4096, texelX, texelY), L"the square's middle pixel");
    Assert::AreEqual(2068u, texelX, L"50.5 × 4096 / 100");
    Assert::AreEqual(2068u, texelY, L"the square starts 100 rows down");
  }

  // The exact sRGB curve: linear below 0.04045, a 2.4 power above, and the ends exact.
  TEST_METHOD(ConvertsSrgbByTheExactCurve)
  {
    Assert::AreEqual(0.0f, NeuronCore::SrgbToLinear(0));
    Assert::AreEqual(1.0f, NeuronCore::SrgbToLinear(255));
    Assert::AreEqual(static_cast<float>(10.0 / 255.0 / 12.92), NeuronCore::SrgbToLinear(10), L"the linear segment");
    Assert::AreEqual(0.0033465358f, NeuronCore::SrgbToLinear(11), 1.0e-9f, L"just above the segment");
    Assert::AreEqual(0.2158605f, NeuronCore::SrgbToLinear(128), 1.0e-7f, L"the middle byte");

    float previous = -1.0f;
    for (std::uint32_t encoded = 0; encoded < 256u; ++encoded)
    {
      const float linear = NeuronCore::SrgbToLinear(static_cast<std::uint8_t>(encoded));
      Assert::IsTrue(linear > previous, L"the curve rises with every byte");
      previous = linear;
    }
  }
};

} // namespace NeuronCoreTests
