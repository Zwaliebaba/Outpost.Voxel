#include "pch.h"

#include "Float3.h"
#include "Lighting.h"
#include "Message.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "TraceHit.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::LightingParameters;

constexpr float RADIANS_PER_DEGREE = 0.0174532925f;
constexpr float TOLERANCE = 1.0e-6f;

// The texels of the 16 × 16 maps below.
constexpr std::size_t MAP_TEXELS = std::size_t{16} * 16;

void AreClose(Float3 _expected, Float3 _actual, float _tolerance, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _tolerance, _what);
  Assert::AreEqual(_expected.y, _actual.y, _tolerance, _what);
  Assert::AreEqual(_expected.z, _actual.z, _tolerance, _what);
}

// Simple, distinct values, so that each term of §11's formula shows where it lands.
[[nodiscard]] LightingParameters TestLighting() noexcept
{
  return {{0.0f, 1.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, {1.0f, 1.0f, 1.0f}, 0.5f, {0.2f, 0.2f, 0.2f}, {0.0f, 0.0f, 0.125f}, 1.0f};
}

// A sun straight overhead on a square of 16 × 16 texels, each a unit across, whose near plane is y = 100 and whose
// depth range is 100: depth 0.5 is the plane y = 50. Its right is +X and its up +Z (MakeViewBasis).
[[nodiscard]] NeuronCore::OrthographicView OverheadView() noexcept
{
  return NeuronCore::MakeOrthographicView({0.0f, 100.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 8.0f, 8.0f, 100.0f, 16, 16);
}

} // namespace

// The lighting pass's twin (Design/Archive/SampleRenderer.md §10, §11). The GPU side is compared with it in NeuronClientTests;
// these pin the twin itself.
TEST_CLASS(LightingTests)
{
public:
  // Design/ADR/ADR-008's reading of MagicaVoxel's _angle: elevation above the horizon, azimuth from MagicaVoxel's -Y towards
  // +X, which in the engine's axes is from -Z towards +X.
  TEST_METHOD(SunDirectionFollowsTheAssumedConvention)
  {
    const float elevation = 30.0f * RADIANS_PER_DEGREE;
    AreClose({0.0f, std::sin(elevation), -std::cos(elevation)}, NeuronCore::SunDirection(elevation, 0.0f), TOLERANCE, L"azimuth 0 is -Z");
    AreClose({std::cos(elevation), std::sin(elevation), 0.0f}, NeuronCore::SunDirection(elevation, 90.0f * RADIANS_PER_DEGREE), TOLERANCE,
             L"azimuth 90 is +X");
    AreClose({0.0f, 1.0f, 0.0f}, NeuronCore::SunDirection(90.0f * RADIANS_PER_DEGREE, 1.0f), TOLERANCE, L"elevation 90 is overhead");
    // The station's 50 50, from an independent double-precision evaluation.
    AreClose({0.492403877f, 0.766044443f, -0.413175911f}, NeuronCore::SunDirection(50.0f * RADIANS_PER_DEGREE, 50.0f * RADIANS_PER_DEGREE),
             TOLERANCE, L"the station's sun");
    Assert::AreEqual(1.0f, NeuronCore::Length(NeuronCore::SunDirection(0.3f, 2.0f)), TOLERANCE, L"a unit vector");
  }

  // ADR-008's mapping: _emit × 2^_flux for an emissive entry, nothing for any other.
  TEST_METHOD(EmitsAsTheMappingSays)
  {
    Assert::AreEqual(2.4f, NeuronCore::EmissiveScale({255, 255, 85, 255, true, 0.6f, 2.0f}), 1.0e-6f, L"the station's _emit 0.6, _flux 2");
    Assert::AreEqual(0.5f, NeuronCore::EmissiveScale({255, 255, 85, 255, true, 0.5f, 0.0f}), L"_flux 0 leaves _emit as it is");
    Assert::AreEqual(0.0f, NeuronCore::EmissiveScale({255, 255, 85, 255, false, 0.6f, 2.0f}), L"not emissive");
  }

  // Design/SpaceScene.md §12.1: the world's sun and hemisphere as they are, over a black background, with the viewer's
  // gain.
  TEST_METHOD(ParametersComeFromTheWorld)
  {
    const NeuronCore::WorldSettings world{{0.0f, 0.6f, 0.8f},      {1.0f, 0.5f, 0.25f}, 0.01f, {0.1f, 0.2f, 0.3f}, {0.2f, 0.4f, 0.6f}, 7,
                                          {0.0f, 0.0f, 0.0f, 1.0f}};
    const LightingParameters lighting = NeuronCore::MakeLightingParameters(world, 3.0f);
    AreClose(world.toSun, lighting.toSun, 0.0f, L"the sun's direction");
    AreClose(world.sunRadiance, lighting.sunRadiance, 0.0f, L"the sun");
    AreClose(world.ambientUpper, lighting.skyColor, 0.0f, L"the ambient's upper color");
    Assert::AreEqual(1.0f, lighting.skyIntensity, L"the colors as they are");
    AreClose(world.ambientLower, lighting.groundColor, 0.0f, L"the ambient's lower color");
    AreClose({0.0f, 0.0f, 0.0f}, lighting.background, 0.0f, L"black under the sky");
    Assert::AreEqual(3.0f, lighting.emissiveGain);
  }

  // §11: ground below, sky above, and half of each on a vertical face.
  TEST_METHOD(AmbientBlendsGroundAndSky)
  {
    const LightingParameters lighting = TestLighting();
    AreClose({0.5f, 0.5f, 0.5f}, NeuronCore::Ambient({0.0f, 1.0f, 0.0f}, lighting), TOLERANCE, L"facing up: the sky");
    AreClose({0.1f, 0.1f, 0.1f}, NeuronCore::Ambient({0.0f, -1.0f, 0.0f}, lighting), TOLERANCE, L"facing down: the ground");
    AreClose({0.3f, 0.3f, 0.3f}, NeuronCore::Ambient({1.0f, 0.0f, 0.0f}, lighting), TOLERANCE, L"facing sideways");
  }

  // §11: C = albedo × (E_sun × max(0, N·S) × shadow + ambient(N)) + albedo × emissive.
  TEST_METHOD(ShadesByTheDesignsFormula)
  {
    LightingParameters lighting = TestLighting();
    const Float3 albedo{0.5f, 0.25f, 1.0f};
    const Float3 up{0.0f, 1.0f, 0.0f};
    AreClose(albedo * 2.5f, NeuronCore::ShadeSurface(albedo, 0.0f, up, 1.0f, lighting), TOLERANCE, L"in the sun: 2 + 0.5");
    AreClose(albedo * 0.5f, NeuronCore::ShadeSurface(albedo, 0.0f, up, 0.0f, lighting), TOLERANCE, L"in shadow: the sky alone");
    AreClose(albedo * 1.5f, NeuronCore::ShadeSurface(albedo, 0.0f, up, 0.5f, lighting), TOLERANCE, L"half shadowed");
    AreClose(albedo * 0.1f, NeuronCore::ShadeSurface(albedo, 0.0f, {0.0f, -1.0f, 0.0f}, 1.0f, lighting), TOLERANCE,
             L"facing away: the ground's light alone");
    AreClose(albedo * 3.0f, NeuronCore::ShadeSurface(albedo, 0.5f, up, 1.0f, lighting), TOLERANCE, L"emissive adds its scale");
    lighting.emissiveGain = 4.0f;
    AreClose(albedo * 4.5f, NeuronCore::ShadeSurface(albedo, 0.5f, up, 1.0f, lighting), TOLERANCE, L"times the gain");
  }

  TEST_METHOD(OffsetsOneAndAHalfTexels)
  {
    Assert::AreEqual(1.5f, NeuronCore::ShadowNormalOffset(OverheadView()), L"a unit per texel");
    const NeuronCore::OrthographicView station =
      NeuronCore::MakeOrthographicView({0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 512.0f, 512.0f, 1.0f, 4096, 4096);
    Assert::AreEqual(0.375f, NeuronCore::ShadowNormalOffset(station), L"the station's map: a quarter unit per texel");
  }

  // §10 on hand-built maps: an empty map lights everything, an occluder shadows what lies beyond it and not what lies
  // before it, the border is lit, and a point past the far plane is lit where the map is empty.
  TEST_METHOD(ShadowFactorComparesAgainstTheMap)
  {
    const NeuronCore::OrthographicView view = OverheadView();
    std::vector<float> depth(MAP_TEXELS, NeuronCore::ORTHOGRAPHIC_FAR_DEPTH);
    const NeuronCore::ShadowMapImage empty{16, 16, depth};
    Assert::AreEqual(1.0f, NeuronCore::ShadowFactor(empty, view, {0.5f, 20.0f, 0.5f}), L"nothing casts a shadow");
    Assert::AreEqual(1.0f, NeuronCore::ShadowFactor(empty, view, {0.5f, -50.0f, 0.5f}), L"past the far plane, lit");

    std::vector<float> roof(MAP_TEXELS, 0.5f);
    const NeuronCore::ShadowMapImage covered{16, 16, roof};
    Assert::AreEqual(0.0f, NeuronCore::ShadowFactor(covered, view, {0.5f, 20.0f, 0.5f}), L"under a roof at y = 50");
    Assert::AreEqual(1.0f, NeuronCore::ShadowFactor(covered, view, {0.5f, 60.0f, 0.5f}), L"above the roof");
    Assert::AreEqual(1.0f, NeuronCore::ShadowFactor(covered, view, {0.5f, 50.0f, 0.5f}), L"on the roof: less or equal passes");
    Assert::AreEqual(1.0f, NeuronCore::ShadowFactor(covered, view, {40.0f, 20.0f, 0.0f}), L"beyond the map, the border is lit");
    Assert::AreEqual(0.0f, NeuronCore::ShadowFactor(covered, view, {0.5f, -50.0f, 0.5f}), L"past the far plane, under the roof");
  }

  // A roof over the left half of the map: a point under the edge sees three of nine taps' worth of sky, more or less,
  // and the fraction rises across the edge, as the linear comparison filter blends it.
  TEST_METHOD(ShadowFactorFiltersAcrossAnEdge)
  {
    const NeuronCore::OrthographicView view = OverheadView();
    std::vector<float> depth(MAP_TEXELS, NeuronCore::ORTHOGRAPHIC_FAR_DEPTH);
    for (std::size_t row = 0; row < 16u; ++row)
    {
      for (std::size_t column = 0; column < 8u; ++column)
      {
        depth[row * 16u + column] = 0.5f;
      }
    }
    const NeuronCore::ShadowMapImage map{16, 16, depth};
    // Texel centres are at half units; world x = 0 is the edge between columns 7 and 8.
    Assert::AreEqual(0.0f, NeuronCore::ShadowFactor(map, view, {-3.5f, 20.0f, 0.5f}), TOLERANCE, L"well under the roof");
    Assert::AreEqual(1.0f, NeuronCore::ShadowFactor(map, view, {3.5f, 20.0f, 0.5f}), TOLERANCE, L"well clear of it");
    Assert::AreEqual(0.5f, NeuronCore::ShadowFactor(map, view, {0.0f, 20.0f, 0.5f}), TOLERANCE, L"on the edge");
    float previous = -1.0f;
    for (std::int32_t step = -16; step <= 16; ++step)
    {
      const float x = static_cast<float>(step) * 0.125f;
      const float lit = NeuronCore::ShadowFactor(map, view, {x, 20.0f, 0.5f});
      Assert::IsTrue(lit >= previous, std::format(L"light rises across the edge at x = {}", x).c_str());
      previous = lit;
    }
  }

  // §11's two cases for a pixel: a voxel at the depth the splat wrote, and the background where no voxel was hit, in
  // every direction, since there is no ground (Design/ADR/ADR-013).
  TEST_METHOD(LightsVoxelAndBackground)
  {
    const LightingParameters lighting = TestLighting();
    const NeuronCore::OrthographicView shadowView = OverheadView();
    std::vector<float> depth(MAP_TEXELS, NeuronCore::ORTHOGRAPHIC_FAR_DEPTH);
    const NeuronCore::ShadowMapImage map{16, 16, depth};
    // Looking straight down from y = 10 at an odd size, so that the centre pixel's ray is the -Y axis.
    const NeuronCore::PerspectiveView view =
      NeuronCore::MakePerspectiveView({0.0f, 10.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.0f, 0.1f, 5, 5);
    const Float3 albedo{0.5f, 0.25f, 1.0f};
    const Float3 up{0.0f, 1.0f, 0.0f};

    const float voxelDepth = NeuronCore::PerspectiveDepth(view, 4.0f);
    AreClose(albedo * 2.5f, NeuronCore::LightPixel(view, 2, 2, 7, up, voxelDepth, albedo, 0.0f, map, shadowView, lighting), TOLERANCE,
             L"a lit voxel");
    AreClose(lighting.background, NeuronCore::LightPixel(view, 2, 2, NeuronCore::NO_VOXEL, {}, 0.0f, {}, 0.0f, map, shadowView, lighting),
             TOLERANCE, L"looking down, the background");

    const NeuronCore::PerspectiveView skyward =
      NeuronCore::MakePerspectiveView({0.0f, 10.0f, 0.0f}, {0.0f, 20.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.0f, 0.1f, 5, 5);
    AreClose(lighting.background,
             NeuronCore::LightPixel(skyward, 2, 2, NeuronCore::NO_VOXEL, {}, 0.0f, {}, 0.0f, map, shadowView, lighting), TOLERANCE,
             L"looking up, the background");

    // A roof between the voxel and the sun shadows it: only the sky's light is left.
    std::vector<float> roof(MAP_TEXELS, 0.5f);
    const NeuronCore::ShadowMapImage covered{16, 16, roof};
    AreClose(albedo * 0.5f, NeuronCore::LightPixel(view, 2, 2, 7, up, voxelDepth, albedo, 0.0f, covered, shadowView, lighting), TOLERANCE,
             L"a voxel in shadow");
  }
};

} // namespace NeuronCoreTests
