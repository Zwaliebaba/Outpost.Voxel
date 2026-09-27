#include "pch.h"

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

namespace VoxelCoreTests
{
namespace
{

using VoxelCore::Float2;
using VoxelCore::Float3;

void ExpectOrthonormalRightHanded(Float3 _right, Float3 _up, Float3 _forward, const wchar_t* _what)
{
  Assert::AreEqual(1.0f, VoxelCore::Length(_right), 1.0e-6f, _what);
  Assert::AreEqual(1.0f, VoxelCore::Length(_up), 1.0e-6f, _what);
  Assert::AreEqual(1.0f, VoxelCore::Length(_forward), 1.0e-6f, _what);
  Assert::AreEqual(0.0f, VoxelCore::Dot(_right, _up), 1.0e-6f, _what);
  Assert::AreEqual(0.0f, VoxelCore::Dot(_right, _forward), 1.0e-6f, _what);
  Assert::AreEqual(0.0f, VoxelCore::Dot(_up, _forward), 1.0e-6f, _what);
  // The view looks down -Z: right x up = -forward.
  const Float3 back = VoxelCore::Cross(_right, _up);
  Assert::AreEqual(-_forward.x, back.x, 1.0e-6f, _what);
  Assert::AreEqual(-_forward.y, back.y, 1.0e-6f, _what);
  Assert::AreEqual(-_forward.z, back.z, 1.0e-6f, _what);
}

} // namespace

// Ray generation and the depth conventions of Design/SampleRenderer.md §7.5 and §9.3.
TEST_CLASS(ViewTests)
{
public:
  TEST_METHOD(OddSizesHaveExactlyZeroCenters)
  {
    const Float2 center = VoxelCore::PixelCenterNdc(80, 45, 161, 91);
    Assert::AreEqual(0.0f, center.x);
    Assert::AreEqual(0.0f, center.y);

    // y runs down the screen and up in NDC; the corner pixels sit half a pixel inside the edges.
    const Float2 topLeft = VoxelCore::PixelCenterNdc(0, 0, 161, 91);
    Assert::AreEqual(-1.0f + 1.0f / 161.0f, topLeft.x, 1.0e-7f);
    Assert::AreEqual(1.0f - 1.0f / 91.0f, topLeft.y, 1.0e-7f);
    const Float2 bottomRight = VoxelCore::PixelCenterNdc(160, 90, 161, 91);
    Assert::AreEqual(1.0f - 1.0f / 161.0f, bottomRight.x, 1.0e-7f);
    Assert::AreEqual(-1.0f + 1.0f / 91.0f, bottomRight.y, 1.0e-7f);
  }

  TEST_METHOD(ALevelCameraHasExactlyZeroComponents)
  {
    // A level camera at the odd test resolution: the centre column and row of rays are exactly parallel to a world
    // plane, the case Listing 5 must survive (§4.2, item 6).
    const VoxelCore::PerspectiveView view =
      VoxelCore::MakePerspectiveView({0.5f, -520.0f, 127.5f}, {0.5f, 0.5f, 127.5f}, {0.0f, 0.0f, 1.0f}, 0.785398163f, 0.1f, 161, 91);
    Assert::AreEqual(1.0f, view.forward.y);
    for (std::uint32_t y = 0; y < 91; ++y)
    {
      Assert::AreEqual(0.0f, VoxelCore::PerspectiveRay(view, 80, y).direction.x, std::format(L"column 80, row {}", y).c_str());
    }
    for (std::uint32_t x = 0; x < 161; ++x)
    {
      Assert::AreEqual(0.0f, VoxelCore::PerspectiveRay(view, x, 45).direction.z, std::format(L"row 45, column {}", x).c_str());
    }
    Assert::AreEqual(1.0f, VoxelCore::PerspectiveRay(view, 80, 45).direction.y, L"the centre ray is the view direction");
  }

  TEST_METHOD(PerspectiveRaysHaveUnitViewDepth)
  {
    // The ray parameter of a hit is its view depth, so that depth is n / t without a division by the ray's length.
    SeededRandom random(31u);
    for (std::uint32_t i = 0; i < 200; ++i)
    {
      const Float3 position = random.InBox({-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f});
      const VoxelCore::PerspectiveView view = VoxelCore::MakePerspectiveView(position, position + random.Direction(), {0.0f, 0.0f, 1.0f},
                                                                             random.Uniform(0.3f, 2.0f), 0.1f, 64, 48);
      const std::wstring what = std::format(L"view {}", i);
      ExpectOrthonormalRightHanded(view.right, view.up, view.forward, what.c_str());
      for (const std::uint32_t pixel : {0u, 17u, 31u, 47u})
      {
        const VoxelCore::Ray ray = VoxelCore::PerspectiveRay(view, pixel, pixel);
        Assert::AreEqual(1.0f, VoxelCore::Dot(ray.direction, view.forward), 1.0e-6f, what.c_str());

        // The ray passes through the pixel centre: its lateral components over its depth are the NDC times the field of
        // view, which is how the splat's rectangle maps back to pixels.
        const Float2 ndc = VoxelCore::PixelCenterNdc(pixel, pixel, 64, 48);
        Assert::AreEqual(ndc.x * view.tanHalfFovY * view.aspect, VoxelCore::Dot(ray.direction, view.right), 1.0e-6f, what.c_str());
        Assert::AreEqual(ndc.y * view.tanHalfFovY, VoxelCore::Dot(ray.direction, view.up), 1.0e-6f, what.c_str());
      }
    }
  }

  TEST_METHOD(PerspectiveDepthIsReversed)
  {
    const VoxelCore::PerspectiveView view =
      VoxelCore::MakePerspectiveView({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.0f, 0.1f, 64, 48);
    Assert::AreEqual(1.0f, VoxelCore::PerspectiveDepth(view, 0.1f), L"the near plane is 1");
    Assert::AreEqual(0.5f, VoxelCore::PerspectiveDepth(view, 0.2f), 1.0e-7f, L"twice as far is half");
    Assert::IsTrue(VoxelCore::PerspectiveDepth(view, 1000.0f) < VoxelCore::PerspectiveDepth(view, 999.0f), L"nearer is greater");
    Assert::AreEqual(0.0f, VoxelCore::PerspectiveDepth(view, std::numeric_limits<float>::infinity()), L"the far plane is at infinity");
  }

  TEST_METHOD(BasisFallsBackWhenLookingAlongUp)
  {
    // The sun straight overhead looks along -Z with +Z as up: +Y stands in, and every ray is axis-parallel.
    Float3 right{};
    Float3 up{};
    VoxelCore::MakeViewBasis({0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, right, up);
    ExpectOrthonormalRightHanded(right, up, {0.0f, 0.0f, -1.0f}, L"looking down");
    Assert::AreEqual(1.0f, right.x);
    Assert::AreEqual(1.0f, up.y);

    VoxelCore::MakeViewBasis({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, right, up);
    ExpectOrthonormalRightHanded(right, up, {0.0f, 0.0f, 1.0f}, L"looking up");

    const VoxelCore::OrthographicView sun =
      VoxelCore::MakeOrthographicView({0.0f, 0.0f, 300.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, 100.0f, 100.0f, 310.0f, 64, 64);
    for (const std::uint32_t pixel : {0u, 31u, 63u})
    {
      const VoxelCore::Ray ray = VoxelCore::OrthographicRay(sun, pixel, 63u - pixel);
      Assert::AreEqual(0.0f, ray.direction.x);
      Assert::AreEqual(0.0f, ray.direction.y);
      Assert::AreEqual(-1.0f, ray.direction.z);
    }
  }

  TEST_METHOD(OrthographicRaysStartOnTheNearPlane)
  {
    SeededRandom random(32u);
    for (std::uint32_t i = 0; i < 200; ++i)
    {
      const Float3 origin = random.InBox({-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f});
      const VoxelCore::OrthographicView view =
        VoxelCore::MakeOrthographicView(origin, random.Direction(), {0.0f, 0.0f, 1.0f}, 30.0f, 20.0f, 250.0f, 64, 48);
      const std::wstring what = std::format(L"view {}", i);
      ExpectOrthonormalRightHanded(view.right, view.up, view.forward, what.c_str());
      for (const std::uint32_t pixel : {0u, 17u, 47u})
      {
        const VoxelCore::Ray ray = VoxelCore::OrthographicRay(view, pixel, pixel);
        const Float2 ndc = VoxelCore::PixelCenterNdc(pixel, pixel, 64, 48);
        const Float3 offset = ray.origin - view.origin;
        Assert::AreEqual(0.0f, VoxelCore::Dot(offset, view.forward), 1.0e-4f, what.c_str());
        Assert::AreEqual(ndc.x * 30.0f, VoxelCore::Dot(offset, view.right), 1.0e-4f, what.c_str());
        Assert::AreEqual(ndc.y * 20.0f, VoxelCore::Dot(offset, view.up), 1.0e-4f, what.c_str());
        Assert::AreEqual(view.forward.x, ray.direction.x, what.c_str());
        Assert::AreEqual(view.forward.y, ray.direction.y, what.c_str());
        Assert::AreEqual(view.forward.z, ray.direction.z, what.c_str());
      }
    }
    const VoxelCore::OrthographicView view =
      VoxelCore::MakeOrthographicView({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.0f, 1.0f, 250.0f, 64, 48);
    Assert::AreEqual(0.0f, VoxelCore::OrthographicDepth(view, 0.0f), L"the near plane is 0");
    Assert::AreEqual(1.0f, VoxelCore::OrthographicDepth(view, 250.0f), L"the far plane is 1");
  }
};

} // namespace VoxelCoreTests
