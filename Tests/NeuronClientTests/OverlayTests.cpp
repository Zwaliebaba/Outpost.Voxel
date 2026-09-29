#include "pch.h"

#include "Overlay.h"
#include "Pick.h"
#include "RecordingSurface.h"

#include "Float3.h"
#include "PerspectiveView.h"

#include <cmath>
#include <cstddef>
#include <optional>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float2;
using NeuronCore::Float3;

constexpr Float3 WHITE{1.0f, 1.0f, 1.0f};
constexpr Float3 GREEN{0.0f, 1.0f, 0.0f};
constexpr Float3 GRAY{0.2f, 0.2f, 0.2f};

// A view straight down onto the origin from 500 units up, 800 by 600: a level circle about the origin shows as a circle.
[[nodiscard]] NeuronCore::PerspectiveView DownView() noexcept
{
  return NeuronCore::MakePerspectiveView({0.0f, 500.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.785398163f, 0.1f, 800, 600);
}

[[nodiscard]] float Distance(Float2 _a, Float2 _b) noexcept
{
  return std::hypot(_b.x - _a.x, _b.y - _a.y);
}

} // namespace

// Design/ADR/ADR-034: the world's overlay, drawn on a surface as a view shows it, on the CPU.
TEST_CLASS(OverlayTests)
{
public:
  // A ring is RING_SEGMENTS segments end to end, closed, every end on the circle the view shows.
  TEST_METHOD(DrawsARingOnItsCircle)
  {
    const NeuronCore::PerspectiveView view = DownView();
    RecordingSurface surface;
    NeuronClient::DrawRing(surface, view, {0.0f, 0.0f, 0.0f}, 100.0f, 2.0f, WHITE, 0.8f);
    Assert::AreEqual(static_cast<std::size_t>(NeuronClient::RING_SEGMENTS), surface.segments.size());
    const std::optional<NeuronClient::ScreenPoint> centerProjected = NeuronClient::ProjectPoint(view, {0.0f, 0.0f, 0.0f});
    const std::optional<NeuronClient::ScreenPoint> rimProjected = NeuronClient::ProjectPoint(view, {100.0f, 0.0f, 0.0f});
    Assert::IsTrue(centerProjected.has_value() && rimProjected.has_value(), L"in view");
    const Float2 center = centerProjected.value_or(NeuronClient::ScreenPoint{}).pixels;
    const float radius = Distance(center, rimProjected.value_or(NeuronClient::ScreenPoint{}).pixels);
    for (std::size_t segment = 0; segment < surface.segments.size(); ++segment)
    {
      const RecordingSurface::Segment& drawn = surface.segments[segment];
      const RecordingSurface::Segment& next = surface.segments[(segment + 1) % surface.segments.size()];
      Assert::AreEqual(radius, Distance(center, drawn.start), 1.0e-2f, L"on the circle");
      Assert::IsTrue(Distance(drawn.end, next.start) < 1.0e-3f, L"end to end, and closed");
      Assert::AreEqual(2.0f, drawn.widthPixels);
    }
  }

  // A line that crosses the near plane is cut there, and one wholly behind it is left out.
  TEST_METHOD(CutsALineAtTheNearPlane)
  {
    const NeuronCore::PerspectiveView view = DownView();
    RecordingSurface surface;
    NeuronClient::DrawLine(surface, view, {10.0f, 0.0f, 0.0f}, {10.0f, 900.0f, 0.0f}, 1.5f, WHITE, 1.0f);
    Assert::AreEqual(std::size_t{1}, surface.segments.size(), L"cut, and drawn");
    Assert::IsTrue(std::isfinite(surface.segments[0].end.x) && std::isfinite(surface.segments[0].end.y), L"to where it meets the plane");
    NeuronClient::DrawLine(surface, view, {10.0f, 600.0f, 0.0f}, {10.0f, 900.0f, 0.0f}, 1.5f, WHITE, 1.0f);
    Assert::AreEqual(std::size_t{1}, surface.segments.size(), L"a line behind the eye is left out");
  }

  // A marker is a diamond of its size about where its point shows, whatever the distance; a bar is its full part, then
  // the rest.
  TEST_METHOD(DrawsAMarkerAndABar)
  {
    const NeuronCore::PerspectiveView view = DownView();
    RecordingSurface surface;
    NeuronClient::DrawMarker(surface, view, {50.0f, 0.0f, 50.0f}, 12.0f, 2.0f, GREEN, 1.0f);
    Assert::AreEqual(std::size_t{4}, surface.segments.size(), L"four sides");
    const std::optional<NeuronClient::ScreenPoint> projected = NeuronClient::ProjectPoint(view, {50.0f, 0.0f, 50.0f});
    Assert::IsTrue(projected.has_value(), L"in view");
    const Float2 at = projected.value_or(NeuronClient::ScreenPoint{}).pixels;
    for (const RecordingSurface::Segment& side : surface.segments)
    {
      Assert::AreEqual(6.0f, Distance(at, side.start), 1.0e-3f, L"each corner half its size away");
    }

    NeuronClient::DrawBar(surface, view, {0.0f, 0.0f, 0.0f}, 20.0f, 40.0f, 4.0f, 0.75f, GREEN, GRAY, 0.9f);
    Assert::AreEqual(std::size_t{2}, surface.fills.size(), L"the full part and the rest");
    Assert::AreEqual(30u, surface.fills[0].widthPixels, L"three quarters");
    Assert::AreEqual(10u, surface.fills[1].widthPixels);
    Assert::AreEqual(surface.fills[0].xPixels + 30, surface.fills[1].xPixels, L"side by side");
  }
};

} // namespace NeuronClientTests
