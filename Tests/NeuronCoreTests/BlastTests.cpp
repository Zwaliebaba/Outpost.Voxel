#include "pch.h"

#include "SeededRandom.h"

#include "Blast.h"
#include "Float3.h"
#include "Fragmentation.h"
#include "PerspectiveView.h"
#include "Ray.h"
#include "TraceHit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;

[[nodiscard]] float Luminance(Float3 _color) noexcept
{
  return 0.2126f * _color.x + 0.7152f * _color.y + 0.0722f * _color.z;
}

[[nodiscard]] NeuronCore::Blast BlastAt(Float3 _origin, float _extent, float _timeSeconds) noexcept
{
  return {.origin = _origin, .drift = {0.0f, 0.0f, 0.0f}, .drag = 1.0f, .extent = _extent, .seed = 7u, .timeSeconds = _timeSeconds};
}

// A heating detonation in a part's space, about the origin.
[[nodiscard]] NeuronCore::PlacementHeat HeatAt(float _timeSeconds) noexcept
{
  return {.blastOrigin = {0.0f, 0.0f, 0.0f},
          .timeSeconds = _timeSeconds,
          .shockSpeed = 100.0f,
          .heatDistance = 20.0f,
          .coolingRate = 0.8f,
          .firstFragment = 0u};
}

} // namespace

// The light of a detonation (Design/ADR/ADR-025): its hot debris, its flash and its shell of gas, on the CPU. The GPU
// passes are held to these twins by LightingPassTests and GasShellPassTests.
TEST_CLASS(BlastTests)
{
public:
  // The ramp runs from a dull red to near white, each step of unit luminance where no channel was clamped.
  TEST_METHOD(HeatRampRunsFromRedToWhite)
  {
    const NeuronCore::BlastLighting lighting = NeuronCore::MakeBlastLighting({}, {0.0f, 0.0f, 0.0f});
    const Float3 cold = NeuronCore::HeatColor(0.0f, lighting);
    const Float3 hot = NeuronCore::HeatColor(1.0f, lighting);
    Assert::IsTrue(cold.x > cold.y && cold.y >= cold.z, L"the coldest glow is red");
    Assert::IsTrue(std::abs(hot.x - 1.0f) < 0.2f && std::abs(hot.y - 1.0f) < 0.2f && std::abs(hot.z - 1.0f) < 0.25f,
                   L"white heat is near white");
    Assert::AreEqual(1.0f, Luminance(hot), 1.0e-3f, L"of unit luminance");
    float previousBlue = -1.0f;
    for (std::uint32_t step = 0; step <= 64; ++step)
    {
      const Float3 color = NeuronCore::HeatColor(static_cast<float>(step) / 64.0f, lighting);
      Assert::IsTrue(color.x >= 0.0f && color.y >= 0.0f && color.z >= 0.0f, L"no channel below zero");
      Assert::IsTrue(color.z / std::max(color.x, 1.0e-6f) >= previousBlue - 1.0e-5f, L"bluer as it heats");
      previousBlue = color.z / std::max(color.x, 1.0e-6f);
    }
    Assert::AreEqual(0.0f, Luminance(NeuronCore::HeatRadiance(0.0f, lighting)) + Luminance(NeuronCore::HeatRadiance(-1.0f, lighting)),
                     L"no heat, no glow");
    Assert::AreEqual(lighting.heatGain * 0.25f * Luminance(NeuronCore::HeatColor(0.5f, lighting)),
                     Luminance(NeuronCore::HeatRadiance(0.5f, lighting)), 1.0e-4f, L"the gain times the heat squared");
  }

  // A fragment is cold until the blast reaches its pivot, then starts at its distance's heat and cools, a larger fragment
  // slower than a small one.
  TEST_METHOD(FragmentsHeatWhenTheBlastArrivesAndCool)
  {
    const NeuronCore::Fragment lone{{30.0f, 0.0f, 0.0f}, 1.0f};
    const NeuronCore::Fragment chunk{{30.0f, 0.0f, 0.0f}, 0.2f};
    const float arrival = 30.0f / 100.0f;
    Assert::AreEqual(0.0f, NeuronCore::FragmentHeat(lone, HeatAt(0.0f)), L"cold at the detonation");
    Assert::AreEqual(0.0f, NeuronCore::FragmentHeat(lone, HeatAt(arrival * 0.9f)), L"cold before the blast arrives");
    NeuronCore::PlacementHeat none = HeatAt(1.0f);
    none.heatDistance = 0.0f;
    Assert::AreEqual(0.0f, NeuronCore::FragmentHeat(lone, none), L"a heat distance of 0 heats nothing");

    const float initial = 1.0f / (1.0f + 1.5f * 1.5f);
    Assert::AreEqual(initial, NeuronCore::FragmentHeat(lone, HeatAt(arrival + 1.0e-4f)), 1.0e-3f, L"its distance's heat on arrival");
    float previous = 2.0f;
    for (const float since : {0.1f, 0.5f, 1.0f, 3.0f, 8.0f})
    {
      const float heat = NeuronCore::FragmentHeat(lone, HeatAt(arrival + since));
      Assert::AreEqual(initial * std::exp(-0.8f * since), heat, 1.0e-4f, std::format(L"{} s after", since).c_str());
      Assert::IsTrue(heat < previous, L"it cools");
      Assert::IsTrue(NeuronCore::FragmentHeat(chunk, HeatAt(arrival + since)) > heat, L"a larger fragment cools slower");
      previous = heat;
    }
    Assert::IsTrue(NeuronCore::FragmentHeat({{2.0f, 0.0f, 0.0f}, 1.0f}, HeatAt(0.5f)) > NeuronCore::FragmentHeat(lone, HeatAt(0.5f)),
                   L"nearer the blast, hotter");
  }

  // A flash lights a surface facing it by the inverse square of its distance, grown by its softness, and one facing away
  // not at all; it fades within about a second, and the frame keeps the nearest ones.
  TEST_METHOD(FlashesLightWhatFacesThem)
  {
    const NeuronCore::Blast blast = BlastAt({0.0f, 0.0f, 0.0f}, 50.0f, 0.05f);
    const NeuronCore::BlastLighting lighting = NeuronCore::MakeBlastLighting({&blast, 1}, {0.0f, 0.0f, -500.0f});
    Assert::AreEqual(1u, lighting.flashCount);
    const NeuronCore::Flash& flash = lighting.flashes[0];
    const Float3 albedo{0.5f, 0.5f, 0.5f};
    for (const float distance : {10.0f, 50.0f, 200.0f})
    {
      const Float3 lit = NeuronCore::FlashLight({distance, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, albedo, lighting);
      const float squared = distance * distance + flash.softness;
      Assert::AreEqual(0.5f * flash.intensity.x * distance / std::sqrt(squared) / squared, lit.x, 1.0e-4f * lit.x,
                       std::format(L"{} away", distance).c_str());
      const Float3 away = NeuronCore::FlashLight({distance, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, albedo, lighting);
      Assert::IsTrue(away.x == 0.0f && away.y == 0.0f && away.z == 0.0f, L"facing away, unlit");
    }
    Assert::AreEqual(0u, NeuronCore::MakeBlastLighting(std::vector{BlastAt({}, 50.0f, 1.2f)}, {}).flashCount, L"faded by 1.2 s");
    Assert::AreEqual(0u, NeuronCore::MakeBlastLighting(std::vector{BlastAt({}, 50.0f, 0.0f)}, {}).flashCount, L"not yet at time 0");

    std::vector<NeuronCore::Blast> many;
    for (std::uint32_t i = 0; i < NeuronCore::MAX_LIT_BLASTS + 4u; ++i)
    {
      many.push_back(BlastAt({static_cast<float>(i) * 100.0f, 0.0f, 0.0f}, 10.0f, 0.1f));
    }
    const NeuronCore::BlastLighting nearest = NeuronCore::MakeBlastLighting(many, {1500.0f, 0.0f, 0.0f});
    Assert::AreEqual(NeuronCore::MAX_LIT_BLASTS, nearest.flashCount);
    for (std::uint32_t i = 0; i < nearest.flashCount; ++i)
    {
      Assert::IsTrue(nearest.flashes[i].position.x >= 400.0f, L"the nearest to the camera");
    }
  }

  // The debris's glow and the flashes add to a voxel's pixel, and nothing to the background.
  TEST_METHOD(BlastPixelAddsGlowAndFlash)
  {
    const NeuronCore::PerspectiveView view =
      NeuronCore::MakePerspectiveView({0.0f, 0.0f, -100.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.5f, 9, 9);
    const NeuronCore::Blast blast = BlastAt({0.0f, 0.0f, -20.0f}, 30.0f, 0.1f);
    const NeuronCore::BlastLighting lighting = NeuronCore::MakeBlastLighting({&blast, 1}, view.position);
    const float depth = NeuronCore::PerspectiveDepth(view, 100.0f);
    const Float3 normal{0.0f, 0.0f, -1.0f};
    const Float3 albedo{0.4f, 0.4f, 0.4f};
    const Float3 background = NeuronCore::BlastPixel(view, 4, 4, NeuronCore::NO_VOXEL, normal, depth, albedo, 0.7f, lighting);
    Assert::IsTrue(background.x == 0.0f && background.y == 0.0f && background.z == 0.0f, L"nothing to the background");
    const Float3 cold = NeuronCore::BlastPixel(view, 4, 4, 3u, normal, depth, albedo, 0.0f, lighting);
    const Float3 hot = NeuronCore::BlastPixel(view, 4, 4, 3u, normal, depth, albedo, 0.7f, lighting);
    Assert::IsTrue(Luminance(cold) > 0.0f, L"the flash lights a cold voxel");
    Assert::AreEqual(Luminance(cold) + Luminance(NeuronCore::HeatRadiance(0.7f, lighting)), Luminance(hot), 1.0e-3f * Luminance(hot),
                     L"a hot one glows besides");
  }

  // The noise is the same for the same point and seed, stays within its range with a mean near 1, and moves little
  // over a small step.
  TEST_METHOD(ShellNoiseIsSmoothAndRepeats)
  {
    SeededRandom random(20261120u);
    double sum = 0.0;
    constexpr std::uint32_t SAMPLES = 4000;
    for (std::uint32_t i = 0; i < SAMPLES; ++i)
    {
      const Float3 point = random.InBox({-8.0f, -8.0f, -8.0f}, {8.0f, 8.0f, 8.0f});
      const float value = NeuronCore::ShellNoise(point, 42u);
      Assert::AreEqual(value, NeuronCore::ShellNoise(point, 42u), L"it repeats");
      Assert::IsTrue(value >= 0.25f && value <= 1.75f, std::format(L"{} within its range", value).c_str());
      Assert::IsTrue(std::abs(NeuronCore::ShellNoise(point + Float3{1.0e-3f, 0.0f, 0.0f}, 42u) - value) < 0.02f, L"it is smooth");
      sum += value;
    }
    Assert::AreEqual(1.0, sum / SAMPLES, 0.1, L"its mean is near 1");
    Assert::IsTrue(NeuronCore::ShellNoise({0.3f, 0.6f, 0.9f}, 1u) != NeuronCore::ShellNoise({0.3f, 0.6f, 0.9f}, 2u),
                   L"the seed changes it");
  }

  // A shell grows and fades; a ray that misses it sees nothing, one past its limb more than one through its middle, and
  // a voxel in front of it hides what lies behind.
  TEST_METHOD(ShellsGrowFadeAndBrightenAtTheLimb)
  {
    float previousRadius = 0.0f;
    float previousBrightness = 1.0e9f;
    for (const float time : {0.05f, 0.2f, 0.5f, 1.0f, 2.0f})
    {
      const NeuronCore::Blast blast = BlastAt({0.0f, 0.0f, 0.0f}, 40.0f, time);
      const NeuronCore::GasShells shells = NeuronCore::MakeGasShells({&blast, 1}, {0.0f, 0.0f, -1000.0f});
      Assert::AreEqual(1u, shells.count, std::format(L"lit at {} s", time).c_str());
      const NeuronCore::GasShell& shell = shells.shells[0];
      Assert::IsTrue(shell.radius > previousRadius && shell.radius <= 80.0f, L"it grows toward twice the model's radius");
      const float brightness = Luminance(shell.emission) * shell.thickness;
      Assert::IsTrue(brightness < previousBrightness, L"it fades");
      previousRadius = shell.radius;
      previousBrightness = brightness;
    }
    Assert::AreEqual(0u, NeuronCore::MakeGasShells(std::vector{BlastAt({}, 40.0f, 4.0f)}, {}).count, L"gone by 4 s");

    const NeuronCore::Blast blast = BlastAt({0.0f, 0.0f, 0.0f}, 40.0f, 0.5f);
    const NeuronCore::GasShell shell = NeuronCore::MakeGasShells({&blast, 1}, {}).shells[0];
    const auto through = [&](float _offset, float _farthest)
    { return Luminance(NeuronCore::ShellRadiance({{_offset, 0.0f, -1000.0f}, {0.0f, 0.0f, 1.0f}}, 0.0f, _farthest, shell)); };
    const float endless = 1.0e30f;
    Assert::AreEqual(0.0f, through(shell.radius + 4.0f * shell.thickness, endless), L"a miss sees nothing");
    Assert::IsTrue(through(shell.radius * 0.95f, endless) > through(0.0f, endless), L"brighter at the limb");
    Assert::IsTrue(through(0.0f, 1000.0f) < through(0.0f, endless) && through(0.0f, 1000.0f) > 0.0f, L"a voxel inside hides the far side");
    Assert::AreEqual(0.0f, through(0.0f, 900.0f), L"a voxel in front hides it all");
  }

  // The pass's pixel: every shell, up to the voxel there or without end.
  TEST_METHOD(GasShellPixelStopsAtTheVoxel)
  {
    const NeuronCore::PerspectiveView view =
      NeuronCore::MakePerspectiveView({0.0f, 0.0f, -300.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.5f, 33, 33);
    const std::vector<NeuronCore::Blast> blasts{BlastAt({0.0f, 0.0f, 0.0f}, 40.0f, 0.5f), BlastAt({10.0f, 5.0f, 30.0f}, 20.0f, 0.3f)};
    const NeuronCore::GasShells shells = NeuronCore::MakeGasShells(blasts, view.position);
    Assert::AreEqual(2u, shells.count);
    const float open = Luminance(NeuronCore::GasShellPixel(view, 16, 16, NeuronCore::PERSPECTIVE_FAR_DEPTH, shells));
    const float blocked = Luminance(NeuronCore::GasShellPixel(view, 16, 16, NeuronCore::PerspectiveDepth(view, 300.0f), shells));
    const Float3 each = NeuronCore::ShellRadiance(NeuronCore::PerspectiveRay(view, 16, 16), view.nearPlane, 1.0e30f, shells.shells[0]) +
                        NeuronCore::ShellRadiance(NeuronCore::PerspectiveRay(view, 16, 16), view.nearPlane, 1.0e30f, shells.shells[1]);
    Assert::AreEqual(Luminance(each), open, 1.0e-4f * open, L"the sum of the shells");
    Assert::IsTrue(blocked > 0.0f && blocked < open, L"the near halves only, in front of a voxel at the middle");
  }
};

} // namespace NeuronCoreTests
