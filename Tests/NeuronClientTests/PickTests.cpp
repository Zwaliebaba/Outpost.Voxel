#include "pch.h"

#include "Pick.h"

#include "Float3.h"
#include "PerspectiveView.h"
#include "Ray.h"
#include "RigidTransform.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float2;
using NeuronCore::Float3;

// A view from above and behind the origin, looking at it, 800 by 600.
[[nodiscard]] NeuronCore::PerspectiveView TestView() noexcept
{
  return NeuronCore::MakePerspectiveView({0.0f, 100.0f, -400.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.785398163f, 0.1f, 800, 600);
}

[[nodiscard]] NeuronClient::PickBox Box(std::uint32_t _id, Float3 _middle, float _half) noexcept
{
  return {_id, _middle, NeuronCore::IDENTITY_ROTATION, {_half, _half, _half}};
}

} // namespace

// Design/ADR/ADR-034 and the concept's §9: the pick, on the CPU, against each entity's projected box grown to at least
// ten pixels.
TEST_CLASS(PickTests)
{
public:
  // A point along the ray through a pixel's centre projects back onto that centre, at the ray's parameter as its depth;
  // a point behind the eye projects nowhere.
  TEST_METHOD(ProjectsWhereTheViewsRaysGo)
  {
    const NeuronCore::PerspectiveView view = TestView();
    constexpr std::array<std::array<std::uint32_t, 2>, 4> PIXELS{{{0, 0}, {400, 300}, {799, 599}, {123, 456}}};
    for (const std::array<std::uint32_t, 2>& pixel : PIXELS)
    {
      const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(view, pixel[0], pixel[1]);
      const std::optional<NeuronClient::ScreenPoint> projected = NeuronClient::ProjectPoint(view, ray.origin + ray.direction * 250.0f);
      const std::wstring what = std::format(L"pixel ({}, {})", pixel[0], pixel[1]);
      Assert::IsTrue(projected.has_value(), what.c_str());
      const NeuronClient::ScreenPoint point = projected.value_or(NeuronClient::ScreenPoint{});
      Assert::AreEqual(static_cast<float>(pixel[0]) + 0.5f, point.pixels.x, 1.0e-3f, what.c_str());
      Assert::AreEqual(static_cast<float>(pixel[1]) + 0.5f, point.pixels.y, 1.0e-3f, what.c_str());
      Assert::AreEqual(250.0f, point.depth, 1.0e-2f, what.c_str());
    }
    Assert::IsFalse(NeuronClient::ProjectPoint(view, {0.0f, 100.0f, -500.0f}).has_value(), L"behind the eye");
  }

  // Of two boxes under the pointer, the nearer is picked; a box far off is picked within the ten pixels about it that it
  // is grown to, and not beyond; and empty space picks nothing.
  TEST_METHOD(PicksTheNearestBoxUnderThePointer)
  {
    const NeuronCore::PerspectiveView view = TestView();
    const std::vector<NeuronClient::PickBox> boxes{Box(1, {0.0f, 0.0f, 200.0f}, 10.0f), Box(2, {0.0f, 0.0f, -100.0f}, 10.0f),
                                                   Box(3, {300.0f, 0.0f, 1000.0f}, 0.5f)};
    const std::optional<NeuronClient::ScreenPoint> nearer = NeuronClient::ProjectPoint(view, boxes[1].middle);
    Assert::IsTrue(nearer.has_value(), L"the near box is in view");
    Assert::IsTrue(NeuronClient::Pick(view, boxes, nearer.value_or(NeuronClient::ScreenPoint{}).pixels) == 2u,
                   L"the nearer of two under the pointer");

    const std::optional<NeuronClient::PixelRect> picked = NeuronClient::PickRect(view, boxes[2]);
    Assert::IsTrue(picked.has_value(), L"the far box is in view");
    const NeuronClient::PixelRect grown = picked.value_or(NeuronClient::PixelRect{});
    Assert::AreEqual(NeuronClient::MIN_PICK_PIXELS, grown.width, 1.0e-4f, L"grown to ten pixels wide");
    Assert::AreEqual(NeuronClient::MIN_PICK_PIXELS, grown.height, 1.0e-4f, L"and tall");
    const Float2 middle{grown.x + 0.5f * grown.width, grown.y + 0.5f * grown.height};
    Assert::IsTrue(NeuronClient::Pick(view, boxes, {middle.x + 4.0f, middle.y}) == 3u, L"picked four pixels off");
    Assert::IsFalse(NeuronClient::Pick(view, boxes, {middle.x + 6.0f, middle.y}).has_value(), L"but not six");
    Assert::IsFalse(NeuronClient::Pick(view, boxes, {20.0f, 20.0f}).has_value(), L"empty space");
  }

  // A box selects the entities whose middles it holds, in their order, from either pair of its corners.
  TEST_METHOD(SelectsTheMiddlesWithinABox)
  {
    const NeuronCore::PerspectiveView view = TestView();
    const std::vector<NeuronClient::PickBox> boxes{Box(4, {-100.0f, 0.0f, 0.0f}, 5.0f), Box(5, {0.0f, 0.0f, 0.0f}, 5.0f),
                                                   Box(6, {100.0f, 0.0f, 0.0f}, 5.0f), Box(7, {0.0f, 100.0f, -500.0f}, 5.0f)};
    const std::optional<NeuronClient::ScreenPoint> leftProjected = NeuronClient::ProjectPoint(view, boxes[0].middle);
    const std::optional<NeuronClient::ScreenPoint> middleProjected = NeuronClient::ProjectPoint(view, boxes[1].middle);
    Assert::IsTrue(leftProjected.has_value() && middleProjected.has_value(), L"in view");
    const Float2 left = leftProjected.value_or(NeuronClient::ScreenPoint{}).pixels;
    const Float2 middle = middleProjected.value_or(NeuronClient::ScreenPoint{}).pixels;
    const Float2 corner{left.x - 5.0f, left.y - 5.0f};
    const Float2 opposite{middle.x + 5.0f, middle.y + 5.0f};
    Assert::IsTrue(NeuronClient::PickWithin(view, boxes, corner, opposite) == std::vector<std::uint32_t>{4, 5}, L"the two it holds");
    Assert::IsTrue(NeuronClient::PickWithin(view, boxes, opposite, corner) == std::vector<std::uint32_t>{4, 5}, L"from the other corners");
    Assert::IsTrue(NeuronClient::PickWithin(view, boxes, {0.0f, 0.0f}, {800.0f, 600.0f}).size() == 3, L"never what is behind the eye");
  }
};

} // namespace NeuronClientTests
