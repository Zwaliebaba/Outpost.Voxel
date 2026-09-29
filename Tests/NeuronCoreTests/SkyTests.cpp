#include "pch.h"

#include "ColorSpace.h"
#include "Float3.h"
#include "Lighting.h"
#include "Message.h"
#include "PerspectiveView.h"
#include "Quaternion.h"
#include "SeededRandom.h"
#include "Sky.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float2;
using NeuronCore::Float3;
using NeuronCore::SkyParameters;

constexpr float RADIANS_PER_DEGREE = 0.0174532925f;

// A world like the sector's defaults: the sun at 50 and 50 degrees at 0.7, 0.27 degrees across, and the galaxy's plane
// turned 60 degrees about x.
[[nodiscard]] NeuronCore::WorldSettings TestWorld() noexcept
{
  const float sunRadians = 50.0f * RADIANS_PER_DEGREE;
  return {NeuronCore::SunDirection(sunRadians, sunRadians),
          {0.7f, 0.7f, 0.7f},
          0.27f * RADIANS_PER_DEGREE,
          {0.05f, 0.05f, 0.05f},
          {0.05f, 0.05f, 0.05f},
          11,
          {0.5f, 0.0f, 0.0f, 0.8660254f}};
}

[[nodiscard]] bool IsFiniteAndNotNegative(Float3 _color) noexcept
{
  return std::isfinite(_color.x) && std::isfinite(_color.y) && std::isfinite(_color.z) && _color.x >= 0.0f && _color.y >= 0.0f &&
         _color.z >= 0.0f;
}

// _count directions spread evenly over the sphere, on a Fibonacci spiral.
[[nodiscard]] std::vector<Float3> SphereDirections(std::uint32_t _count)
{
  std::vector<Float3> directions;
  directions.reserve(_count);
  const double golden = std::numbers::pi * (3.0 - std::sqrt(5.0));
  for (std::uint32_t i = 0; i < _count; ++i)
  {
    const double y = 1.0 - 2.0 * (static_cast<double>(i) + 0.5) / static_cast<double>(_count);
    const double across = std::sqrt(1.0 - y * y);
    const double angle = golden * static_cast<double>(i);
    directions.push_back(
      {static_cast<float>(across * std::cos(angle)), static_cast<float>(y), static_cast<float>(across * std::sin(angle))});
  }
  return directions;
}

// The galaxy's direction at a latitude and longitude, from the core toward +Z.
[[nodiscard]] Float3 Galactic(double _latitude, double _longitude) noexcept
{
  return {static_cast<float>(std::cos(_latitude) * std::cos(_longitude)), static_cast<float>(std::sin(_latitude)),
          static_cast<float>(std::cos(_latitude) * std::sin(_longitude))};
}

} // namespace

// The sky's twins of Design/Archive/SpaceScene.md §11 against §15's list: the GPU side is compared with them in NeuronClientTests.
TEST_CLASS(SkyTests)
{
public:
  // The sun is bright enough that its disc gives the lighting's irradiance; the galaxy takes the plane's rotation and
  // the sky's seed.
  TEST_METHOD(TakesItsParametersFromTheWorld)
  {
    const NeuronCore::WorldSettings world = TestWorld();
    const SkyParameters sky = NeuronCore::MakeSkyParameters(world);
    const float radius = world.sunAngularRadiusRadians;
    const float darkenedSolidAngle = std::numbers::pi_v<float> * radius * radius * (1.0f - NeuronCore::SUN_LIMB_DARKENING / 3.0f);
    Assert::AreEqual(0.7f, sky.sunRadiance.x * darkenedSolidAngle, 1.0e-6f, L"E = L π R² (1 - u / 3)");
    Assert::AreEqual(radius, sky.sunAngularRadiusRadians);
    const NeuronCore::Rotation galaxy = NeuronCore::RotationOf(world.galacticPlane);
    Assert::AreEqual(galaxy.axisY.y, sky.galaxy.axisY.y);
    Assert::AreEqual(galaxy.axisY.z, sky.galaxy.axisY.z);
    Assert::AreEqual(world.skySeed, sky.seed);
    Assert::AreEqual(NeuronCore::GALAXY_GAIN, sky.galaxyGain);
    Assert::AreEqual(NeuronCore::STAR_GAIN, sky.starGain);
  }

  // Finite and never negative everywhere: over the whole sphere, at the galactic poles, along the axes, at the exact sun
  // direction and opposite it.
  TEST_METHOD(IsFiniteEverywhere)
  {
    const SkyParameters sky = NeuronCore::MakeSkyParameters(TestWorld());
    std::vector<Float3> directions = SphereDirections(20000);
    for (const Float3 axis : {Float3{1.0f, 0.0f, 0.0f}, Float3{0.0f, 1.0f, 0.0f}, Float3{0.0f, 0.0f, 1.0f}, sky.galaxy.axisX,
                              sky.galaxy.axisY, sky.galaxy.axisZ, sky.toSun})
    {
      directions.push_back(axis);
      directions.push_back(-axis);
    }
    for (const Float3 direction : directions)
    {
      const std::wstring what = std::format(L"toward ({}, {}, {})", direction.x, direction.y, direction.z);
      Assert::IsTrue(IsFiniteAndNotNegative(NeuronCore::GalaxyRadiance(direction, sky.seed)), (what + L", the galaxy").c_str());
      Assert::IsTrue(IsFiniteAndNotNegative(NeuronCore::SunRadiance(direction, sky, 0.001f)), (what + L", the sun").c_str());
    }
    const Float3 sun = NeuronCore::SunRadiance(sky.toSun, sky, 0.001f);
    Assert::AreEqual(sky.sunRadiance.x, sun.x, L"the disc's middle, at its full radiance");

    // Through a view, from pixels looking at the sun and at each pole.
    for (const Float3 forward : {sky.toSun, sky.galaxy.axisY, -sky.galaxy.axisY})
    {
      const NeuronCore::PerspectiveView view =
        NeuronCore::MakePerspectiveView({0.0f, 0.0f, 0.0f}, forward, {0.0f, 1.0f, 0.0f}, 45.0f * RADIANS_PER_DEGREE, 0.1f, 33, 17);
      for (std::uint32_t y = 0; y < view.heightPixels; ++y)
      {
        for (std::uint32_t x = 0; x < view.widthPixels; ++x)
        {
          Assert::IsTrue(IsFiniteAndNotNegative(NeuronCore::SkyPixel(view, x, y, sky)), std::format(L"pixel {} {}", x, y).c_str());
        }
      }
    }
  }

  // The galaxy is a function of the direction alone, so it has no seam where its longitude wraps, at the anticentre, or
  // anywhere else: directions a millionth of a radian apart see nearly the same.
  TEST_METHOD(HasNoSeamWhereLongitudeWraps)
  {
    constexpr std::uint32_t SEED = 5;
    constexpr double APART = 1.0e-6;
    for (int step = -120; step <= 120; ++step)
    {
      const double latitude = 0.005 * step;
      const Float3 east = NeuronCore::GalaxyRadiance(Galactic(latitude, std::numbers::pi - APART), SEED);
      const Float3 west = NeuronCore::GalaxyRadiance(Galactic(latitude, -std::numbers::pi + APART), SEED);
      const float scale = NeuronCore::Luminance(east) + NeuronCore::Luminance(west);
      Assert::AreEqual(NeuronCore::Luminance(east), NeuronCore::Luminance(west), 1.0e-3f * scale + 1.0e-12f,
                       std::format(L"across the anticentre at latitude {}", latitude).c_str());
    }
    SeededRandom random(17);
    for (std::uint32_t i = 0; i < 20000; ++i)
    {
      const double latitude = random.Uniform(-0.7f, 0.7f);
      const double longitude = random.Uniform(-3.2f, 3.2f);
      const Float3 here = NeuronCore::GalaxyRadiance(Galactic(latitude, longitude), SEED);
      const Float3 there = NeuronCore::GalaxyRadiance(Galactic(latitude + APART, longitude + APART), SEED);
      const float scale = NeuronCore::Luminance(here) + NeuronCore::Luminance(there);
      Assert::AreEqual(NeuronCore::Luminance(here), NeuronCore::Luminance(there), 1.0e-3f * scale + 1.0e-12f,
                       std::format(L"at latitude {}, longitude {}", latitude, longitude).c_str());
    }
  }

  // The band: brighter in the plane than away from it, and brighter toward the core than away from it.
  TEST_METHOD(ShinesAlongThePlaneAndTowardTheCore)
  {
    constexpr std::uint32_t SEED = 9;
    double plane = 0.0;
    double high = 0.0;
    double core = 0.0;
    double anticenter = 0.0;
    for (int step = -314; step < 314; ++step)
    {
      const double longitude = 0.01 * step;
      const double inPlane = NeuronCore::Luminance(NeuronCore::GalaxyRadiance(Galactic(0.05, longitude), SEED));
      plane += inPlane;
      high += NeuronCore::Luminance(NeuronCore::GalaxyRadiance(Galactic(1.0, longitude), SEED));
      if (std::abs(longitude) < 1.5)
      {
        core += inPlane;
      }
      else
      {
        anticenter += inPlane;
      }
    }
    Logger::WriteMessage(
      std::format(L"in the plane {}, away from it {}; toward the core {}, away from it {}\n", plane, high, core, anticenter).c_str());
    Assert::IsTrue(plane > 100.0 * high, L"the band stands out from the sky away from it");
    Assert::IsTrue(core > 1.5 * anticenter, L"and brightens toward the core");
  }

  // §11.4: summed over the directions it covers, the sun's disc gives the lighting's irradiance.
  TEST_METHOD(SunGivesTheLightingsIrradiance)
  {
    const SkyParameters sky = NeuronCore::MakeSkyParameters(TestWorld());
    const float radius = sky.sunAngularRadiusRadians;
    Float3 right{};
    Float3 up{};
    NeuronCore::MakeViewBasis(sky.toSun, {0.0f, 1.0f, 0.0f}, right, up);
    constexpr int STEPS = 400;
    const double step = 3.0 * radius / STEPS;
    double irradiance = 0.0;
    for (int j = -STEPS / 2; j < STEPS / 2; ++j)
    {
      for (int i = -STEPS / 2; i < STEPS / 2; ++i)
      {
        const auto across = static_cast<float>((i + 0.5) * step);
        const auto along = static_cast<float>((j + 0.5) * step);
        const Float3 direction = NeuronCore::Normalize(sky.toSun + right * across + up * along);
        irradiance += NeuronCore::SunRadiance(direction, sky, radius * 0.01f).x * step * step;
      }
    }
    Assert::AreEqual(0.7, irradiance, 0.007, L"the lighting's sun, within a percent");
  }

  // Within Abramowitz and Stegun's 1.5e-7 and single precision's rounding: at most 6.3e-7 from 0 to 6.4 in steps of a
  // millionth, measured by a throwaway program, and 5.4e-7 with fused multiply-adds.
  TEST_METHOD(ErfcMatchesTheReference)
  {
    for (int i = 0; i <= 400; ++i)
    {
      const float x = static_cast<float>(i) / 64.0f;
      Assert::AreEqual(std::erfc(static_cast<double>(x)), static_cast<double>(NeuronCore::Erfc(x)), 1.0e-6,
                       std::format(L"erfc({})", x).c_str());
    }
  }

  // §15: a star's shares of its light, summed over its pixels, come to one wherever in its pixel it falls: along an
  // axis, and over the pixels' squares.
  TEST_METHOD(ConservesAStarsEnergy)
  {
    SeededRandom random(23);
    for (std::uint32_t sample = 0; sample < 64; ++sample)
    {
      const Float2 center{10.0f + random.Uniform(0.0f, 1.0f), 10.0f + random.Uniform(0.0f, 1.0f)};
      double row = 0.0;
      double square = 0.0;
      for (std::uint32_t y = 0; y < 21; ++y)
      {
        row += NeuronCore::StarAxisShare(static_cast<float>(y) + 0.5f - center.y);
        for (std::uint32_t x = 0; x < 21; ++x)
        {
          square += NeuronCore::StarShare({static_cast<float>(x) + 0.5f - center.x, static_cast<float>(y) + 0.5f - center.y});
        }
      }
      Assert::AreEqual(1.0, row, 1.0e-6, std::format(L"along an axis, from {}", center.y).c_str());
      Assert::AreEqual(1.0, square, 1.0e-5, std::format(L"over the square, from ({}, {})", center.x, center.y).c_str());
    }
    Assert::IsTrue(NeuronCore::StarAxisShare(8.0f) >= 0.0f && NeuronCore::StarAxisShare(-8.0f) >= 0.0f, L"never negative in the tails");
  }

  // Every pixel whose centre lies outside a star's quad receives less than the darkest visible value; and a star that
  // lights no pixel that much has no quad.
  TEST_METHOD(QuadHoldsEveryVisiblePixel)
  {
    const float peak = NeuronCore::StarAxisShare(0.0f) * NeuronCore::StarAxisShare(0.0f);
    const float faintest = NeuronCore::STAR_DARKEST_VISIBLE / peak;
    Assert::AreEqual(0.0f, NeuronCore::StarQuadRadius(faintest * 0.999f), L"too faint to draw");
    SeededRandom random(29);
    for (int step = 0; step < 30; ++step)
    {
      const float brightness = faintest * 1.001f * std::pow(1.7f, static_cast<float>(step));
      const float radius = NeuronCore::StarQuadRadius(brightness);
      Assert::IsTrue(radius > 0.5f, std::format(L"a quad for brightness {}", brightness).c_str());
      for (std::uint32_t sample = 0; sample < 16; ++sample)
      {
        const Float2 center{random.Uniform(0.0f, 1.0f), random.Uniform(0.0f, 1.0f)};
        const int reach = static_cast<int>(radius) + 3;
        for (int y = -reach; y <= reach; ++y)
        {
          for (int x = -reach; x <= reach; ++x)
          {
            const Float2 offset{static_cast<float>(x) + 0.5f - center.x, static_cast<float>(y) + 0.5f - center.y};
            if (std::abs(offset.x) >= radius || std::abs(offset.y) >= radius)
            {
              Assert::IsTrue(brightness * NeuronCore::StarShare(offset) < NeuronCore::STAR_DARKEST_VISIBLE,
                             std::format(L"brightness {} at ({}, {})", brightness, offset.x, offset.y).c_str());
            }
          }
        }
      }
    }
  }

  // A star at infinity lies where the ray through a pixel's centre points, whatever the camera's position; one behind
  // the camera or beside it is not drawn.
  TEST_METHOD(ProjectsStarsAtInfinity)
  {
    const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView({3.0f, -2.0f, 40.0f}, {5.0f, 1.0f, 50.0f}, {0.0f, 1.0f, 0.0f},
                                                                             45.0f * RADIANS_PER_DEGREE, 0.1f, 161, 91);
    for (std::uint32_t y = 0; y < view.heightPixels; y += 5)
    {
      for (std::uint32_t x = 0; x < view.widthPixels; x += 5)
      {
        const Float3 direction = NeuronCore::Normalize(NeuronCore::PerspectiveRay(view, x, y).direction);
        const std::optional<Float2> position = NeuronCore::StarPosition(view, direction);
        Assert::IsTrue(position.has_value(), std::format(L"pixel {} {}", x, y).c_str());
        const Float2 at = position.value_or(Float2{-1.0f, -1.0f});
        Assert::AreEqual(static_cast<float>(x) + 0.5f, at.x, 1.0e-3f, std::format(L"pixel {} {}", x, y).c_str());
        Assert::AreEqual(static_cast<float>(y) + 0.5f, at.y, 1.0e-3f, std::format(L"pixel {} {}", x, y).c_str());
      }
    }
    Assert::IsFalse(NeuronCore::StarPosition(view, -view.forward).has_value(), L"behind the camera");
    Assert::IsFalse(NeuronCore::StarPosition(view, view.right).has_value(), L"beside it");
  }

  // What a star's quad adds: its color times its brightness times the pixel's share inside the quad, where a pixel on
  // its left or top edge is inside and one on its right or bottom edge is not; and nothing from a star too faint.
  TEST_METHOD(StarsAddTheirShare)
  {
    const NeuronCore::PerspectiveView view =
      NeuronCore::MakePerspectiveView({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 45.0f * RADIANS_PER_DEGREE, 0.1f, 64, 48);
    const NeuronCore::StarRecord star{{0.0f, 0.0f, 1.0f}, 1.0f, {1.2f, 1.0f, 0.8f}};
    const float brightness = star.flux * 2.0f;
    const float radius = NeuronCore::StarQuadRadius(brightness);
    double sum = 0.0;
    for (std::uint32_t y = 0; y < view.heightPixels; ++y)
    {
      for (std::uint32_t x = 0; x < view.widthPixels; ++x)
      {
        const Float3 added = NeuronCore::StarPixel(view, star, 2.0f, x, y);
        const Float2 offset{static_cast<float>(x) + 0.5f - 32.0f, static_cast<float>(y) + 0.5f - 24.0f};
        const bool inside = offset.x >= -radius && offset.x < radius && offset.y >= -radius && offset.y < radius;
        const float expected = inside ? brightness * NeuronCore::StarShare(offset) : 0.0f;
        Assert::AreEqual(star.color.x * expected, added.x, 1.0e-6f, std::format(L"pixel {} {}", x, y).c_str());
        Assert::AreEqual(star.color.z * expected, added.z, 1.0e-6f, std::format(L"pixel {} {}", x, y).c_str());
        sum += added.y;
      }
    }
    Assert::AreEqual(static_cast<double>(brightness), sum, 1.0e-3 * brightness, L"all but the invisible edge of its light");
    const NeuronCore::StarRecord faint{{0.0f, 0.0f, 1.0f}, 1.0e-4f, {1.0f, 1.0f, 1.0f}};
    Assert::AreEqual(0.0f, NeuronCore::StarPixel(view, faint, 2.0f, 32, 24).y, L"too faint to draw");
  }
};

} // namespace NeuronCoreTests
