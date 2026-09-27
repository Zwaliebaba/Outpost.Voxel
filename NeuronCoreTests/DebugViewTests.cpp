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
      AreEqualFloat3({0.0f, 0.0f, 0.0f},
                     NeuronCore::DebugViewColor(static_cast<DebugView>(view), NeuronCore::NO_VOXEL, {0.0f, 0.0f, 1.0f}, {0.5f, 0.25f, 1.0f},
                                                {0.0f, 0.0f, -1.0f}),
                     L"a pixel no voxel covers");
    }
  }

  TEST_METHOD(ShowsWhatEachViewNames)
  {
    const Float3 normal{0.0f, -1.0f, 0.0f};
    const Float3 albedo{0.5f, 0.25f, 1.0f};
    const Float3 forward{0.0f, 1.0f, 0.0f};
    AreEqualFloat3(albedo, NeuronCore::DebugViewColor(DebugView::Albedo, 7u, normal, albedo, forward), L"albedo");
    AreEqualFloat3({0.5f, 0.0f, 0.5f}, NeuronCore::DebugViewColor(DebugView::Normal, 7u, normal, albedo, forward), L"normal");

    // A face turned to the camera gets the whole headlight, and one turned away only the quarter that stands in for
    // ambient light.
    AreEqualFloat3(albedo, NeuronCore::DebugViewColor(DebugView::Headlight, 7u, normal, albedo, forward), L"facing the camera");
    AreEqualFloat3(albedo * 0.25f, NeuronCore::DebugViewColor(DebugView::Headlight, 7u, {0.0f, 1.0f, 0.0f}, albedo, forward),
                   L"facing away");

    const std::uint32_t hash = NeuronCore::PcgHash(7u);
    const Float3 expected =
      Float3{static_cast<float>(hash & 0xFFu), static_cast<float>((hash >> 8u) & 0xFFu), static_cast<float>((hash >> 16u) & 0xFFu)} /
      Float3{255.0f, 255.0f, 255.0f};
    AreEqualFloat3(expected, NeuronCore::DebugViewColor(DebugView::VoxelIndex, 7u, normal, albedo, forward), L"voxel index");
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
