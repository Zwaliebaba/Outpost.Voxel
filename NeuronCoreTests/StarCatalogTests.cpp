#include "pch.h"

#include "ColorSpace.h"
#include "Float3.h"
#include "Quaternion.h"
#include "RigidTransform.h"
#include "StarCatalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::StarRecord;

// The galaxy's frame as the world's, so that a direction's y is the sine of its galactic latitude; and the sector's
// default plane, turned 60 degrees about x.
constexpr NeuronCore::Quaternion GALAXY_AS_WORLD{0.0f, 0.0f, 0.0f, 1.0f};
constexpr NeuronCore::Quaternion TILTED_PLANE{0.5f, 0.0f, 0.0f, 0.8660254f};

[[nodiscard]] double Magnitude(const StarRecord& _star) noexcept
{
  return -2.5 * std::log10(static_cast<double>(_star.flux));
}

// The temperatures a sweep takes: every 250 K from the coolest to the hottest.
constexpr std::uint32_t KELVIN_STEPS = 110;

[[nodiscard]] float Kelvin(std::uint32_t _step) noexcept
{
  return NeuronCore::STAR_COOLEST_KELVIN + 250.0f * static_cast<float>(_step);
}

// The law: stars brighter than _magnitude over the whole sky.
[[nodiscard]] double BrighterThan(double _magnitude) noexcept
{
  return NeuronCore::STAR_REFERENCE_COUNT *
         std::pow(10.0, NeuronCore::STAR_COUNT_SLOPE * (_magnitude - NeuronCore::STAR_REFERENCE_MAGNITUDE));
}

} // namespace

// The star catalog of Design/SpaceScene.md §11.2, which runs only on the CPU, against §15's list.
TEST_CLASS(StarCatalogTests)
{
public:
  TEST_METHOD(RepeatsItsSeed)
  {
    const std::vector<StarRecord> first = NeuronCore::MakeStarCatalog(7, TILTED_PLANE, NeuronCore::STAR_COUNT);
    const std::vector<StarRecord> second = NeuronCore::MakeStarCatalog(7, TILTED_PLANE, NeuronCore::STAR_COUNT);
    Assert::AreEqual(std::size_t{NeuronCore::STAR_COUNT}, first.size());
    Assert::AreEqual(first.size(), second.size());
    Assert::IsTrue(std::memcmp(first.data(), second.data(), first.size() * sizeof(StarRecord)) == 0, L"the same seed, the same bytes");
    const std::vector<StarRecord> other = NeuronCore::MakeStarCatalog(8, TILTED_PLANE, NeuronCore::STAR_COUNT);
    Assert::IsFalse(std::memcmp(first.data(), other.data(), first.size() * sizeof(StarRecord)) == 0, L"another seed, another sky");
  }

  // The counts per magnitude follow the law, within four standard deviations of a Poisson count, and none is brighter
  // than the brightest magnitude or fainter than the faintest the count reaches.
  TEST_METHOD(FollowsTheMagnitudeLaw)
  {
    const std::vector<StarRecord> stars = NeuronCore::MakeStarCatalog(1, GALAXY_AS_WORLD, NeuronCore::STAR_COUNT);
    const double faintest = NeuronCore::FaintestStarMagnitude(NeuronCore::STAR_COUNT);
    Assert::AreEqual(7.2199, faintest, 1.0e-3, L"20,000 stars reach magnitude 7.2");
    // Magnitude bins a whole magnitude wide, from -2 up: the first holds every star brighter than -1, and the last every
    // one fainter than 6.
    constexpr std::size_t BINS = 9;
    std::array<double, BINS> counts{};
    for (const StarRecord& star : stars)
    {
      const double magnitude = Magnitude(star);
      Assert::IsTrue(magnitude >= NeuronCore::STAR_BRIGHTEST_MAGNITUDE - 1.0e-5 && magnitude <= faintest + 1.0e-5,
                     std::format(L"magnitude {}", magnitude).c_str());
      counts[std::min(static_cast<std::size_t>(std::floor(magnitude + 2.0)), BINS - 1)] += 1.0;
    }
    for (std::size_t bin = 0; bin < BINS; ++bin)
    {
      const double lower = static_cast<double>(bin) - 2.0;
      const double upper = bin + 1 == BINS ? faintest : lower + 1.0;
      const double expected = bin == 0 ? BrighterThan(upper) : BrighterThan(upper) - BrighterThan(lower);
      Assert::AreEqual(expected, counts[bin], 4.0 * std::sqrt(expected) + 2.0,
                       std::format(L"magnitudes {} to {}: {} stars", lower, upper, counts[bin]).c_str());
    }
  }

  // Bright stars fall almost evenly over the sky, as evenly spread ones would, a fifth of them within a sine of 0.2 of
  // the plane; faint ones gather toward the plane, and toward the core more than away from it.
  TEST_METHOD(GathersFaintStarsTowardThePlane)
  {
    const std::vector<StarRecord> stars = NeuronCore::MakeStarCatalog(2, GALAXY_AS_WORLD, NeuronCore::STAR_COUNT);
    double bright = 0.0;
    double brightNear = 0.0;
    double faint = 0.0;
    double faintNear = 0.0;
    double faintCore = 0.0;
    double faintAnticenter = 0.0;
    for (const StarRecord& star : stars)
    {
      const double magnitude = Magnitude(star);
      const bool near = std::abs(star.direction.y) < 0.2f;
      if (magnitude < 3.0)
      {
        bright += 1.0;
        brightNear += near ? 1.0 : 0.0;
      }
      else if (magnitude > 6.0)
      {
        faint += 1.0;
        faintNear += near ? 1.0 : 0.0;
        faintCore += star.direction.x > 0.5f ? 1.0 : 0.0;
        faintAnticenter += star.direction.x < -0.5f ? 1.0 : 0.0;
      }
    }
    Logger::WriteMessage(
      std::format(L"near the plane: {} of {} bright stars and {} of {} faint ones\n", brightNear, bright, faintNear, faint).c_str());
    Assert::AreEqual(0.2, brightNear / bright, 0.06, L"bright stars, almost evenly");
    Assert::IsTrue(faintNear / faint > brightNear / bright + 0.2, L"faint stars, gathered toward the plane");
    Assert::IsTrue(faintCore > 1.3 * faintAnticenter, L"and toward the core");
  }

  // The catalog is made in the galaxy's frame and turned into the world by the plane's rotation.
  TEST_METHOD(TurnsTheGalaxyIntoTheWorld)
  {
    const std::vector<StarRecord> galactic = NeuronCore::MakeStarCatalog(3, GALAXY_AS_WORLD, 2000);
    const std::vector<StarRecord> tilted = NeuronCore::MakeStarCatalog(3, TILTED_PLANE, 2000);
    const NeuronCore::Rotation rotation = NeuronCore::RotationOf(TILTED_PLANE);
    for (std::size_t i = 0; i < galactic.size(); ++i)
    {
      const Float3 expected = NeuronCore::RotateVector(rotation, galactic[i].direction);
      Assert::AreEqual(expected.x, tilted[i].direction.x, 1.0e-6f);
      Assert::AreEqual(expected.y, tilted[i].direction.y, 1.0e-6f);
      Assert::AreEqual(expected.z, tilted[i].direction.z, 1.0e-6f);
      Assert::AreEqual(galactic[i].flux, tilted[i].flux);
      Assert::AreEqual(galactic[i].color.x, tilted[i].color.x);
    }
  }

  // Every direction is a unit vector, and every color in gamut, no channel below zero, at unit luminance.
  TEST_METHOD(ColorsAreInGamutAtUnitLuminance)
  {
    for (const StarRecord& star : NeuronCore::MakeStarCatalog(4, TILTED_PLANE, NeuronCore::STAR_COUNT))
    {
      Assert::AreEqual(1.0f, NeuronCore::Length(star.direction), 1.0e-6f, L"a unit direction");
      Assert::IsTrue(star.color.x >= 0.0f && star.color.y >= 0.0f && star.color.z >= 0.0f, L"in gamut");
      Assert::AreEqual(1.0f, NeuronCore::Luminance(star.color), 1.0e-5f, L"unit luminance");
    }
    for (std::uint32_t step = 0; step <= KELVIN_STEPS; ++step)
    {
      const float kelvin = Kelvin(step);
      const Float3 color = NeuronCore::StarColor(kelvin);
      Assert::IsTrue(color.x >= 0.0f && color.y >= 0.0f && color.z >= 0.0f, std::format(L"{} K in gamut", kelvin).c_str());
      Assert::AreEqual(1.0f, NeuronCore::Luminance(color), 1.0e-5f, std::format(L"{} K at unit luminance", kelvin).c_str());
    }
  }

  // A cool star is red, a hot one blue, and one at the sun's temperature close to white; between, blue gains on red as
  // the temperature rises. The saturation keeps every difference slight.
  TEST_METHOD(ColorsFollowTheTemperature)
  {
    const Float3 cool = NeuronCore::StarColor(NeuronCore::STAR_COOLEST_KELVIN);
    const Float3 sun = NeuronCore::StarColor(5800.0f);
    const Float3 hot = NeuronCore::StarColor(NeuronCore::STAR_HOTTEST_KELVIN);
    Assert::IsTrue(cool.x > cool.y && cool.y > cool.z, L"cool: red over blue");
    Assert::IsTrue(hot.z > hot.y && hot.y > hot.x, L"hot: blue over red");
    Assert::AreEqual(1.0f, sun.x, 0.1f, L"the sun's temperature, near white");
    Assert::AreEqual(1.0f, sun.z, 0.1f, L"the sun's temperature, near white");
    Assert::IsTrue(cool.z > 0.6f && hot.x > 0.6f, L"slight differences");
    float ratio = 0.0f;
    for (std::uint32_t step = 0; step <= KELVIN_STEPS; ++step)
    {
      const float kelvin = Kelvin(step);
      const Float3 color = NeuronCore::StarColor(kelvin);
      Assert::IsTrue(color.z / color.x > ratio, std::format(L"bluer at {} K", kelvin).c_str());
      ratio = color.z / color.x;
    }
  }
};

} // namespace NeuronCoreTests
