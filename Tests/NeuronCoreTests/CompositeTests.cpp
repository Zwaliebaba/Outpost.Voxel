#include "pch.h"

#include "CubeSymmetry.h"
#include "SeededRandom.h"

#include "Composite.h"
#include "Explosion.h"
#include "Float3.h"
#include "Message.h"
#include "NvfImport.h"
#include "Quaternion.h"
#include "RigidTransform.h"
#include "SidePalette.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::CompositeComponent;
using NeuronCore::CompositeModel;
using NeuronCore::Int3;

constexpr NeuronCore::Quaternion IDENTITY{0.0f, 0.0f, 0.0f, 1.0f};

// A model of two parts, each of up to _voxels distinct random voxels in a cube _cells wide, the second at its own origin.
[[nodiscard]] NeuronCore::VoxModel TwoPartModel(SeededRandom& _random, std::uint32_t _voxels, std::uint32_t _cells)
{
  NeuronCore::VoxModel model{};
  model.version = 150;
  const std::array<Int3, 2> origins{{{0, 0, 0}, {-7, 11, 3}}};
  for (const Int3 origin : origins)
  {
    const auto firstRecord = static_cast<std::uint32_t>(model.records.size());
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> cells;
    for (std::uint32_t voxel = 0; voxel < _voxels; ++voxel)
    {
      const std::uint32_t x = _random.Below(_cells);
      const std::uint32_t y = _random.Below(_cells);
      const std::uint32_t z = _random.Below(_cells);
      if (cells.emplace(x, y, z).second)
      {
        model.records.push_back(NeuronCore::PackVoxelRecord({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y),
                                                             static_cast<std::uint8_t>(z), static_cast<std::uint8_t>(voxel % 16u)}));
      }
    }
    const auto size = static_cast<std::int32_t>(_cells);
    model.instances.push_back({origin, {size, size, size}, firstRecord, static_cast<std::uint32_t>(model.records.size()) - firstRecord});
  }
  return model;
}

// The quaternion a composite stores for the cube's rotation _rotation, from the NVF importer's table.
[[nodiscard]] NeuronCore::Quaternion Stored(const NeuronCore::Rotation& _rotation)
{
  const std::optional<NeuronCore::Quaternion> quaternion = NeuronCore::CubeRotationQuaternion(_rotation);
  Assert::IsTrue(quaternion.has_value(), L"one of the cube's rotations");
  return quaternion.value_or(IDENTITY);
}

// Calls _visit with every voxel of _composite as the doubled coordinates of its center in the composite's space: whole
// numbers, so that the reference below is exact. A rotation of the cube's is a signed permutation of the axes.
template <typename Visit>
void ForEachDoubledCenter(const std::vector<NeuronCore::VoxModel>& _models, const CompositeModel& _composite, const Visit& _visit)
{
  for (const CompositeComponent& component : _composite.components)
  {
    const NeuronCore::VoxModel& model = _models[component.model];
    const NeuronCore::Rotation rotation = NeuronCore::RotationOf(component.rotation);
    const auto turn = [&rotation](std::int64_t _x, std::int64_t _y, std::int64_t _z)
    {
      const auto row = [_x, _y, _z](float _a, float _b, float _c)
      { return static_cast<std::int64_t>(_a) * _x + static_cast<std::int64_t>(_b) * _y + static_cast<std::int64_t>(_c) * _z; };
      return std::array<std::int64_t, 3>{row(rotation.axisX.x, rotation.axisY.x, rotation.axisZ.x),
                                         row(rotation.axisX.y, rotation.axisY.y, rotation.axisZ.y),
                                         row(rotation.axisX.z, rotation.axisY.z, rotation.axisZ.z)};
    };
    for (const NeuronCore::ModelInstance& instance : model.instances)
    {
      for (std::uint32_t index = 0; index < instance.recordCount; ++index)
      {
        const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(model.records[instance.firstRecord + index]);
        const std::array<std::int64_t, 3> turned =
          turn(2 * (instance.origin.x + voxel.x) + 1, 2 * (instance.origin.y + voxel.y) + 1, 2 * (instance.origin.z + voxel.z) + 1);
        _visit(std::array<std::int64_t, 3>{turned[0] + 2 * std::int64_t{component.translation.x},
                                           turned[1] + 2 * std::int64_t{component.translation.y},
                                           turned[2] + 2 * std::int64_t{component.translation.z}});
      }
    }
  }
}

// Every component of _composite gets its own rotation and translation, drawn from _random: a rotation of the cube's and
// a translation within a few hundred voxels.
void Scatter(SeededRandom& _random, CompositeModel& _composite)
{
  const std::array<CubeSymmetry, 48> symmetries = CubeSymmetries();
  for (CompositeComponent& component : _composite.components)
  {
    CubeSymmetry symmetry{};
    do
    {
      symmetry = symmetries[_random.Below(48u)];
    } while (!symmetry.proper);
    component.rotation = Stored(symmetry.rotation);
    component.translation = {static_cast<std::int32_t>(_random.Below(600u)) - 300, static_cast<std::int32_t>(_random.Below(600u)) - 300,
                             static_cast<std::int32_t>(_random.Below(600u)) - 300};
  }
}

} // namespace

// Design/ADR/ADR-029: the geometry of a composite, which the server that places an entity and the client that draws it
// both measure through these functions, and the palettes of its sides.
TEST_CLASS(CompositeTests)
{
public:
  // A world of single models, as the space scene's, names model i by composite i, and measures it as the model itself,
  // to the bit: its entities stand and blast where they did before composites.
  TEST_METHOD(MeasuresASingleModelAsTheModel)
  {
    SeededRandom random(11);
    const std::vector<NeuronCore::VoxModel> models{TwoPartModel(random, 300, 9), TwoPartModel(random, 40, 20)};
    const std::vector<CompositeModel> composites = NeuronCore::SingleModelComposites(models.size());
    Assert::AreEqual(models.size(), composites.size());
    for (std::uint32_t index = 0; index < models.size(); ++index)
    {
      const CompositeModel& composite = composites[index];
      Assert::AreEqual(std::size_t{1}, composite.components.size(), L"one component");
      Assert::AreEqual(static_cast<std::uint16_t>(index), composite.components.front().model, L"composite i is model i");
      Assert::IsTrue(NeuronCore::IsIdentityComponent(composite.components.front()), L"where it is");
      const NeuronCore::VoxelBounds expected = NeuronCore::OccupiedBounds(models[index]).value_or(NeuronCore::VoxelBounds{});
      const NeuronCore::VoxelBounds bounds = NeuronCore::CompositeBounds(models, composite).value_or(NeuronCore::VoxelBounds{});
      Assert::IsTrue(expected.lower.x == bounds.lower.x && expected.lower.y == bounds.lower.y && expected.lower.z == bounds.lower.z &&
                       expected.upper.x == bounds.upper.x && expected.upper.y == bounds.upper.y && expected.upper.z == bounds.upper.z,
                     L"the model's box");
      const NeuronCore::Float3 centroid = NeuronCore::VoxelCentroid(models[index]);
      const NeuronCore::Float3 measured = NeuronCore::CompositeCentroid(models, composite);
      Assert::IsTrue(centroid.x == measured.x && centroid.y == measured.y && centroid.z == measured.z, L"the model's centroid, exactly");
    }
  }

  // The box and the centroid of components turned by the cube's rotations and moved by whole voxels, against a reference
  // over every voxel in whole numbers.
  TEST_METHOD(MeasuresTurnedAndMovedComponents)
  {
    SeededRandom random(29);
    const std::vector<NeuronCore::VoxModel> models{TwoPartModel(random, 200, 12), TwoPartModel(random, 60, 7), TwoPartModel(random, 1, 3)};
    for (std::uint32_t trial = 0; trial < 40; ++trial)
    {
      CompositeModel composite{{{0, {}, IDENTITY}, {1, {}, IDENTITY}, {2, {}, IDENTITY}, {1, {}, IDENTITY}}};
      Scatter(random, composite);
      constexpr std::int64_t LARGEST = std::numeric_limits<std::int64_t>::max();
      std::array<std::int64_t, 3> lower{LARGEST, LARGEST, LARGEST};
      std::array<std::int64_t, 3> upper{-LARGEST, -LARGEST, -LARGEST};
      std::array<double, 3> sum{};
      double count = 0.0;
      ForEachDoubledCenter(models, composite,
                           [&](const std::array<std::int64_t, 3>& _center)
                           {
                             for (std::size_t axis = 0; axis < 3; ++axis)
                             {
                               lower[axis] = std::min(lower[axis], (_center[axis] - 1) / 2);
                               upper[axis] = std::max(upper[axis], (_center[axis] + 1) / 2);
                               sum[axis] += 0.5 * static_cast<double>(_center[axis]);
                             }
                             count += 1.0;
                           });
      const std::optional<NeuronCore::VoxelBounds> bounds = NeuronCore::CompositeBounds(models, composite);
      Assert::IsTrue(bounds.has_value(), L"voxels");
      const NeuronCore::VoxelBounds box = bounds.value_or(NeuronCore::VoxelBounds{});
      const std::array<std::int32_t, 3> measuredLower{box.lower.x, box.lower.y, box.lower.z};
      const std::array<std::int32_t, 3> measuredUpper{box.upper.x, box.upper.y, box.upper.z};
      const NeuronCore::Float3 centroid = NeuronCore::CompositeCentroid(models, composite);
      const std::array<float, 3> measuredCentroid{centroid.x, centroid.y, centroid.z};
      for (std::size_t axis = 0; axis < 3; ++axis)
      {
        const std::wstring what = std::format(L"trial {}, axis {}", trial, axis);
        Assert::AreEqual(static_cast<std::int32_t>(lower[axis]), measuredLower[axis], (what + L": the box's lower corner").c_str());
        Assert::AreEqual(static_cast<std::int32_t>(upper[axis]), measuredUpper[axis], (what + L": the box's upper corner").c_str());
        Assert::AreEqual(sum[axis] / count, static_cast<double>(measuredCentroid[axis]), 1.0e-4, (what + L": the centroid").c_str());
      }
    }
  }

  // A component's transform is its rotation and then its translation, exact in single precision for a point of whole
  // or half voxels.
  TEST_METHOD(TurnsThenMovesAComponent)
  {
    const NeuronCore::Rotation quarterTurn{{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    const CompositeComponent component{3, {-12, 40, 7}, Stored(quarterTurn)};
    const NeuronCore::RigidTransform transform = NeuronCore::ComponentTransform(component);
    Assert::IsTrue(NeuronCore::IsCubeSymmetry(transform.rotation), L"exactly the cube's rotation");
    const NeuronCore::Float3 placed = NeuronCore::TransformPoint(transform, {2.5f, -1.0f, 4.5f});
    // (x, y, z) turns to (z, y, -x) under the quarter turn.
    Assert::IsTrue(placed.x == -12.0f + 4.5f && placed.y == 40.0f - 1.0f && placed.z == 7.0f - 2.5f, L"turned, then moved");
    Assert::IsFalse(NeuronCore::IsIdentityComponent(component), L"a turned component moves its model");
    Assert::IsFalse(NeuronCore::IsIdentityComponent({3, {0, 0, 1}, IDENTITY}), L"and so does a moved one");
  }

  // A composite of components without voxels has no box; one without a voxel among others does not change it.
  TEST_METHOD(IgnoresComponentsWithoutVoxels)
  {
    SeededRandom random(5);
    NeuronCore::VoxModel empty{};
    empty.instances.push_back({{0, 0, 0}, {1, 1, 1}, 0, 0});
    const std::vector<NeuronCore::VoxModel> models{TwoPartModel(random, 50, 6), empty};
    Assert::IsFalse(NeuronCore::CompositeBounds(models, {{{1, {0, 0, 0}, IDENTITY}}}).has_value(), L"nothing to stand in");
    const CompositeModel alone{{{0, {4, 0, -2}, IDENTITY}}};
    const CompositeModel both{{{0, {4, 0, -2}, IDENTITY}, {1, {900, 900, 900}, IDENTITY}}};
    const NeuronCore::VoxelBounds expected = NeuronCore::CompositeBounds(models, alone).value_or(NeuronCore::VoxelBounds{});
    const NeuronCore::VoxelBounds bounds = NeuronCore::CompositeBounds(models, both).value_or(NeuronCore::VoxelBounds{});
    Assert::IsTrue(expected.lower.x == bounds.lower.x && expected.upper.z == bounds.upper.z, L"the empty model adds nothing");
    const NeuronCore::Float3 a = NeuronCore::CompositeCentroid(models, alone);
    const NeuronCore::Float3 b = NeuronCore::CompositeCentroid(models, both);
    Assert::IsTrue(a.x == b.x && a.y == b.y && a.z == b.z, L"nor to the centroid");
  }

  // G24: a side's palette shows the side's color in its entry and keeps everything else of the author's.
  TEST_METHOD(PaintsTheSidesEntryAlone)
  {
    std::array<NeuronCore::PaletteEntry, NeuronCore::PALETTE_ENTRY_COUNT> palette{};
    for (std::uint32_t entry = 0; entry < palette.size(); ++entry)
    {
      const auto value = static_cast<std::uint8_t>(10 * entry);
      palette[entry] = {value,
                        static_cast<std::uint8_t>(value + 1),
                        static_cast<std::uint8_t>(value + 2),
                        200,
                        entry % 2 == 0,
                        0.25f * static_cast<float>(entry),
                        0.5f};
    }
    const NeuronCore::SideColor color{220, 80, 60};
    const std::array<NeuronCore::PaletteEntry, NeuronCore::PALETTE_ENTRY_COUNT> side = NeuronCore::SidePalette(palette, color);
    for (std::uint32_t entry = 0; entry < palette.size(); ++entry)
    {
      const NeuronCore::PaletteEntry& before = palette[entry];
      const NeuronCore::PaletteEntry& after = side[entry];
      const std::wstring what = std::format(L"entry {}", entry + 1);
      const bool painted = entry + 1 == NeuronCore::SIDE_PALETTE_ENTRY;
      Assert::IsTrue(after.red == (painted ? color.red : before.red) && after.green == (painted ? color.green : before.green) &&
                       after.blue == (painted ? color.blue : before.blue),
                     (what + L": the color").c_str());
      Assert::IsTrue(after.alpha == before.alpha && after.emissive == before.emissive && after.emit == before.emit &&
                       after.flux == before.flux,
                     (what + L": the material").c_str());
    }
    Assert::AreEqual(16u, NeuronCore::SIDE_PALETTE_ENTRY, L"the last entry, so the fifteen before it stay the author's");
  }

  // Each model's own palette, then its variant for each side, model after model; then the remembered variant of each, in
  // the same order (Design/ADR/ADR-032): every pair of a model and a side has its own palette and its remembered one, and
  // together they fill the scene's palettes without a gap.
  TEST_METHOD(IndexesEachModelsPaletteForEachSide)
  {
    constexpr std::size_t MODELS = 3;
    for (const std::size_t sides : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{255}})
    {
      std::vector<bool> seen(2 * MODELS * (sides + 1), false);
      for (std::uint32_t model = 0; model < MODELS; ++model)
      {
        Assert::AreEqual(model, NeuronCore::SidePaletteIndex(model, 0, 0), L"without sides, a model's own index");
        for (std::uint32_t side = 0; side <= sides; ++side)
        {
          const std::uint32_t index = NeuronCore::SidePaletteIndex(model, side, sides);
          const std::uint32_t remembered = NeuronCore::RememberedPaletteIndex(model, side, sides, MODELS);
          const std::wstring what = std::format(L"{} sides: model {}, side {}", sides, model, side);
          Assert::IsTrue(index < MODELS * (sides + 1) && !seen[index], what.c_str());
          Assert::AreEqual(index + static_cast<std::uint32_t>(MODELS * (sides + 1)), remembered, (what + L", remembered").c_str());
          seen[index] = true;
          seen[remembered] = true;
        }
      }
      Assert::IsTrue(std::ranges::all_of(seen, [](bool _seen) { return _seen; }), L"no gap");
    }
  }
};

} // namespace NeuronCoreTests
