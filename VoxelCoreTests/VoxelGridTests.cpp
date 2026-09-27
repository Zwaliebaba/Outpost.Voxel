#include "pch.h"

#include "Box.h"
#include "Float3.h"
#include "Ray.h"
#include "SeededRandom.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelGrid.h"
#include "VoxelRecord.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VoxelCoreTests
{
namespace
{

using VoxelCore::Float3;
using VoxelCore::Int3;
using VoxelCore::TraceHit;

// Adds an instance to _model with every cell of _size occupied with probability _fill, in x, y, z order.
void AddRandomInstance(SeededRandom& _random, VoxelCore::VoxModel& _model, Int3 _origin, Int3 _size, float _fill)
{
  const auto first = static_cast<std::uint32_t>(_model.records.size());
  for (std::int32_t z = 0; z < _size.z; ++z)
  {
    for (std::int32_t y = 0; y < _size.y; ++y)
    {
      for (std::int32_t x = 0; x < _size.x; ++x)
      {
        if (_random.Uniform(0.0f, 1.0f) < _fill)
        {
          const auto color = static_cast<std::uint8_t>(_random.Below(VoxelCore::PALETTE_ENTRY_COUNT));
          _model.records.push_back(
            VoxelCore::PackVoxelRecord({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(z), color}));
        }
      }
    }
  }
  _model.instances.push_back({_origin, _size, first, static_cast<std::uint32_t>(_model.records.size()) - first});
}

// Two instances, the second overlapping the first.
[[nodiscard]] VoxelCore::VoxModel RandomModel(SeededRandom& _random)
{
  VoxelCore::VoxModel model{};
  AddRandomInstance(_random, model, {-3, 2, -1}, {12, 10, 8}, 0.35f);
  AddRandomInstance(_random, model, {3, 5, 2}, {9, 7, 6}, 0.5f);
  return model;
}

[[nodiscard]] std::vector<VoxelCore::Box> Boxes(const VoxelCore::VoxModel& _model)
{
  std::vector<VoxelCore::Box> boxes;
  for (const VoxelCore::ModelInstance& instance : _model.instances)
  {
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      boxes.push_back(VoxelCore::VoxelBox(instance, _model.records[instance.firstRecord + i]));
    }
  }
  return boxes;
}

void ExpectSameHit(const TraceHit& _expected, const TraceHit& _actual, const std::wstring& _ray)
{
  Assert::AreEqual(_expected.voxel, _actual.voxel, _ray.c_str());
  if (_expected.voxel != VoxelCore::NO_VOXEL)
  {
    Assert::AreEqual(_expected.distance, _actual.distance, _ray.c_str());
    Assert::AreEqual(_expected.normal.x, _actual.normal.x, _ray.c_str());
    Assert::AreEqual(_expected.normal.y, _actual.normal.y, _ray.c_str());
    Assert::AreEqual(_expected.normal.z, _actual.normal.z, _ray.c_str());
  }
}

[[nodiscard]] float RoundedTo(float _value, float _step) noexcept
{
  return std::round(_value / _step) * _step;
}

} // namespace

// The reference tracer of Design/SampleRenderer.md §14, held to the brute force it stands in for.
TEST_CLASS(VoxelGridTests)
{
public:
  TEST_METHOD(TracesAsBruteForceDoes)
  {
    SeededRandom random(41u);
    const VoxelCore::VoxModel model = RandomModel(random);
    const VoxelCore::VoxelGrid grid(model);
    const std::vector<VoxelCore::Box> boxes = Boxes(model);
    const Float3 lower{-3.0f, 2.0f, -1.0f};
    const Float3 upper{12.0f, 12.0f, 8.0f};
    const std::array<float, 4> minDistances{0.0f, 0.1f, 1.5f, 7.25f};

    std::uint32_t hits = 0;
    constexpr std::uint32_t RAY_COUNT = 6000;
    for (std::uint32_t i = 0; i < RAY_COUNT; ++i)
    {
      Float3 origin = random.InBox({-15.0f, -12.0f, -15.0f}, {25.0f, 25.0f, 20.0f});
      Float3 direction{};
      switch (i % 5)
      {
      case 0: // anywhere
        direction = random.Direction();
        break;
      case 1: // at a point in the grid
        direction = random.InBox(lower, upper) - origin;
        break;
      case 2: // at a corner or an edge of a cell, exactly
      {
        const Float3 target = random.InBox(lower, upper);
        direction = Float3{std::round(target.x), std::round(target.y), RoundedTo(target.z, 0.5f)} - origin;
        break;
      }
      case 3: // parallel to one or two axes, from a cell face, a cell centre or anywhere
      {
        const Float3 target = random.InBox(lower, upper);
        direction = target - origin;
        const std::uint32_t pattern = random.Below(6);
        const float step = std::array<float, 3>{1.0f, 0.5f, 0.0f}[random.Below(3)];
        const auto flatten = [step](float& _component, float& _position, float _target)
        {
          _component = 0.0f;
          _position = step > 0.0f ? RoundedTo(_target, step) : _target;
        };
        if (pattern == 0 || pattern == 3 || pattern == 4)
        {
          flatten(direction.x, origin.x, target.x);
        }
        if (pattern == 1 || pattern == 3 || pattern == 5)
        {
          flatten(direction.y, origin.y, target.y);
        }
        if (pattern == 2 || pattern == 4 || pattern == 5)
        {
          flatten(direction.z, origin.z, target.z);
        }
        break;
      }
      default: // from inside the grid
        origin = random.InBox(lower, upper);
        direction = random.Direction();
        break;
      }

      const VoxelCore::Ray ray{origin, direction};
      const float minDistance = minDistances[i % minDistances.size()];
      const TraceHit expected = VoxelCore::TraceBoxes<false>(boxes, ray, minDistance);
      const std::wstring what = std::format(L"ray {}: origin {} {} {}, direction {} {} {}, from {}", i, origin.x, origin.y, origin.z,
                                            direction.x, direction.y, direction.z, minDistance);
      ExpectSameHit(expected, grid.Trace(ray, minDistance), what);
      hits += expected.voxel != VoxelCore::NO_VOXEL ? 1u : 0u;
    }
    Logger::WriteMessage(std::format(L"{} of {} rays hit", hits, RAY_COUNT).c_str());
    Assert::IsTrue(hits > RAY_COUNT / 4, L"enough hits");
  }

  TEST_METHOD(FirstInstanceHoldsOverlappingCells)
  {
    VoxelCore::VoxModel model{};
    model.records = {VoxelCore::PackVoxelRecord({0, 0, 0, 1}), VoxelCore::PackVoxelRecord({2, 0, 0, 2}),
                     VoxelCore::PackVoxelRecord({0, 0, 0, 3})};
    model.instances = {{{5, 5, 5}, {3, 1, 1}, 0, 2}, {{7, 5, 5}, {1, 1, 1}, 2, 1}};
    const VoxelCore::VoxelGrid grid(model);
    Assert::AreEqual(1u, grid.VoxelAt({7, 5, 5}), L"the first instance's voxel holds the shared cell");
    Assert::AreEqual(0u, grid.VoxelAt({5, 5, 5}));
    Assert::AreEqual(VoxelCore::NO_VOXEL, grid.VoxelAt({6, 5, 5}));

    // The view pass draws both and keeps the first at equal depth; so does the tracer.
    const VoxelCore::Ray ray{{10.0f, 5.5f, 5.5f}, {-1.0f, 0.0f, 0.0f}};
    Assert::AreEqual(1u, grid.Trace(ray, 0.0f).voxel);
    Assert::AreEqual(1u, VoxelCore::TraceBoxes<false>(Boxes(model), ray, 0.0f).voxel);
  }

  TEST_METHOD(BreaksSeamTiesTowardTheLowerRecord)
  {
    // Two voxels share the face x = 1, and a ray runs down that face: both report a hit at one distance (§4.2, item 12).
    // The walk starts in the cell of record 1 and meets record 0 second, so only the tie rule can put record 0 first.
    VoxelCore::VoxModel model{};
    model.records = {VoxelCore::PackVoxelRecord({0, 0, 0, 0}), VoxelCore::PackVoxelRecord({1, 0, 0, 0})};
    model.instances = {{{0, 0, 0}, {2, 1, 1}, 0, 2}};
    const VoxelCore::VoxelGrid grid(model);
    const VoxelCore::Ray ray{{1.0f, 0.5f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    const TraceHit expected = VoxelCore::TraceBoxes<false>(Boxes(model), ray, 0.0f);
    Assert::AreEqual(0u, expected.voxel, L"brute force");
    ExpectSameHit(expected, grid.Trace(ray, 0.0f), L"the grid");
  }

  TEST_METHOD(SkipsWhatLiesNearerThanTheMinimum)
  {
    VoxelCore::VoxModel model{};
    model.records = {VoxelCore::PackVoxelRecord({0, 0, 0, 0}), VoxelCore::PackVoxelRecord({1, 0, 0, 0}),
                     VoxelCore::PackVoxelRecord({3, 0, 0, 0})};
    model.instances = {{{0, 0, 0}, {4, 1, 1}, 0, 3}};
    const VoxelCore::VoxelGrid grid(model);
    const VoxelCore::Ray ray{{-5.0f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}};

    const TraceHit first = grid.Trace(ray, 0.0f);
    Assert::AreEqual(0u, first.voxel);
    Assert::AreEqual(5.0f, first.distance);
    Assert::AreEqual(-1.0f, first.normal.x);

    // A minimum beyond the first voxel's face skips that voxel, and the next box's entry face counts although it lies
    // inside the solid: each box is intersected on its own, as each splat is.
    const TraceHit second = grid.Trace(ray, 5.5f);
    Assert::AreEqual(1u, second.voxel);
    Assert::AreEqual(6.0f, second.distance);

    const TraceHit third = grid.Trace(ray, 6.5f);
    Assert::AreEqual(2u, third.voxel);
    Assert::AreEqual(8.0f, third.distance);
    Assert::AreEqual(VoxelCore::NO_VOXEL, grid.Trace(ray, 8.5f).voxel);
  }

  TEST_METHOD(EmptyModelsMissEverything)
  {
    const VoxelCore::VoxelGrid grid(VoxelCore::VoxModel{});
    Assert::AreEqual(0, grid.Size().x);
    Assert::AreEqual(VoxelCore::NO_VOXEL, grid.VoxelAt({0, 0, 0}));
    Assert::AreEqual(VoxelCore::NO_VOXEL, grid.Trace({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, 0.0f).voxel);
  }

  TEST_METHOD(RefusesDegenerateRays)
  {
    SeededRandom random(42u);
    const VoxelCore::VoxModel model = RandomModel(random);
    const VoxelCore::VoxelGrid grid(model);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    Assert::AreEqual(VoxelCore::NO_VOXEL, grid.Trace({{0.0f, 5.0f, 3.0f}, {0.0f, 0.0f, 0.0f}}, 0.0f).voxel, L"no direction");
    Assert::AreEqual(VoxelCore::NO_VOXEL, grid.Trace({{0.0f, 5.0f, 3.0f}, {nan, 1.0f, 0.0f}}, 0.0f).voxel, L"a NaN direction");
    Assert::AreEqual(VoxelCore::NO_VOXEL, grid.Trace({{infinity, 5.0f, 3.0f}, {1.0f, 0.0f, 0.0f}}, 0.0f).voxel, L"an infinite origin");
  }
};

} // namespace VoxelCoreTests
