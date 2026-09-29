#include "pch.h"

#include "Float3.h"
#include "ToneMap.h"

#include <cstdint>
#include <format>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;

// The twin computes in single precision; the references below are double precision.
constexpr float TOLERANCE = 2.0e-6f;

void AreClose(Float3 _expected, Float3 _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, TOLERANCE, _what);
  Assert::AreEqual(_expected.y, _actual.y, TOLERANCE, _what);
  Assert::AreEqual(_expected.z, _actual.z, TOLERANCE, _what);
}

} // namespace

// The tone map's twin (Design/Archive/SampleRenderer.md §11). The GPU side is compared with it in NeuronClientTests; these pin
// the twin itself.
TEST_CLASS(ToneMapTests)
{
public:
  // Reference values from an independent double-precision implementation of Hill's fit, with his matrices.
  TEST_METHOD(MatchesTheReferenceFit)
  {
    AreClose({0.0f, 0.0f, 0.0f}, NeuronCore::AcesFitted({0.0f, 0.0f, 0.0f}), L"black");
    AreClose({0.105591247f, 0.105591247f, 0.105590191f}, NeuronCore::AcesFitted({0.18f, 0.18f, 0.18f}), L"middle gray");
    AreClose({0.619115427f, 0.619115427f, 0.619109236f}, NeuronCore::AcesFitted({1.0f, 1.0f, 1.0f}), L"white");
    AreClose({0.909013768f, 0.909013768f, 0.909004678f}, NeuronCore::AcesFitted({4.0f, 4.0f, 4.0f}), L"two stops over");
    AreClose({0.688027874f, 0.0f, 0.00263900675f}, NeuronCore::AcesFitted({1.0f, 0.0f, 0.0f}), L"red");
    AreClose({0.101613277f, 0.623658979f, 0.0288437185f}, NeuronCore::AcesFitted({0.0f, 1.0f, 0.0f}), L"green");
    AreClose({0.0f, 0.0f, 0.601758846f}, NeuronCore::AcesFitted({0.0f, 0.0f, 1.0f}), L"blue");
    AreClose({0.366705719f, 0.172954975f, 0.0741483937f}, NeuronCore::AcesFitted({0.5f, 0.25f, 0.125f}), L"a warm color");
  }

  TEST_METHOD(StaysInTheDisplayRange)
  {
    AreClose({1.0f, 1.0f, 1.0f}, NeuronCore::AcesFitted({100.0f, 100.0f, 100.0f}), L"far over");
    float previous = -1.0f;
    for (std::uint32_t step = 0; step <= 256u; ++step)
    {
      const float gray = static_cast<float>(step) * 0.0625f;
      const float mapped = NeuronCore::AcesFitted({gray, gray, gray}).x;
      Assert::IsTrue(mapped >= previous && mapped <= 1.0f, std::format(L"gray {} maps to {}", gray, mapped).c_str());
      previous = mapped;
    }
  }

  TEST_METHOD(AppliesTheExposureFirst)
  {
    const Float3 color{0.5f, 0.25f, 0.125f};
    AreClose(NeuronCore::AcesFitted(color * 2.0f), NeuronCore::ToneMap(color, 2.0f), L"exposure 2");
    AreClose(NeuronCore::AcesFitted(color), NeuronCore::ToneMap(color, 1.0f), L"exposure 1 is the identity");
  }
};

} // namespace NeuronCoreTests
