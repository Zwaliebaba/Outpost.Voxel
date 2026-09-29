#include "pch.h"

#include "Box.h"
#include "Float3.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "SeededRandom.h"
#include "SplatBounds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Box;
using NeuronCore::Float2;
using NeuronCore::Float3;
using NeuronCore::OrthographicView;
using NeuronCore::PerspectiveView;
using NeuronCore::SplatBounds;

constexpr std::uint32_t WIDTH_PIXELS = 64;
constexpr std::uint32_t HEIGHT_PIXELS = 48;
constexpr float NEAR_PLANE = 0.1f;
constexpr std::uint32_t BOX_COUNT = 400;

[[nodiscard]] Float2 MarginNdc() noexcept
{
  return {2.0f * NeuronCore::BOUNDS_MARGIN_PIXELS / static_cast<float>(WIDTH_PIXELS),
          2.0f * NeuronCore::BOUNDS_MARGIN_PIXELS / static_cast<float>(HEIGHT_PIXELS)};
}

[[nodiscard]] Box RandomBox(SeededRandom& _random, bool _oriented, Float3 _center, float _largestRadius)
{
  Float3 axisX{1.0f, 0.0f, 0.0f};
  Float3 axisY{0.0f, 1.0f, 0.0f};
  Float3 axisZ{0.0f, 0.0f, 1.0f};
  if (_oriented)
  {
    _random.Rotation(axisX, axisY, axisZ);
  }
  const float smallest = 0.05f * _largestRadius;
  return NeuronCore::MakeOrientedBox(
    _center, _random.InBox({smallest, smallest, smallest}, {_largestRadius, _largestRadius, _largestRadius}), axisX, axisY, axisZ);
}

// A box in or near a random camera's view: a quarter across the near plane, half within ten units, a quarter far away
// and small enough for the quadric path; up to a third of the way outside the frustum on each side.
[[nodiscard]] Box BoxInView(SeededRandom& _random, const PerspectiveView& _view, bool _oriented, std::uint32_t _index)
{
  float depth = 0.0f;
  float largestRadius = 0.0f;
  switch (_index % 4)
  {
  case 0:
    largestRadius = _random.Uniform(0.2f, 2.0f);
    depth = NEAR_PLANE + _random.Uniform(-1.5f, 1.5f) * largestRadius;
    break;
  case 1:
  case 2:
    largestRadius = _random.Uniform(0.1f, 3.0f);
    depth = _random.Uniform(0.5f, 10.0f);
    break;
  default:
    largestRadius = _random.Uniform(0.05f, 1.0f);
    depth = _random.Uniform(10.0f, 200.0f);
    break;
  }
  const float reach = std::max(depth, 1.0f) * 1.3f;
  const Float3 center = _view.position + _view.forward * depth +
                        _view.right * (_random.Uniform(-1.0f, 1.0f) * reach * _view.tanHalfFovY * _view.aspect) +
                        _view.up * (_random.Uniform(-1.0f, 1.0f) * reach * _view.tanHalfFovY);
  return RandomBox(_random, _oriented, center, largestRadius);
}

[[nodiscard]] PerspectiveView RandomPerspective(SeededRandom& _random)
{
  const Float3 position = _random.InBox({-30.0f, -30.0f, -30.0f}, {30.0f, 30.0f, 30.0f});
  const Float3 target = position + _random.Direction();
  return NeuronCore::MakePerspectiveView(position, target, {0.0f, 1.0f, 0.0f}, _random.Uniform(0.5f, 1.6f), NEAR_PLANE, WIDTH_PIXELS,
                                         HEIGHT_PIXELS);
}

// Whether a pixel centre lies inside the splat with at least half the margin to spare: the margin is there for the
// rasterizer's snapping, and bounds that needed it to cover their own rounding would leave nothing for that.
[[nodiscard]] bool Covers(const SplatBounds& _bounds, Float2 _ndc) noexcept
{
  const Float2 spare = MarginNdc();
  return _bounds.visible && _ndc.x >= _bounds.minNdc.x + 0.5f * spare.x && _ndc.x <= _bounds.maxNdc.x - 0.5f * spare.x &&
         _ndc.y >= _bounds.minNdc.y + 0.5f * spare.y && _ndc.y <= _bounds.maxNdc.y - 0.5f * spare.y;
}

template <bool Oriented> void PerspectiveSplatsCoverEveryHit(std::uint32_t _seed)
{
  SeededRandom random(_seed);
  std::uint32_t quadricBoxesHit = 0;
  std::uint32_t preciseBoxesHit = 0;
  std::uint32_t nearPlaneBoxesHit = 0;
  std::uint32_t pixelsHit = 0;
  for (std::uint32_t i = 0; i < BOX_COUNT; ++i)
  {
    const PerspectiveView view = RandomPerspective(random);
    const Box box = BoxInView(random, view, Oriented, i);
    const SplatBounds bounds = NeuronCore::PerspectiveSplatBounds(box, view);

    bool anyHit = false;
    for (std::uint32_t y = 0; y < HEIGHT_PIXELS; ++y)
    {
      for (std::uint32_t x = 0; x < WIDTH_PIXELS; ++x)
      {
        const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(view, x, y);
        float distance = 0.0f;
        Float3 normal{};
        if (!NeuronCore::IntersectBox<Oriented, false>(box, ray.origin, ray.direction, NeuronCore::InverseDirection(ray), distance,
                                                       normal) ||
            distance < view.nearPlane)
        {
          continue;
        }
        const std::wstring what = std::format(L"box {}, pixel {} {}, distance {}", i, x, y, distance);
        Assert::IsTrue(Covers(bounds, NeuronCore::PixelCenterNdc(x, y, WIDTH_PIXELS, HEIGHT_PIXELS)), what.c_str());
        // Reversed-Z: nothing the box shows may be nearer, that is deeper, than the splat.
        Assert::IsTrue(NeuronCore::PerspectiveDepth(view, distance) <= bounds.depth * (1.0f + 1.0e-5f), what.c_str());
        anyHit = true;
        ++pixelsHit;
      }
    }
    if (!anyHit)
    {
      continue;
    }

    // Which path the bounds took, decided as the implementation decides it.
    const Float3 offset = box.center - view.position;
    const float radius = NeuronCore::Length(box.radius);
    Float2 minNdc{};
    Float2 maxNdc{};
    const bool quadric = NeuronCore::Dot(offset, view.forward) - radius >= view.nearPlane &&
                         NeuronCore::QuadricBounds(box.center, radius, view, minNdc, maxNdc) &&
                         (maxNdc.x - minNdc.x) * 0.5f * static_cast<float>(WIDTH_PIXELS) <= NeuronCore::QUADRIC_LIMIT_PIXELS &&
                         (maxNdc.y - minNdc.y) * 0.5f * static_cast<float>(HEIGHT_PIXELS) <= NeuronCore::QUADRIC_LIMIT_PIXELS;
    quadricBoxesHit += quadric ? 1u : 0u;
    preciseBoxesHit += quadric ? 0u : 1u;
    nearPlaneBoxesHit += NeuronCore::Dot(offset, view.forward) - radius < view.nearPlane ? 1u : 0u;
  }
  Logger::WriteMessage(std::format(L"{} boxes hit on the quadric path, {} on the precise path, {} of them with a sphere reaching the near "
                                   L"plane; {} pixels",
                                   quadricBoxesHit, preciseBoxesHit, nearPlaneBoxesHit, pixelsHit)
                         .c_str());
  Assert::IsTrue(quadricBoxesHit >= 25, L"the quadric path is exercised");
  Assert::IsTrue(preciseBoxesHit >= 40, L"the precise path is exercised");
  Assert::IsTrue(nearPlaneBoxesHit >= 20, L"near-plane clipping is exercised");
}

template <bool Oriented> void OrthographicSplatsCoverEveryHit(std::uint32_t _seed)
{
  SeededRandom random(_seed);
  std::uint32_t boxesHit = 0;
  for (std::uint32_t i = 0; i < BOX_COUNT; ++i)
  {
    const float halfWidth = random.Uniform(5.0f, 40.0f);
    const float halfHeight = halfWidth * static_cast<float>(HEIGHT_PIXELS) / static_cast<float>(WIDTH_PIXELS);
    const OrthographicView view =
      NeuronCore::MakeOrthographicView(random.InBox({-30.0f, -30.0f, -30.0f}, {30.0f, 30.0f, 30.0f}), random.Direction(),
                                       {0.0f, 1.0f, 0.0f}, halfWidth, halfHeight, 100.0f, WIDTH_PIXELS, HEIGHT_PIXELS);
    const float depth = random.Uniform(-3.0f, 100.0f);
    const Float3 center = view.origin + view.forward * depth + view.right * random.Uniform(-1.3f * halfWidth, 1.3f * halfWidth) +
                          view.up * random.Uniform(-1.3f * halfHeight, 1.3f * halfHeight);
    const Box box = RandomBox(random, Oriented, center, random.Uniform(0.1f, 0.2f * halfWidth));
    const SplatBounds bounds = NeuronCore::OrthographicSplatBounds(box, view);

    bool anyHit = false;
    for (std::uint32_t y = 0; y < HEIGHT_PIXELS; ++y)
    {
      for (std::uint32_t x = 0; x < WIDTH_PIXELS; ++x)
      {
        const NeuronCore::Ray ray = NeuronCore::OrthographicRay(view, x, y);
        float distance = 0.0f;
        Float3 normal{};
        if (!NeuronCore::IntersectBox<Oriented, false>(box, ray.origin, ray.direction, NeuronCore::InverseDirection(ray), distance,
                                                       normal) ||
            distance > view.depthRange)
        {
          continue;
        }
        const std::wstring what = std::format(L"box {}, pixel {} {}, distance {}", i, x, y, distance);
        Assert::IsTrue(Covers(bounds, NeuronCore::PixelCenterNdc(x, y, WIDTH_PIXELS, HEIGHT_PIXELS)), what.c_str());
        // Standard Z: nothing the box shows may be nearer, that is shallower, than the splat.
        Assert::IsTrue(NeuronCore::OrthographicDepth(view, distance) >= bounds.depth - 1.0e-6f, what.c_str());
        anyHit = true;
      }
    }
    boxesHit += anyHit ? 1u : 0u;
  }
  Logger::WriteMessage(std::format(L"{} of {} boxes hit", boxesHit, BOX_COUNT).c_str());
  Assert::IsTrue(boxesHit >= BOX_COUNT / 4, L"enough boxes are seen");
}

void AreEqualFloat2(Float2 _expected, Float2 _actual, float _tolerance, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _tolerance, _what);
  Assert::AreEqual(_expected.y, _actual.y, _tolerance, _what);
}

// The rectangle the implementation derives from raw bounds: clamped to the viewport, then grown by the margin.
void ExpectClampedAndGrown(Float2 _rawMin, Float2 _rawMax, const SplatBounds& _bounds, const wchar_t* _what)
{
  const Float2 margin = MarginNdc();
  const Float2 expectedMin = NeuronCore::Max(_rawMin, {-1.0f, -1.0f}) - margin;
  const Float2 expectedMax = NeuronCore::Min(_rawMax, {1.0f, 1.0f}) + margin;
  Assert::IsTrue(_bounds.visible, _what);
  AreEqualFloat2(expectedMin, _bounds.minNdc, 1.0e-6f, _what);
  AreEqualFloat2(expectedMax, _bounds.maxNdc, 1.0e-6f, _what);
}

} // namespace

// The bounds of Design/Archive/SampleRenderer.md §9.2, as the splat vertex shaders' C++ twins compute them.
TEST_CLASS(SplatBoundsTests)
{
public:
  TEST_METHOD(QuadricBoundsAreTangentToTheSphere)
  {
    SeededRandom random(11u);
    for (std::uint32_t i = 0; i < 2000; ++i)
    {
      const PerspectiveView view = RandomPerspective(random);
      const float depth = random.Uniform(1.0f, 100.0f);
      const float radius = random.Uniform(0.01f, 0.9f) * depth;
      const Float3 center = view.position + view.forward * depth + view.right * (random.Uniform(-1.0f, 1.0f) * depth) +
                            view.up * (random.Uniform(-1.0f, 1.0f) * depth);
      Float2 minNdc{};
      Float2 maxNdc{};
      const std::wstring what = std::format(L"sphere {}", i);
      Assert::IsTrue(NeuronCore::QuadricBounds(center, radius, view, minNdc, maxNdc), what.c_str());

      // Each side of the rectangle, with the camera, spans a plane; the sphere must lie on the inner side of every such
      // plane and touch it. In view coordinates the plane through the edge x = X (in NDC) has normal (1, 0, -X tanX).
      const Float3 offset = center - view.position;
      const double x = NeuronCore::Dot(offset, view.right);
      const double y = NeuronCore::Dot(offset, view.up);
      const double z = NeuronCore::Dot(offset, view.forward);
      const double tanX = static_cast<double>(view.tanHalfFovY) * view.aspect;
      const double tanY = view.tanHalfFovY;
      const std::array<double, 4> signedDistances{
        (x - static_cast<double>(minNdc.x) * tanX * z) / std::sqrt(1.0 + minNdc.x * tanX * minNdc.x * tanX),
        (static_cast<double>(maxNdc.x) * tanX * z - x) / std::sqrt(1.0 + maxNdc.x * tanX * maxNdc.x * tanX),
        (y - static_cast<double>(minNdc.y) * tanY * z) / std::sqrt(1.0 + minNdc.y * tanY * minNdc.y * tanY),
        (static_cast<double>(maxNdc.y) * tanY * z - y) / std::sqrt(1.0 + maxNdc.y * tanY * maxNdc.y * tanY),
      };
      for (const double distance : signedDistances)
      {
        Assert::AreEqual(static_cast<double>(radius), distance, 1.0e-4 * (radius + depth), what.c_str());
      }
    }
  }

  TEST_METHOD(PreciseBoundsHoldEveryPointBeyondTheNearPlane)
  {
    SeededRandom random(12u);
    std::uint32_t clipped = 0;
    for (std::uint32_t i = 0; i < 1000; ++i)
    {
      const PerspectiveView view = RandomPerspective(random);
      const Box box = BoxInView(random, view, i % 2 == 1, i);
      Float2 minNdc{};
      Float2 maxNdc{};
      float nearest = 0.0f;
      if (!NeuronCore::PreciseBounds(box, view, minNdc, maxNdc, nearest))
      {
        continue;
      }
      clipped += nearest <= view.nearPlane ? 1u : 0u;

      // Points throughout the box, and its corners: every one beyond the near plane projects inside the rectangle, no
      // nearer than the nearest depth reported, and the corners beyond the plane reach the rectangle's edges wherever
      // no clipped edge does.
      const std::wstring what = std::format(L"box {}", i);
      const float tanX = view.tanHalfFovY * view.aspect;
      const float tanY = view.tanHalfFovY;
      for (std::uint32_t sample = 0; sample < 200; ++sample)
      {
        const Float3 corner{(sample & 1u) != 0u ? 1.0f : -1.0f, (sample & 2u) != 0u ? 1.0f : -1.0f, (sample & 4u) != 0u ? 1.0f : -1.0f};
        const Float3 local = sample < 8 ? corner : random.InBox({-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f});
        const Float3 point =
          box.center + box.axisX * (local.x * box.radius.x) + box.axisY * (local.y * box.radius.y) + box.axisZ * (local.z * box.radius.z);
        const Float3 offset = point - view.position;
        const float depth = NeuronCore::Dot(offset, view.forward);
        if (depth < view.nearPlane)
        {
          continue;
        }
        const Float2 ndc{NeuronCore::Dot(offset, view.right) / (depth * tanX), NeuronCore::Dot(offset, view.up) / (depth * tanY)};
        const float tolerance = 1.0e-4f * (1.0f + std::abs(ndc.x) + std::abs(ndc.y));
        Assert::IsTrue(ndc.x >= minNdc.x - tolerance && ndc.x <= maxNdc.x + tolerance, what.c_str());
        Assert::IsTrue(ndc.y >= minNdc.y - tolerance && ndc.y <= maxNdc.y + tolerance, what.c_str());
        Assert::IsTrue(depth >= nearest * (1.0f - 1.0e-5f), what.c_str());
      }
    }
    Logger::WriteMessage(std::format(L"{} boxes clipped by the near plane", clipped).c_str());
    Assert::IsTrue(clipped >= 100, L"clipping is exercised");
  }

  TEST_METHOD(PerspectiveSplatsCoverEveryHitAligned)
  {
    PerspectiveSplatsCoverEveryHit<false>(13u);
  }

  TEST_METHOD(PerspectiveSplatsCoverEveryHitOriented)
  {
    PerspectiveSplatsCoverEveryHit<true>(14u);
  }

  TEST_METHOD(OrthographicSplatsCoverEveryHitAligned)
  {
    OrthographicSplatsCoverEveryHit<false>(15u);
  }

  TEST_METHOD(OrthographicSplatsCoverEveryHitOriented)
  {
    OrthographicSplatsCoverEveryHit<true>(16u);
  }

  TEST_METHOD(ChoosesTheQuadricOnlyForSmallSpheres)
  {
    // The default view of §3: a voxel 520 units out in a 1,080-line image is a few pixels across.
    const PerspectiveView view = NeuronCore::MakePerspectiveView({0.5f, 127.5f, -520.0f}, {0.5f, 127.5f, 0.5f}, {0.0f, 1.0f, 0.0f},
                                                                 0.785398163f, NEAR_PLANE, WIDTH_PIXELS, HEIGHT_PIXELS);
    const Box distant = NeuronCore::MakeAxisAlignedBox({3.5f, 130.5f, 0.5f}, {0.5f, 0.5f, 0.5f});
    Float2 minNdc{};
    Float2 maxNdc{};
    Assert::IsTrue(NeuronCore::QuadricBounds(distant.center, NeuronCore::Length(distant.radius), view, minNdc, maxNdc));
    const SplatBounds distantBounds = NeuronCore::PerspectiveSplatBounds(distant, view);
    ExpectClampedAndGrown(minNdc, maxNdc, distantBounds, L"a distant voxel takes the quadric");
    Assert::AreEqual(NeuronCore::PerspectiveDepth(view, 520.5f - NeuronCore::Length(distant.radius)), distantBounds.depth, 1.0e-9f,
                     L"distant depth: its sphere's nearest point");

    // Three units out, the sphere spans more than 20 pixels.
    const Box closeBy = NeuronCore::MakeAxisAlignedBox({0.5f, 127.5f, -517.0f}, {0.5f, 0.5f, 0.5f});
    float nearest = 0.0f;
    Assert::IsTrue(NeuronCore::PreciseBounds(closeBy, view, minNdc, maxNdc, nearest));
    const SplatBounds closeBounds = NeuronCore::PerspectiveSplatBounds(closeBy, view);
    ExpectClampedAndGrown(minNdc, maxNdc, closeBounds, L"a close voxel takes the precise path");
    Assert::AreEqual(NeuronCore::PerspectiveDepth(view, 2.5f), closeBounds.depth, 1.0e-6f, L"close depth: its front face");
  }

  TEST_METHOD(CullsWhatCannotBeSeen)
  {
    const PerspectiveView view = NeuronCore::MakePerspectiveView({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 1.0f,
                                                                 NEAR_PLANE, WIDTH_PIXELS, HEIGHT_PIXELS);
    const Float3 unit{0.5f, 0.5f, 0.5f};
    Assert::IsFalse(NeuronCore::PerspectiveSplatBounds(NeuronCore::MakeAxisAlignedBox({0.0f, 0.0f, -5.0f}, unit), view).visible, L"behind");
    Assert::IsFalse(NeuronCore::PerspectiveSplatBounds(NeuronCore::MakeAxisAlignedBox({40.0f, 0.0f, 5.0f}, unit), view).visible, L"right");
    Assert::IsFalse(NeuronCore::PerspectiveSplatBounds(NeuronCore::MakeAxisAlignedBox({0.0f, -40.0f, 5.0f}, unit), view).visible, L"below");

    // A box around the camera covers the screen at the near plane: reversed-Z depth 1.
    const SplatBounds around = NeuronCore::PerspectiveSplatBounds(NeuronCore::MakeAxisAlignedBox({0.0f, 0.0f, 0.0f}, unit), view);
    Assert::IsTrue(around.visible, L"around");
    Assert::AreEqual(1.0f, around.depth, L"around: at the near plane");
    Assert::IsTrue(around.minNdc.x < -1.0f && around.minNdc.y < -1.0f && around.maxNdc.x > 1.0f && around.maxNdc.y > 1.0f, L"full screen");

    const OrthographicView sun = NeuronCore::MakeOrthographicView({0.0f, 50.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 10.0f,
                                                                  10.0f, 60.0f, WIDTH_PIXELS, HEIGHT_PIXELS);
    Assert::IsFalse(NeuronCore::OrthographicSplatBounds(NeuronCore::MakeAxisAlignedBox({0.0f, 52.0f, 0.0f}, unit), sun).visible, L"above");
    Assert::IsFalse(NeuronCore::OrthographicSplatBounds(NeuronCore::MakeAxisAlignedBox({0.0f, -12.0f, 0.0f}, unit), sun).visible,
                    L"beyond");
    Assert::IsFalse(NeuronCore::OrthographicSplatBounds(NeuronCore::MakeAxisAlignedBox({12.0f, 0.0f, 0.0f}, unit), sun).visible, L"aside");
    const SplatBounds straddling = NeuronCore::OrthographicSplatBounds(NeuronCore::MakeAxisAlignedBox({0.0f, 50.0f, 0.0f}, unit), sun);
    Assert::IsTrue(straddling.visible, L"straddling");
    Assert::AreEqual(0.0f, straddling.depth, L"straddling: at the near plane");
    const SplatBounds inside = NeuronCore::OrthographicSplatBounds(NeuronCore::MakeAxisAlignedBox({0.0f, 20.0f, 0.0f}, unit), sun);
    Assert::AreEqual(29.5f / 60.0f, inside.depth, 1.0e-7f, L"a box's top face");
  }
};

} // namespace NeuronCoreTests
