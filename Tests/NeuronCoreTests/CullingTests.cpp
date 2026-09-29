#include "pch.h"

#include "SeededRandom.h"

#include "Float3.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "Sphere.h"

#include <array>
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
using NeuronCore::Sphere;

// A camera somewhere in the world's bound (Design/Archive/SpaceScene.md §7.5), looking anywhere, with a field of view from 20 to
// 120 degrees, a random aspect and a random near plane. Each number is drawn in its own statement, so that every
// compiler draws them in the same order.
[[nodiscard]] NeuronCore::PerspectiveView RandomCamera(SeededRandom& _random)
{
  const Float3 eye = _random.InBox({-16000.0f, -16000.0f, -16000.0f}, {16000.0f, 16000.0f, 16000.0f});
  const Float3 forward = _random.Direction();
  const float fovYRadians = _random.Uniform(0.35f, 2.1f);
  const float nearPlane = _random.Uniform(0.01f, 2.0f);
  const std::uint32_t width = 64u + _random.Below(1900u);
  const std::uint32_t height = 64u + _random.Below(1100u);
  return NeuronCore::MakePerspectiveView(eye, eye + forward * 100.0f, {0.0f, 1.0f, 0.0f}, fovYRadians, nearPlane, width, height);
}

// A sun's view somewhere in the world's bound.
[[nodiscard]] NeuronCore::OrthographicView RandomSunView(SeededRandom& _random)
{
  const Float3 origin = _random.InBox({-16000.0f, -16000.0f, -16000.0f}, {16000.0f, 16000.0f, 16000.0f});
  const Float3 forward = _random.Direction();
  const float halfWidth = _random.Uniform(10.0f, 2000.0f);
  const float halfHeight = _random.Uniform(10.0f, 2000.0f);
  const float depthRange = _random.Uniform(10.0f, 4000.0f);
  return NeuronCore::MakeOrthographicView(origin, forward, {0.0f, 1.0f, 0.0f}, halfWidth, halfHeight, depthRange, 1024u, 1024u);
}

// A sphere of _radius outside a plane at _point, whose outward normal is _outward: touching it, or beyond it by _gap.
[[nodiscard]] Sphere Outside(Float3 _point, Float3 _outward, float _radius, float _gap) noexcept
{
  return {_point + _outward * (_radius + _gap), _radius};
}

} // namespace

// Design/Archive/SpaceScene.md §7.4 and §15: culling keeps every sphere that touches a view, and the camera's draws run nearest
// first.
TEST_CLASS(CullingTests)
{
public:
  // A sphere just touching each side of the frustum, or its near plane, from outside is kept; one that clears it by
  // twice the margin is not.
  TEST_METHOD(KeepsSpheresThatTouchTheFrustum)
  {
    SeededRandom random(20261012u);
    for (std::uint32_t sample = 0; sample < 256u; ++sample)
    {
      const NeuronCore::PerspectiveView view = RandomCamera(random);
      const float tanY = view.tanHalfFovY;
      const float tanX = view.tanHalfFovY * view.aspect;
      const float radius = random.Uniform(0.1f, 400.0f);
      const float depth = view.nearPlane + radius + random.Uniform(0.0f, 3000.0f);
      const float across = random.Uniform(-0.9f, 0.9f);
      const Float3 ahead = view.position + view.forward * depth;

      // Each side as a point on it, inside the other three, and its outward normal.
      struct Side
      {
        Float3 point;
        Float3 outward;
      };
      const std::array<Side, 5> sides{
        {{ahead + view.right * (tanX * depth) + view.up * (across * tanY * depth), NeuronCore::Normalize(view.right - view.forward * tanX)},
         {ahead - view.right * (tanX * depth) + view.up * (across * tanY * depth),
          NeuronCore::Normalize(-view.right - view.forward * tanX)},
         {ahead + view.up * (tanY * depth) + view.right * (across * tanX * depth), NeuronCore::Normalize(view.up - view.forward * tanY)},
         {ahead - view.up * (tanY * depth) + view.right * (across * tanX * depth), NeuronCore::Normalize(-view.up - view.forward * tanY)},
         {view.position + view.forward * view.nearPlane + view.right * (across * tanX * view.nearPlane), -view.forward}}};
      for (std::uint32_t side = 0; side < 5u; ++side)
      {
        const std::wstring what = std::format(L"sample {}, side {}, radius {}", sample, side, radius);
        Assert::IsTrue(NeuronCore::IsInView(view, Outside(sides[side].point, sides[side].outward, radius, 0.0f)), what.c_str());
        Assert::IsFalse(NeuronCore::IsInView(view, Outside(sides[side].point, sides[side].outward, radius, 2.0f * NeuronCore::CULL_MARGIN)),
                        what.c_str());
      }
      Assert::IsTrue(NeuronCore::IsInView(view, {view.position, 1.0f}), L"a sphere around the eye");
      Assert::IsTrue(NeuronCore::IsInView(view, {ahead, radius}), L"a sphere ahead");
      Assert::IsFalse(NeuronCore::IsInView(view, {view.position - view.forward * (depth + 2.0f * radius), radius}), L"a sphere behind");
    }
  }

  // A sphere just touching each of the box's six faces from outside is kept; one that clears it by twice the margin is
  // not.
  TEST_METHOD(KeepsSpheresThatTouchTheShadowBox)
  {
    SeededRandom random(20261013u);
    for (std::uint32_t sample = 0; sample < 256u; ++sample)
    {
      const NeuronCore::OrthographicView view = RandomSunView(random);
      const float radius = random.Uniform(0.1f, 400.0f);
      const Float3 middle = view.origin + view.forward * (0.5f * view.depthRange);
      const Float3 inRight = view.right * (random.Uniform(-0.9f, 0.9f) * view.halfWidth);
      const Float3 inUp = view.up * (random.Uniform(-0.9f, 0.9f) * view.halfHeight);
      const Float3 inDepth = view.forward * (random.Uniform(-0.45f, 0.45f) * view.depthRange);
      struct Face
      {
        Float3 point;
        Float3 outward;
      };
      const std::array<Face, 6> faces{{{middle + view.right * view.halfWidth + inUp + inDepth, view.right},
                                       {middle - view.right * view.halfWidth + inUp + inDepth, -view.right},
                                       {middle + view.up * view.halfHeight + inRight + inDepth, view.up},
                                       {middle - view.up * view.halfHeight + inRight + inDepth, -view.up},
                                       {view.origin + inRight + inUp, -view.forward},
                                       {view.origin + view.forward * view.depthRange + inRight + inUp, view.forward}}};
      for (std::uint32_t face = 0; face < 6u; ++face)
      {
        const std::wstring what = std::format(L"sample {}, face {}, radius {}", sample, face, radius);
        Assert::IsTrue(NeuronCore::IsInView(view, Outside(faces[face].point, faces[face].outward, radius, 0.0f)), what.c_str());
        Assert::IsFalse(NeuronCore::IsInView(view, Outside(faces[face].point, faces[face].outward, radius, 2.0f * NeuronCore::CULL_MARGIN)),
                        what.c_str());
      }
      Assert::IsTrue(NeuronCore::IsInView(view, {middle, radius}), L"a sphere inside");
    }
  }

  // The camera draws what it keeps nearest first, by the sphere's nearest point, and two as near in their order; a sphere
  // around the eye comes first.
  TEST_METHOD(DrawsTheNearestFirst)
  {
    const NeuronCore::PerspectiveView view =
      NeuronCore::MakePerspectiveView({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.1f, 640u, 480u);
    const std::vector<Sphere> spheres{{{0.0f, 0.0f, 100.0f}, 10.0f},  // 90 away
                                      {{0.0f, 0.0f, -100.0f}, 10.0f}, // behind: culled
                                      {{5.0f, 0.0f, 60.0f}, 1.0f},    // about 59.2
                                      {{0.0f, 0.0f, 0.0f}, 2.0f},     // around the eye: -2
                                      {{-5.0f, 0.0f, 60.0f}, 1.0f},   // as near as the third
                                      {{0.0f, 0.0f, 95.0f}, 5.0f}};   // 90 away, as the first
    const std::vector<std::uint32_t> draws = NeuronCore::ListViewDraws(view, spheres);
    const std::vector<std::uint32_t> expected{3u, 2u, 4u, 0u, 5u};
    Assert::IsTrue(draws == expected, L"nearest first, ties in order, the one behind culled");
  }

  TEST_METHOD(ShadowDrawsKeepTheirOrder)
  {
    const NeuronCore::OrthographicView view =
      NeuronCore::MakeOrthographicView({0.0f, 100.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 50.0f, 50.0f, 200.0f, 256u, 256u);
    const std::vector<Sphere> spheres{{{0.0f, 0.0f, 0.0f}, 10.0f},
                                      {{0.0f, 0.0f, 80.0f}, 10.0f}, // beyond a side
                                      {{10.0f, 90.0f, 0.0f}, 1.0f},
                                      {{0.0f, -150.0f, 0.0f}, 5.0f}, // beyond the far plane
                                      {{-40.0f, -50.0f, 40.0f}, 1.0f}};
    const std::vector<std::uint32_t> draws = NeuronCore::ListShadowDraws(view, spheres);
    const std::vector<std::uint32_t> expected{0u, 2u, 4u};
    Assert::IsTrue(draws == expected, L"in their order, those outside culled");
  }
};

} // namespace NeuronCoreTests
