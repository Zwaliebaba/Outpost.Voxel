#include "pch.h"

#include "Box.h"
#include "Float3.h"
#include "Ray.h"
#include "SeededRandom.h"
#include "TraceHit.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Box;
using NeuronCore::Float3;

constexpr std::uint32_t RAY_COUNT = 20000;

// How far the slab reference grows and shrinks a box to decide that a ray passes too close to an edge or a corner, or
// starts too close to a face, for single and double precision to be expected to agree on which face it enters.
constexpr double AMBIGUITY = 1.0e-3;

// The brute-force reference: the classic slab test in double precision, in the box's own coordinates, tracking the
// slab the ray enters last, which is the face it enters through, and the slab it leaves first. The box is closed: a ray
// parallel to a slab and on its boundary is inside it, as it is under the inclusive face tests (§4.2, item 12).
struct SlabHit
{
  bool hit; // the ray starts outside the box and enters it
  double enter;
  double leave;
  int enterAxis;
  double enterSign; // the outward normal of the entry face along enterAxis
  int leaveAxis;
  double leaveSign; // the outward normal of the exit face along leaveAxis
};

[[nodiscard]] double BoxCoordinate(Float3 _vector, Float3 _axis) noexcept
{
  return static_cast<double>(_vector.x) * _axis.x + static_cast<double>(_vector.y) * _axis.y + static_cast<double>(_vector.z) * _axis.z;
}

[[nodiscard]] SlabHit Slabs(const Box& _box, Float3 _origin, Float3 _direction, double _grow) noexcept
{
  const std::array<Float3, 3> axes{_box.axisX, _box.axisY, _box.axisZ};
  const std::array<double, 3> radius{_box.radius.x + _grow, _box.radius.y + _grow, _box.radius.z + _grow};
  const Float3 center = _box.center;
  SlabHit slabs{false, -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), -1, 0.0, -1, 0.0};
  for (std::size_t a = 0; a < 3; ++a)
  {
    const double origin = BoxCoordinate(_origin, axes[a]) - BoxCoordinate(center, axes[a]);
    const double direction = BoxCoordinate(_direction, axes[a]);
    if (direction == 0.0)
    {
      if (std::abs(origin) > radius[a])
      {
        return slabs;
      }
      continue;
    }
    const double nearFace = (-(direction > 0.0 ? radius[a] : -radius[a]) - origin) / direction;
    const double farFace = ((direction > 0.0 ? radius[a] : -radius[a]) - origin) / direction;
    if (nearFace > slabs.enter)
    {
      slabs.enter = nearFace;
      slabs.enterAxis = static_cast<int>(a);
      slabs.enterSign = direction > 0.0 ? -1.0 : 1.0;
    }
    if (farFace < slabs.leave)
    {
      slabs.leave = farFace;
      slabs.leaveAxis = static_cast<int>(a);
      slabs.leaveSign = direction > 0.0 ? 1.0 : -1.0;
    }
  }
  slabs.hit = slabs.enter <= slabs.leave && slabs.enter >= 0.0;
  return slabs;
}

// Whether _point lies inside _box grown by _grow.
[[nodiscard]] bool Inside(const Box& _box, Float3 _point, double _grow) noexcept
{
  const std::array<Float3, 3> axes{_box.axisX, _box.axisY, _box.axisZ};
  const std::array<double, 3> radius{_box.radius.x + _grow, _box.radius.y + _grow, _box.radius.z + _grow};
  for (std::size_t a = 0; a < 3; ++a)
  {
    if (std::abs(BoxCoordinate(_point, axes[a]) - BoxCoordinate(_box.center, axes[a])) >= radius[a])
    {
      return false;
    }
  }
  return true;
}

// Whether single precision can be held to the slab reference for this ray: growing and shrinking the box by AMBIGUITY
// changes neither whether it is hit nor the face it is entered through.
[[nodiscard]] bool Unambiguous(const Box& _box, Float3 _origin, Float3 _direction) noexcept
{
  const SlabHit grown = Slabs(_box, _origin, _direction, AMBIGUITY);
  const SlabHit shrunk = Slabs(_box, _origin, _direction, -AMBIGUITY);
  return grown.hit == shrunk.hit && (!grown.hit || (grown.enterAxis == shrunk.enterAxis && grown.enterSign == shrunk.enterSign));
}

[[nodiscard]] Float3 FaceNormal(const Box& _box, int _axis, double _sign) noexcept
{
  const std::array<Float3, 3> axes{_box.axisX, _box.axisY, _box.axisZ};
  return axes[static_cast<std::size_t>(_axis)] * static_cast<float>(_sign);
}

void AreEqualFloat3(Float3 _expected, Float3 _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _what);
  Assert::AreEqual(_expected.y, _actual.y, _what);
  Assert::AreEqual(_expected.z, _actual.z, _what);
}

template <bool Oriented, bool CanStartInBox>
[[nodiscard]] bool Intersect(const Box& _box, Float3 _origin, Float3 _direction, float& _distance, Float3& _normal) noexcept
{
  return NeuronCore::IntersectBox<Oriented, CanStartInBox>(_box, _origin, _direction, NeuronCore::InverseDirection({_origin, _direction}),
                                                           _distance, _normal);
}

// A random box, and a random ray from outside it: a quarter with a random direction, most of which miss; half aimed at
// a point in or near the box; and a quarter aimed the same way with one or two direction components set to exactly
// zero, which for an axis-aligned box is a ray parallel to its faces.
template <bool Oriented> void RandomCase(SeededRandom& _random, std::uint32_t _index, Box& _box, Float3& _origin, Float3& _direction)
{
  Float3 axisX{1.0f, 0.0f, 0.0f};
  Float3 axisY{0.0f, 1.0f, 0.0f};
  Float3 axisZ{0.0f, 0.0f, 1.0f};
  if constexpr (Oriented)
  {
    _random.Rotation(axisX, axisY, axisZ);
  }
  _box = NeuronCore::MakeOrientedBox(_random.InBox({-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}),
                                     _random.InBox({0.25f, 0.25f, 0.25f}, {3.0f, 3.0f, 3.0f}), axisX, axisY, axisZ);
  do
  {
    _origin = _random.InBox({-20.0f, -20.0f, -20.0f}, {20.0f, 20.0f, 20.0f});
  } while (Inside(_box, _origin, AMBIGUITY));

  if (_index % 4 == 0)
  {
    _direction = _random.Direction();
    return;
  }
  const Float3 local = _random.InBox({-1.3f, -1.3f, -1.3f}, {1.3f, 1.3f, 1.3f}) * _box.radius;
  const Float3 target = _box.center + axisX * local.x + axisY * local.y + axisZ * local.z;
  _direction = NeuronCore::Normalize(target - _origin);
  if (_index % 4 == 3)
  {
    // Aim along the remaining components from a point that shares the target's zeroed coordinates.
    const std::uint32_t pattern = _random.Below(6);
    const bool zeroX = pattern == 0 || pattern == 3 || pattern == 4;
    const bool zeroY = pattern == 1 || pattern == 3 || pattern == 5;
    const bool zeroZ = pattern == 2 || pattern == 4 || pattern == 5;
    _direction = {zeroX ? 0.0f : _direction.x, zeroY ? 0.0f : _direction.y, zeroZ ? 0.0f : _direction.z};
    _origin = {zeroX ? target.x : _origin.x, zeroY ? target.y : _origin.y, zeroZ ? target.z : _origin.z};
  }
}

template <bool Oriented> void CompareWithSlabs(std::uint32_t _seed)
{
  SeededRandom random(_seed);
  std::uint32_t hits = 0;
  std::uint32_t misses = 0;
  std::uint32_t ambiguous = 0;
  std::uint32_t zeroComponentHits = 0;
  for (std::uint32_t i = 0; i < RAY_COUNT; ++i)
  {
    Box box{};
    Float3 origin{};
    Float3 direction{};
    RandomCase<Oriented>(random, i, box, origin, direction);
    if (!Unambiguous(box, origin, direction))
    {
      ++ambiguous;
      continue;
    }

    const SlabHit expected = Slabs(box, origin, direction, 0.0);
    float distance = 0.0f;
    Float3 normal{};
    const bool hit = Intersect<Oriented, false>(box, origin, direction, distance, normal);
    const std::wstring what =
      std::format(L"case {}: origin {} {} {}, direction {} {} {}", i, origin.x, origin.y, origin.z, direction.x, direction.y, direction.z);
    Assert::AreEqual(expected.hit, hit, what.c_str());
    if (!hit)
    {
      ++misses;
      continue;
    }
    ++hits;
    zeroComponentHits += direction.x == 0.0f || direction.y == 0.0f || direction.z == 0.0f ? 1u : 0u;
    Assert::AreEqual(expected.enter, static_cast<double>(distance), 1.0e-4 * (1.0 + expected.enter), what.c_str());
    AreEqualFloat3(FaceNormal(box, expected.enterAxis, expected.enterSign), normal, what.c_str());

    // Outside the box, CanStartInBox changes nothing.
    float outsideDistance = 0.0f;
    Float3 outsideNormal{};
    Assert::IsTrue(Intersect<Oriented, true>(box, origin, direction, outsideDistance, outsideNormal), what.c_str());
    Assert::AreEqual(distance, outsideDistance, what.c_str());
    AreEqualFloat3(normal, outsideNormal, what.c_str());
  }
  Logger::WriteMessage(
    std::format(L"{} hits, {} misses, {} ambiguous, {} hits along a zero component", hits, misses, ambiguous, zeroComponentHits).c_str());
  Assert::IsTrue(hits > RAY_COUNT / 4, L"enough hits");
  Assert::IsTrue(misses > RAY_COUNT / 8, L"enough misses");
  Assert::IsTrue(zeroComponentHits > RAY_COUNT / 20, L"enough hits along exactly-zero components");
  Assert::IsTrue(ambiguous < RAY_COUNT / 100, L"few ambiguous cases");
}

struct ExactCase
{
  Float3 origin;
  Float3 direction;
  bool hit;
  float distance;
  Float3 normal;
};

// The unit box at the origin. Every figure below is exact in single precision.
constexpr Box UNIT_BOX = NeuronCore::MakeAxisAlignedBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f});

template <bool Oriented> void ExpectExactCases(std::span<const ExactCase> _cases)
{
  for (std::size_t i = 0; i < _cases.size(); ++i)
  {
    const ExactCase& expected = _cases[i];
    float distance = 0.0f;
    Float3 normal{};
    const bool hit = Intersect<Oriented, false>(UNIT_BOX, expected.origin, expected.direction, distance, normal);
    const std::wstring what = std::format(L"case {}{}", i, Oriented ? L", oriented" : L"");
    Assert::AreEqual(expected.hit, hit, what.c_str());
    if (expected.hit)
    {
      Assert::AreEqual(expected.distance, distance, what.c_str());
      AreEqualFloat3(expected.normal, normal, what.c_str());
    }
  }
}

} // namespace

// Majercik et al. 2018, Listing 5, as NeuronCore's twin computes it (Design/Archive/SampleRenderer.md §14).
TEST_CLASS(BoxTests)
{
public:
  TEST_METHOD(AlignedMatchesTheSlabTest)
  {
    CompareWithSlabs<false>(20260927u);
  }

  TEST_METHOD(OrientedMatchesTheSlabTest)
  {
    CompareWithSlabs<true>(20260928u);
  }

  TEST_METHOD(HandlesExactlyZeroDirectionComponents)
  {
    const std::array<ExactCase, 9> cases{{
      {{-2.0f, 0.25f, 0.125f}, {1.0f, 0.0f, 0.0f}, true, 1.5f, {-1.0f, 0.0f, 0.0f}},
      {{2.0f, 0.25f, 0.125f}, {-1.0f, 0.0f, 0.0f}, true, 1.5f, {1.0f, 0.0f, 0.0f}},
      {{0.25f, -3.0f, -0.25f}, {0.0f, 1.0f, 0.0f}, true, 2.5f, {0.0f, -1.0f, 0.0f}},
      {{0.125f, 0.25f, 5.0f}, {0.0f, 0.0f, -1.0f}, true, 4.5f, {0.0f, 0.0f, 1.0f}},
      {{0.125f, 0.25f, 5.0f}, {-0.0f, 0.0f, -1.0f}, true, 4.5f, {0.0f, 0.0f, 1.0f}},
      {{0.125f, 0.25f, 5.0f}, {0.0f, -0.0f, -1.0f}, true, 4.5f, {0.0f, 0.0f, 1.0f}},
      {{-2.0f, -0.25f, 0.25f}, {0.5f, 0.125f, 0.0f}, true, 3.0f, {-1.0f, 0.0f, 0.0f}},
      {{-2.0f, 0.75f, 0.0f}, {1.0f, 0.0f, 0.0f}, false, 0.0f, {}},
      {{-2.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, false, 0.0f, {}},
    }};
    ExpectExactCases<false>(cases);
    ExpectExactCases<true>(cases);
  }

  TEST_METHOD(FacesIncludeTheirEdgesAndCorners)
  {
    // A ray that meets the box exactly on an edge or a corner hits it (§4.2, item 12), through the first face it touches
    // in x, y, z order; one that passes a representable step outside misses, and one a step inside hits the face it is
    // inside of.
    const float step = 1.0f / 1024.0f;
    const float overHalf = std::nextafter(0.5f, 1.0f);
    const std::array<ExactCase, 10> cases{{
      {{-2.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, true, 1.5f, {-1.0f, 0.0f, 0.0f}},
      {{-2.0f, overHalf, 0.0f}, {1.0f, 0.0f, 0.0f}, false, 0.0f, {}},
      {{-1.5f, -1.5f, 0.0f}, {1.0f, 1.0f, 0.0f}, true, 1.0f, {-1.0f, 0.0f, 0.0f}},
      {{-1.5f, 0.5f, 0.0f}, {1.0f, -1.0f, 0.0f}, true, 1.0f, {-1.0f, 0.0f, 0.0f}},
      {{-1.5f, 0.5f - step, 0.0f}, {1.0f, -1.0f, 0.0f}, false, 0.0f, {}},
      {{-1.5f, -1.5f + step, 0.0f}, {1.0f, 1.0f, 0.0f}, true, 1.0f, {-1.0f, 0.0f, 0.0f}},
      {{-1.5f + step, -1.5f, 0.0f}, {1.0f, 1.0f, 0.0f}, true, 1.0f, {0.0f, -1.0f, 0.0f}},
      {{-1.5f, -1.5f, -1.5f}, {1.0f, 1.0f, 1.0f}, true, 1.0f, {-1.0f, 0.0f, 0.0f}},
      {{-1.5f, -1.5f + step, -1.5f + step}, {1.0f, 1.0f, 1.0f}, true, 1.0f, {-1.0f, 0.0f, 0.0f}},
      {{1.5f, 1.5f, 1.5f + step}, {-1.0f, -1.0f, -1.0f}, true, 1.0f + step, {0.0f, 0.0f, 1.0f}},
    }};
    ExpectExactCases<false>(cases);
    ExpectExactCases<true>(cases);
  }

  TEST_METHOD(SeamsAreWatertight)
  {
    // Two unit boxes side by side, sharing the face x = 1. A ray down that face, and a ray through the edge their top
    // faces share, hit both at one distance, and the brute force keeps the lower index, as the depth test does.
    const std::array<Box, 2> boxes{NeuronCore::MakeAxisAlignedBox({0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}),
                                   NeuronCore::MakeAxisAlignedBox({1.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f})};
    const std::array<NeuronCore::Ray, 2> rays{{{{1.0f, 0.25f, 5.0f}, {0.0f, 0.0f, -1.0f}}, {{0.0f, 0.5f, 5.0f}, {0.25f, 0.0f, -1.0f}}}};
    for (std::size_t r = 0; r < rays.size(); ++r)
    {
      for (std::size_t b = 0; b < boxes.size(); ++b)
      {
        float distance = 0.0f;
        Float3 normal{};
        const std::wstring what = std::format(L"ray {}, box {}", r, b);
        Assert::IsTrue(Intersect<false, false>(boxes[b], rays[r].origin, rays[r].direction, distance, normal), what.c_str());
        Assert::AreEqual(4.0f, distance, what.c_str());
      }
      const NeuronCore::TraceHit nearest = NeuronCore::TraceBoxes<false>(boxes, rays[r], 0.0f);
      Assert::AreEqual(0u, nearest.voxel, std::format(L"ray {}", r).c_str());
      AreEqualFloat3({0.0f, 0.0f, 1.0f}, nearest.normal, std::format(L"ray {}", r).c_str());
    }
  }

  TEST_METHOD(StartingInsideTheBox)
  {
    // canStartInBox false: the documented wrong answer, no hit. True: the exit point, with the normal facing back.
    const Float3 origin{0.125f, 0.25f, -0.125f};
    float distance = 0.0f;
    Float3 normal{};
    Assert::IsFalse(Intersect<false, false>(UNIT_BOX, origin, {1.0f, 0.0f, 0.0f}, distance, normal), L"aligned, outside only");
    Assert::IsFalse(Intersect<true, false>(UNIT_BOX, origin, {1.0f, 0.0f, 0.0f}, distance, normal), L"oriented, outside only");
    Assert::IsTrue(Intersect<false, true>(UNIT_BOX, origin, {1.0f, 0.0f, 0.0f}, distance, normal), L"aligned, from inside");
    Assert::AreEqual(0.375f, distance);
    AreEqualFloat3({-1.0f, 0.0f, 0.0f}, normal, L"aligned exit normal");
    Assert::IsTrue(Intersect<true, true>(UNIT_BOX, origin, {0.0f, 0.0f, -1.0f}, distance, normal), L"oriented, from inside");
    Assert::AreEqual(0.375f, distance);
    AreEqualFloat3({0.0f, 0.0f, 1.0f}, normal, L"oriented exit normal");

    SeededRandom random(7u);
    std::uint32_t compared = 0;
    for (std::uint32_t i = 0; i < 2000; ++i)
    {
      Float3 axisX{};
      Float3 axisY{};
      Float3 axisZ{};
      random.Rotation(axisX, axisY, axisZ);
      const Box box = NeuronCore::MakeOrientedBox(random.InBox({-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f}),
                                                  random.InBox({0.25f, 0.25f, 0.25f}, {2.0f, 2.0f, 2.0f}), axisX, axisY, axisZ);
      const Float3 local = random.InBox({-0.9f, -0.9f, -0.9f}, {0.9f, 0.9f, 0.9f}) * box.radius;
      const Float3 start = box.center + axisX * local.x + axisY * local.y + axisZ * local.z;
      const Float3 direction = random.Direction();
      const std::wstring what = std::format(L"inside case {}", i);
      Assert::IsFalse(Intersect<true, false>(box, start, direction, distance, normal), what.c_str());
      Assert::IsTrue(Intersect<true, true>(box, start, direction, distance, normal), what.c_str());

      const SlabHit grown = Slabs(box, start, direction, AMBIGUITY);
      const SlabHit shrunk = Slabs(box, start, direction, -AMBIGUITY);
      if (grown.leaveAxis != shrunk.leaveAxis)
      {
        continue; // leaves near an edge
      }
      const SlabHit expected = Slabs(box, start, direction, 0.0);
      Assert::AreEqual(expected.leave, static_cast<double>(distance), 1.0e-4 * (1.0 + expected.leave), what.c_str());
      AreEqualFloat3(FaceNormal(box, expected.leaveAxis, -expected.leaveSign), normal, what.c_str());
      ++compared;
    }
    Assert::IsTrue(compared > 1900, L"most exits are away from edges");
  }
};

} // namespace NeuronCoreTests
