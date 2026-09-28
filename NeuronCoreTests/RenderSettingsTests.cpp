#include "pch.h"

#include "ColorSpace.h"
#include "Float3.h"
#include "RenderSettings.h"
#include "VoxModel.h"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::RenderSettings;
using NeuronCore::VoxAttributes;

constexpr float RADIANS_PER_DEGREE = 0.0174532925f;

void AreEqualFloat3(Float3 _expected, Float3 _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _what);
  Assert::AreEqual(_expected.y, _actual.y, _what);
  Assert::AreEqual(_expected.z, _actual.z, _what);
}

void AreEqualSettings(const RenderSettings& _expected, const RenderSettings& _actual)
{
  Assert::AreEqual(_expected.sunElevationRadians, _actual.sunElevationRadians, L"sun elevation");
  Assert::AreEqual(_expected.sunAzimuthRadians, _actual.sunAzimuthRadians, L"sun azimuth");
  AreEqualFloat3(_expected.sunColor, _actual.sunColor, L"sun color");
  Assert::AreEqual(_expected.sunIntensity, _actual.sunIntensity, L"sun intensity");
  AreEqualFloat3(_expected.skyColor, _actual.skyColor, L"sky color");
  Assert::AreEqual(_expected.skyIntensity, _actual.skyIntensity, L"sky intensity");
  Assert::AreEqual(_expected.groundVisible, _actual.groundVisible, L"ground visible");
  AreEqualFloat3(_expected.groundColor, _actual.groundColor, L"ground color");
  AreEqualFloat3(_expected.backgroundColor, _actual.backgroundColor, L"background color");
  Assert::AreEqual(_expected.exposure, _actual.exposure, L"exposure");
}

} // namespace

// How the lighting reads a scene's render objects (Design/Archive/SampleRenderer.md §3, §11, Design/ADR/ADR-008).
TEST_CLASS(RenderSettingsTests)
{
public:
  // The defaults are the station's own settings, so a file that says nothing is lit as the station is.
  TEST_METHOD(DefaultsAreTheStationsSettings)
  {
    const RenderSettings settings = NeuronCore::DefaultRenderSettings();
    Assert::AreEqual(50.0f * RADIANS_PER_DEGREE, settings.sunElevationRadians);
    Assert::AreEqual(50.0f * RADIANS_PER_DEGREE, settings.sunAzimuthRadians);
    AreEqualFloat3({1.0f, 1.0f, 1.0f}, settings.sunColor, L"a white sun");
    Assert::AreEqual(0.7f, settings.sunIntensity);
    AreEqualFloat3({1.0f, 1.0f, 1.0f}, settings.skyColor, L"a white sky");
    Assert::AreEqual(0.7f, settings.skyIntensity);
    Assert::IsTrue(settings.groundVisible);
    const float ground = NeuronCore::SrgbToLinear(80);
    AreEqualFloat3({ground, ground, ground}, settings.groundColor, L"ground 80 80 80");
    AreEqualFloat3({0.0f, 0.0f, 0.0f}, settings.backgroundColor, L"a black background");
    Assert::AreEqual(1.0f, settings.exposure);
    AreEqualSettings(settings, NeuronCore::ReadRenderSettings({}));
  }

  TEST_METHOD(ReadsEveryValueItKnows)
  {
    const std::vector<VoxAttributes> objects{
      {{"_type", "_inf"}, {"_i", "1.5"}, {"_k", "255 0 128"}, {"_angle", "30 120"}, {"_area", "0.07"}},
      {{"_type", "_uni"}, {"_i", "0.25"}, {"_k", "0 255 0"}},
      {{"_type", "_film"}, {"_expo", "2"}, {"_aces", "1"}},
      {{"_type", "_ground"}, {"_color", "10 20 30"}},
      {{"_type", "_bg"}, {"_color", "255 255 255"}},
      {{"_type", "_setting"}, {"_ground", "0"}},
    };
    const RenderSettings settings = NeuronCore::ReadRenderSettings(objects);
    Assert::AreEqual(30.0f * RADIANS_PER_DEGREE, settings.sunElevationRadians, L"the first angle is the elevation");
    Assert::AreEqual(120.0f * RADIANS_PER_DEGREE, settings.sunAzimuthRadians, L"the second is the azimuth");
    AreEqualFloat3({1.0f, 0.0f, NeuronCore::SrgbToLinear(128)}, settings.sunColor, L"the sun's color, sRGB-decoded");
    Assert::AreEqual(1.5f, settings.sunIntensity);
    AreEqualFloat3({0.0f, 1.0f, 0.0f}, settings.skyColor, L"the sky's color");
    Assert::AreEqual(0.25f, settings.skyIntensity);
    Assert::AreEqual(2.0f, settings.exposure);
    AreEqualFloat3({NeuronCore::SrgbToLinear(10), NeuronCore::SrgbToLinear(20), NeuronCore::SrgbToLinear(30)}, settings.groundColor,
                   L"the ground's color");
    AreEqualFloat3({1.0f, 1.0f, 1.0f}, settings.backgroundColor, L"the background");
    Assert::IsFalse(settings.groundVisible, L"_setting _ground 0 hides the ground");
  }

  // A value in a form the reader does not take keeps the station's value: the scene still renders, as the station.
  TEST_METHOD(KeepsTheDefaultForWhatItCannotRead)
  {
    const std::vector<VoxAttributes> objects{
      {{"_type", "_inf"}, {"_i", "-1"}, {"_k", "255 255"}, {"_angle", "50"}},
      {{"_type", "_uni"}, {"_i", "bright"}, {"_k", "256 0 0"}},
      {{"_type", "_film"}, {"_expo", "1 2"}},
      {{"_type", "_ground"}, {"_color", "1.5 2 3"}},
      {{"_type", "_bg"}, {"_color", "0 0 0 0"}},
      {{"_type", "_setting"}, {"_ground", ""}},
    };
    AreEqualSettings(NeuronCore::DefaultRenderSettings(), NeuronCore::ReadRenderSettings(objects));
  }

  TEST_METHOD(ToleratesSpacesAroundNumbers)
  {
    const std::vector<VoxAttributes> objects{{{"_type", "_inf"}, {"_angle", "  10   20 "}}};
    const RenderSettings settings = NeuronCore::ReadRenderSettings(objects);
    Assert::AreEqual(10.0f * RADIANS_PER_DEGREE, settings.sunElevationRadians);
    Assert::AreEqual(20.0f * RADIANS_PER_DEGREE, settings.sunAzimuthRadians);
  }
};

} // namespace NeuronCoreTests
