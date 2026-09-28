#include "pch.h"

#include "Box.h"
#include "Float3.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "SeededRandom.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float2;
using NeuronCore::Float3;

void ExpectOrthonormalLeftHanded(Float3 _right, Float3 _up, Float3 _forward, const wchar_t* _what)
{
  Assert::AreEqual(1.0f, NeuronCore::Length(_right), 1.0e-6f, _what);
  Assert::AreEqual(1.0f, NeuronCore::Length(_up), 1.0e-6f, _what);
  Assert::AreEqual(1.0f, NeuronCore::Length(_forward), 1.0e-6f, _what);
  Assert::AreEqual(0.0f, NeuronCore::Dot(_right, _up), 1.0e-6f, _what);
  Assert::AreEqual(0.0f, NeuronCore::Dot(_right, _forward), 1.0e-6f, _what);
  Assert::AreEqual(0.0f, NeuronCore::Dot(_up, _forward), 1.0e-6f, _what);
  // Direct3D's convention, left-handed: right × up = +forward (Design/NeuronVoxelFormat.md §12).
  const Float3 ahead = NeuronCore::Cross(_right, _up);
  Assert::AreEqual(_forward.x, ahead.x, 1.0e-6f, _what);
  Assert::AreEqual(_forward.y, ahead.y, 1.0e-6f, _what);
  Assert::AreEqual(_forward.z, ahead.z, 1.0e-6f, _what);
}

} // namespace

// Ray generation and the depth conventions of Design/Archive/SampleRenderer.md §7.5 and §9.3.
TEST_CLASS(ViewTests)
{
public:
  TEST_METHOD(OddSizesHaveExactlyZeroCenters)
  {
    const Float2 center = NeuronCore::PixelCenterNdc(80, 45, 161, 91);
    Assert::AreEqual(0.0f, center.x);
    Assert::AreEqual(0.0f, center.y);

    // y runs down the screen and up in NDC; the corner pixels sit half a pixel inside the edges.
    const Float2 topLeft = NeuronCore::PixelCenterNdc(0, 0, 161, 91);
    Assert::AreEqual(-1.0f + 1.0f / 161.0f, topLeft.x, 1.0e-7f);
    Assert::AreEqual(1.0f - 1.0f / 91.0f, topLeft.y, 1.0e-7f);
    const Float2 bottomRight = NeuronCore::PixelCenterNdc(160, 90, 161, 91);
    Assert::AreEqual(1.0f - 1.0f / 161.0f, bottomRight.x, 1.0e-7f);
    Assert::AreEqual(-1.0f + 1.0f / 91.0f, bottomRight.y, 1.0e-7f);
  }

  TEST_METHOD(ALevelCameraHasExactlyZeroComponents)
  {
    // A level camera at the odd test resolution: the centre column and row of rays are exactly parallel to a world
    // plane, the case Listing 5 must survive (§4.2, item 6).
    const NeuronCore::PerspectiveView view =
      NeuronCore::MakePerspectiveView({0.5f, 127.5f, -520.0f}, {0.5f, 127.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, 0.785398163f, 0.1f, 161, 91);
    Assert::AreEqual(1.0f, view.forward.z);
    for (std::uint32_t y = 0; y < 91; ++y)
    {
      Assert::AreEqual(0.0f, NeuronCore::PerspectiveRay(view, 80, y).direction.x, std::format(L"column 80, row {}", y).c_str());
    }
    for (std::uint32_t x = 0; x < 161; ++x)
    {
      Assert::AreEqual(0.0f, NeuronCore::PerspectiveRay(view, x, 45).direction.y, std::format(L"row 45, column {}", x).c_str());
    }
    Assert::AreEqual(1.0f, NeuronCore::PerspectiveRay(view, 80, 45).direction.z, L"the centre ray is the view direction");
  }

  TEST_METHOD(PerspectiveRaysHaveUnitViewDepth)
  {
    // The ray parameter of a hit is its view depth, so that depth is n / t without a division by the ray's length.
    SeededRandom random(31u);
    for (std::uint32_t i = 0; i < 200; ++i)
    {
      const Float3 position = random.InBox({-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f});
      const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView(position, position + random.Direction(), {0.0f, 1.0f, 0.0f},
                                                                               random.Uniform(0.3f, 2.0f), 0.1f, 64, 48);
      const std::wstring what = std::format(L"view {}", i);
      ExpectOrthonormalLeftHanded(view.right, view.up, view.forward, what.c_str());
      for (const std::uint32_t pixel : {0u, 17u, 31u, 47u})
      {
        const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(view, pixel, pixel);
        Assert::AreEqual(1.0f, NeuronCore::Dot(ray.direction, view.forward), 1.0e-6f, what.c_str());

        // The ray passes through the pixel centre: its lateral components over its depth are the NDC times the field of
        // view, which is how the splat's rectangle maps back to pixels.
        const Float2 ndc = NeuronCore::PixelCenterNdc(pixel, pixel, 64, 48);
        Assert::AreEqual(ndc.x * view.tanHalfFovY * view.aspect, NeuronCore::Dot(ray.direction, view.right), 1.0e-6f, what.c_str());
        Assert::AreEqual(ndc.y * view.tanHalfFovY, NeuronCore::Dot(ray.direction, view.up), 1.0e-6f, what.c_str());
      }
    }
  }

  // Design/NeuronVoxelFormat.md §12.4: a camera looking along +Z with +Y up sees a voxel at +X in the right half of its
  // image and one at +Y in the top half. Cross products in the wrong order mirror every image, and no test that compares
  // the GPU with its twin can see that, since the two share one basis (§12.3).
  TEST_METHOD(ImagesAreNotMirrored)
  {
    constexpr std::uint32_t SIDE_PIXELS = 64;
    const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView({0.0f, 0.0f, -20.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                                                             0.785398163f, 0.1f, SIDE_PIXELS, SIDE_PIXELS);
    struct Placed
    {
      const wchar_t* name;
      Float3 center;
      bool rightHalf; // where the voxel must be: the right half of the image, or else the top half
    };
    for (const Placed& placed : {Placed{L"a voxel at +X", {4.0f, 0.0f, 0.0f}, true}, Placed{L"a voxel at +Y", {0.0f, 4.0f, 0.0f}, false}})
    {
      const NeuronCore::Box box = NeuronCore::MakeAxisAlignedBox(placed.center, {0.5f, 0.5f, 0.5f});
      std::uint32_t hits = 0;
      for (std::uint32_t y = 0; y < SIDE_PIXELS; ++y)
      {
        for (std::uint32_t x = 0; x < SIDE_PIXELS; ++x)
        {
          const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(view, x, y);
          float distance = 0.0f;
          Float3 normal{};
          if (!NeuronCore::IntersectBox<false, false>(box, ray.origin, ray.direction, NeuronCore::InverseDirection(ray), distance, normal))
          {
            continue;
          }
          ++hits;
          const bool inside = placed.rightHalf ? x >= SIDE_PIXELS / 2 : y < SIDE_PIXELS / 2;
          Assert::IsTrue(inside, std::format(L"{} shows at pixel ({}, {})", placed.name, x, y).c_str());
        }
      }
      Assert::IsTrue(hits > 0, std::format(L"{} is in view", placed.name).c_str());
    }
  }

  TEST_METHOD(PerspectiveDepthIsReversed)
  {
    const NeuronCore::PerspectiveView view =
      NeuronCore::MakePerspectiveView({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.1f, 64, 48);
    Assert::AreEqual(1.0f, NeuronCore::PerspectiveDepth(view, 0.1f), L"the near plane is 1");
    Assert::AreEqual(0.5f, NeuronCore::PerspectiveDepth(view, 0.2f), 1.0e-7f, L"twice as far is half");
    Assert::IsTrue(NeuronCore::PerspectiveDepth(view, 1000.0f) < NeuronCore::PerspectiveDepth(view, 999.0f), L"nearer is greater");
    Assert::AreEqual(0.0f, NeuronCore::PerspectiveDepth(view, std::numeric_limits<float>::infinity()), L"the far plane is at infinity");
  }

  TEST_METHOD(BasisFallsBackWhenLookingAlongUp)
  {
    // The sun straight overhead looks along -Y with +Y as up: +Z stands in, and every ray is axis-parallel.
    Float3 right{};
    Float3 up{};
    NeuronCore::MakeViewBasis({0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, right, up);
    ExpectOrthonormalLeftHanded(right, up, {0.0f, -1.0f, 0.0f}, L"looking down");
    Assert::AreEqual(1.0f, right.x);
    Assert::AreEqual(1.0f, up.z);

    NeuronCore::MakeViewBasis({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, right, up);
    ExpectOrthonormalLeftHanded(right, up, {0.0f, 1.0f, 0.0f}, L"looking up");

    const NeuronCore::OrthographicView sun =
      NeuronCore::MakeOrthographicView({0.0f, 300.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 100.0f, 100.0f, 310.0f, 64, 64);
    for (const std::uint32_t pixel : {0u, 31u, 63u})
    {
      const NeuronCore::Ray ray = NeuronCore::OrthographicRay(sun, pixel, 63u - pixel);
      Assert::AreEqual(0.0f, ray.direction.x);
      Assert::AreEqual(-1.0f, ray.direction.y);
      Assert::AreEqual(0.0f, ray.direction.z);
    }
  }

  TEST_METHOD(OrthographicRaysStartOnTheNearPlane)
  {
    SeededRandom random(32u);
    for (std::uint32_t i = 0; i < 200; ++i)
    {
      const Float3 origin = random.InBox({-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f});
      const NeuronCore::OrthographicView view =
        NeuronCore::MakeOrthographicView(origin, random.Direction(), {0.0f, 1.0f, 0.0f}, 30.0f, 20.0f, 250.0f, 64, 48);
      const std::wstring what = std::format(L"view {}", i);
      ExpectOrthonormalLeftHanded(view.right, view.up, view.forward, what.c_str());
      for (const std::uint32_t pixel : {0u, 17u, 47u})
      {
        const NeuronCore::Ray ray = NeuronCore::OrthographicRay(view, pixel, pixel);
        const Float2 ndc = NeuronCore::PixelCenterNdc(pixel, pixel, 64, 48);
        const Float3 offset = ray.origin - view.origin;
        Assert::AreEqual(0.0f, NeuronCore::Dot(offset, view.forward), 1.0e-4f, what.c_str());
        Assert::AreEqual(ndc.x * 30.0f, NeuronCore::Dot(offset, view.right), 1.0e-4f, what.c_str());
        Assert::AreEqual(ndc.y * 20.0f, NeuronCore::Dot(offset, view.up), 1.0e-4f, what.c_str());
        Assert::AreEqual(view.forward.x, ray.direction.x, what.c_str());
        Assert::AreEqual(view.forward.y, ray.direction.y, what.c_str());
        Assert::AreEqual(view.forward.z, ray.direction.z, what.c_str());
      }
    }
    const NeuronCore::OrthographicView view =
      NeuronCore::MakeOrthographicView({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, 1.0f, 250.0f, 64, 48);
    Assert::AreEqual(0.0f, NeuronCore::OrthographicDepth(view, 0.0f), L"the near plane is 0");
    Assert::AreEqual(1.0f, NeuronCore::OrthographicDepth(view, 250.0f), L"the far plane is 1");
  }

  // §7.5: the shadow map is standard Z, cleared to the far plane, and nearer is smaller.
  TEST_METHOD(OrthographicDepthIsStandard)
  {
    static_assert(NeuronCore::ORTHOGRAPHIC_FAR_DEPTH == 1.0f);
    static_assert(NeuronCore::IsNearerOrthographicDepth(0.25f, 0.5f));
    static_assert(!NeuronCore::IsNearerOrthographicDepth(0.5f, 0.5f));
    static_assert(!NeuronCore::IsNearerOrthographicDepth(NeuronCore::ORTHOGRAPHIC_FAR_DEPTH, 0.5f));
  }

  // §10: the sun's view is a square centred on the scene, and every corner of the box it holds lies at least a unit
  // inside its depth range, whatever the sun's direction, straight overhead included.
  TEST_METHOD(ShadowViewHoldsItsBoxInDepth)
  {
    SeededRandom random(33u);
    const Float3 lower{-103.0f, 0.0f, -114.0f};
    const Float3 upper{104.0f, 255.0f, 114.0f};
    const Float3 center{0.5f, 127.5f, 0.0f};
    for (std::uint32_t i = 0; i < 200; ++i)
    {
      Float3 toSun = random.Direction();
      toSun.y = std::abs(toSun.y) + 0.01f;
      if (i == 0)
      {
        toSun = {0.0f, 1.0f, 0.0f};
      }
      const NeuronCore::OrthographicView view = NeuronCore::MakeShadowView(toSun, center, 512.0f, lower, upper, 4096);
      const std::wstring what = std::format(L"sun {}", i);
      ExpectOrthonormalLeftHanded(view.right, view.up, view.forward, what.c_str());
      Assert::AreEqual(-1.0f, NeuronCore::Dot(view.forward, NeuronCore::Normalize(toSun)), 1.0e-6f, what.c_str());
      Assert::AreEqual(0.0f, NeuronCore::Dot(center - view.origin, view.right), 1.0e-3f, L"the square is centred across");
      Assert::AreEqual(0.0f, NeuronCore::Dot(center - view.origin, view.up), 1.0e-3f, L"the square is centred up and down");
      Assert::AreEqual(512.0f, view.halfWidth, what.c_str());
      Assert::AreEqual(512.0f, view.halfHeight, what.c_str());
      Assert::AreEqual(4096u, view.widthPixels, what.c_str());
      Assert::AreEqual(4096u, view.heightPixels, what.c_str());
      for (std::uint32_t corner = 0; corner < 8; ++corner)
      {
        const Float3 point{(corner & 1u) != 0u ? upper.x : lower.x, (corner & 2u) != 0u ? upper.y : lower.y,
                           (corner & 4u) != 0u ? upper.z : lower.z};
        const float distance = NeuronCore::Dot(point - view.origin, view.forward);
        Assert::IsTrue(distance >= 1.0f - 1.0e-3f && distance <= view.depthRange - 1.0f + 1.0e-3f,
                       std::format(L"{}: corner {} at {} of {}", what, corner, distance, view.depthRange).c_str());
      }
    }
  }
};

} // namespace NeuronCoreTests
