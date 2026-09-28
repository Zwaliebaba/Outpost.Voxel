#include "pch.h"

#include "CubeSymmetry.h"
#include "SeededRandom.h"

#include "Box.h"
#include "Explosion.h"
#include "Float3.h"
#include "Placement.h"
#include "RigidTransform.h"
#include "Sphere.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Placement;

[[nodiscard]] bool Same(Float3 _a, Float3 _b) noexcept
{
  return _a.x == _b.x && _a.y == _b.y && _a.z == _b.z;
}

[[nodiscard]] bool SameBox(const NeuronCore::Box& _a, const NeuronCore::Box& _b) noexcept
{
  return Same(_a.center, _b.center) && Same(_a.radius, _b.radius) && Same(_a.axisX, _b.axisX) && Same(_a.axisY, _b.axisY) &&
         Same(_a.axisZ, _b.axisZ);
}

// A model of _parts parts, each of up to _voxels distinct random voxels in a _cells-wide cube, as a .vox of several placed
// models is: its records part after part.
[[nodiscard]] NeuronCore::VoxModel RandomModel(SeededRandom& _random, std::uint32_t _parts, std::uint32_t _voxels, std::uint32_t _cells)
{
  NeuronCore::VoxModel model{};
  model.version = 150;
  for (std::uint32_t part = 0; part < _parts; ++part)
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
                                                             static_cast<std::uint8_t>(z), static_cast<std::uint8_t>(_random.Below(16u))}));
      }
    }
    const auto size = static_cast<std::int32_t>(_cells);
    model.instances.push_back({{0, 0, 0}, {size, size, size}, firstRecord, static_cast<std::uint32_t>(model.records.size()) - firstRecord});
  }
  return model;
}

// A rigid transform whose rotation is uniform and whose translation lies anywhere within _reach of the origin.
[[nodiscard]] NeuronCore::RigidTransform RandomTransform(SeededRandom& _random, float _reach)
{
  NeuronCore::RigidTransform transform{};
  _random.Rotation(transform.rotation.axisX, transform.rotation.axisY, transform.rotation.axisZ);
  transform.translation = _random.InBox({-_reach, -_reach, -_reach}, {_reach, _reach, _reach});
  return transform;
}

// A detonation with an inherited velocity and a seed, around a blast origin inside the part's box.
[[nodiscard]] NeuronCore::PlacementDetonation RandomDetonation(SeededRandom& _random, const Placement& _placement, float _timeSeconds)
{
  NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(_random.InBox(_placement.lower, _placement.upper));
  parameters.launchSpeed = _random.Uniform(10.0f, 60.0f);
  parameters.drag = _random.Uniform(0.5f, 3.0f);
  const Float3 heading = _random.Direction();
  parameters.inheritedVelocity = heading * _random.Uniform(0.0f, 40.0f);
  parameters.seed = _random.Below(1000u);
  return {parameters, _timeSeconds};
}

// The rest center of a record in its part's space: the middle of its cell.
[[nodiscard]] Float3 RestCenter(std::uint32_t _record) noexcept
{
  const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(_record);
  return {static_cast<float>(voxel.x) + 0.5f, static_cast<float>(voxel.y) + 0.5f, static_cast<float>(voxel.z) + 0.5f};
}

// _point under _transform in double precision, from the same float inputs.
[[nodiscard]] std::array<double, 3> TransformInDouble(const NeuronCore::RigidTransform& _transform, Float3 _point) noexcept
{
  const NeuronCore::Rotation& r = _transform.rotation;
  const auto row = [_point](float _translation, float _x, float _y, float _z) noexcept
  {
    return static_cast<double>(_translation) + static_cast<double>(_x) * _point.x + static_cast<double>(_y) * _point.y +
           static_cast<double>(_z) * _point.z;
  };
  return {row(_transform.translation.x, r.axisX.x, r.axisY.x, r.axisZ.x), row(_transform.translation.y, r.axisX.y, r.axisY.y, r.axisZ.y),
          row(_transform.translation.z, r.axisX.z, r.axisY.z, r.axisZ.z)};
}

// Whether _actual is within a few units in the last place of the double-precision _expected, for a sum whose largest
// term is at most _magnitude.
[[nodiscard]] bool WithinRounding(Float3 _actual, const std::array<double, 3>& _expected, double _magnitude) noexcept
{
  const double allowed = 4.0 * _magnitude / 16777216.0;
  return std::abs(static_cast<double>(_actual.x) - _expected[0]) <= allowed &&
         std::abs(static_cast<double>(_actual.y) - _expected[1]) <= allowed &&
         std::abs(static_cast<double>(_actual.z) - _expected[2]) <= allowed;
}

// The largest a coordinate of _transform's image of a point of a 256-voxel part can be: its translation plus √3 × 256.
[[nodiscard]] double Magnitude(const NeuronCore::RigidTransform& _transform) noexcept
{
  const Float3 t = _transform.translation;
  return std::max({std::abs(t.x), std::abs(t.y), std::abs(t.z)}) + 444.0;
}

} // namespace

// Design/SpaceScene.md §7: placements, the boxes they draw, their ids, the lookup from an id, and their spheres.
TEST_CLASS(PlacementTests)
{
public:
  TEST_METHOD(PlacesPartsFromTheirModels)
  {
    SeededRandom random(20261004u);
    const std::vector<NeuronCore::VoxModel> models{RandomModel(random, 2, 40, 12), RandomModel(random, 3, 30, 20)};
    const std::vector<std::uint32_t> records = NeuronCore::SceneRecords(models);
    const std::vector<std::uint32_t> firstRecords = NeuronCore::ModelFirstRecords(models);
    Assert::AreEqual(std::size_t{2}, firstRecords.size());
    Assert::AreEqual(0u, firstRecords[0]);
    Assert::AreEqual(static_cast<std::uint32_t>(models[0].records.size()), firstRecords[1]);
    Assert::AreEqual(models[0].records.size() + models[1].records.size(), records.size());

    for (std::uint32_t model = 0; model < models.size(); ++model)
    {
      for (std::uint32_t part = 0; part < models[model].instances.size(); ++part)
      {
        const NeuronCore::ModelInstance& instance = models[model].instances[part];
        const Placement placement =
          NeuronCore::PlacePart(models[model], model, firstRecords[model], part, {NeuronCore::IDENTITY_ROTATION, {}});
        const std::wstring what = std::format(L"model {}, part {}", model, part);
        Assert::AreEqual(firstRecords[model] + instance.firstRecord, placement.firstRecord, what.c_str());
        Assert::AreEqual(instance.recordCount, placement.recordCount, what.c_str());
        Assert::AreEqual(instance.firstRecord, placement.hashBase, what.c_str());
        Assert::AreEqual(model, placement.paletteIndex, what.c_str());
        Assert::IsFalse(placement.detonation.has_value(), what.c_str());

        // The box is tight: every voxel inside it, and each face touched by one.
        Float3 lowest{1.0e9f, 1.0e9f, 1.0e9f};
        Float3 highest{-1.0e9f, -1.0e9f, -1.0e9f};
        for (std::uint32_t i = 0; i < placement.recordCount; ++i)
        {
          Assert::AreEqual(models[model].records[instance.firstRecord + i], records[placement.firstRecord + i], what.c_str());
          const Float3 center = RestCenter(records[placement.firstRecord + i]);
          lowest = {std::min(lowest.x, center.x - 0.5f), std::min(lowest.y, center.y - 0.5f), std::min(lowest.z, center.z - 0.5f)};
          highest = {std::max(highest.x, center.x + 0.5f), std::max(highest.y, center.y + 0.5f), std::max(highest.z, center.z + 0.5f)};
        }
        Assert::IsTrue(Same(lowest, placement.lower) && Same(highest, placement.upper), what.c_str());
      }
    }
  }

  // §7.2: under a cube symmetry every product in a center is exact, so the center is the double-precision one rounded
  // once, bit for bit, whatever the translation; and the box is axis-aligned.
  TEST_METHOD(AlignedCentersAreExact)
  {
    SeededRandom random(20261005u);
    const NeuronCore::VoxModel model = RandomModel(random, 1, 200, 256);
    for (const CubeSymmetry& symmetry : CubeSymmetries())
    {
      if (!symmetry.proper)
      {
        continue;
      }
      const NeuronCore::RigidTransform transform{symmetry.rotation,
                                                 random.InBox({-16384.0f, -16384.0f, -16384.0f}, {16384.0f, 16384.0f, 16384.0f})};
      const Placement placement = NeuronCore::PlacePart(model, 0, 0, 0, transform);
      Assert::IsTrue(NeuronCore::IsAlignedPlacement(placement));
      for (std::uint32_t i = 0; i < placement.recordCount; ++i)
      {
        const NeuronCore::Box box = NeuronCore::PlacedVoxelBox(placement, i, model.records[i]);
        const std::array<double, 3> expected = TransformInDouble(transform, RestCenter(model.records[i]));
        const Float3 rounded{static_cast<float>(expected[0]), static_cast<float>(expected[1]), static_cast<float>(expected[2])};
        Assert::IsTrue(Same(rounded, box.center), std::format(L"voxel {}: the center is rounded once", i).c_str());
        Assert::IsTrue(SameBox(NeuronCore::MakeAxisAlignedBox(box.center, {0.5f, 0.5f, 0.5f}), box), L"axis-aligned");
      }
    }
  }

  // §7.2: under any other rotation the center is within rounding of the double-precision one, and the box turns with the
  // placement.
  TEST_METHOD(RigidCentersAreWithinRounding)
  {
    SeededRandom random(20261006u);
    const NeuronCore::VoxModel model = RandomModel(random, 1, 400, 256);
    for (std::uint32_t sample = 0; sample < 32u; ++sample)
    {
      const NeuronCore::RigidTransform transform = RandomTransform(random, 16384.0f);
      const Placement placement = NeuronCore::PlacePart(model, 0, 0, 0, transform);
      Assert::IsFalse(NeuronCore::IsAlignedPlacement(placement), L"a random rotation is no symmetry");
      for (std::uint32_t i = 0; i < placement.recordCount; ++i)
      {
        const NeuronCore::Box box = NeuronCore::PlacedVoxelBox(placement, i, model.records[i]);
        Assert::IsTrue(WithinRounding(box.center, TransformInDouble(transform, RestCenter(model.records[i])), Magnitude(transform)),
                       std::format(L"sample {}, voxel {}", sample, i).c_str());
        Assert::IsTrue(Same(box.axisX, transform.rotation.axisX) && Same(box.axisY, transform.rotation.axisY) &&
                         Same(box.axisZ, transform.rotation.axisZ),
                       L"the box's axes are the rotation's");
      }
    }
  }

  // §7.7 and §15: at time 0 a detonated placement draws exactly the boxes of the whole one drawn with the oriented splat.
  TEST_METHOD(DetonatedAtTimeZeroDrawsTheWholeBoxes)
  {
    SeededRandom random(20261007u);
    const NeuronCore::VoxModel model = RandomModel(random, 1, 300, 64);
    for (std::uint32_t sample = 0; sample < 16u; ++sample)
    {
      const Placement whole = NeuronCore::PlacePart(model, 0, 0, 0, RandomTransform(random, 4096.0f));
      Placement detonated = whole;
      detonated.detonation = RandomDetonation(random, whole, 0.0f);
      Assert::IsFalse(NeuronCore::IsAlignedPlacement(detonated), L"a detonated placement draws oriented");
      for (std::uint32_t i = 0; i < whole.recordCount; ++i)
      {
        Assert::IsTrue(
          SameBox(NeuronCore::PlacedVoxelBox(whole, i, model.records[i]), NeuronCore::PlacedVoxelBox(detonated, i, model.records[i])),
          std::format(L"sample {}, voxel {}", sample, i).c_str());
      }
    }
  }

  // §15: composed with a placement, the pose in the part's space agrees with a double-precision transform of it.
  TEST_METHOD(DetonatedPosesComposeWithThePlacement)
  {
    SeededRandom random(20261008u);
    const NeuronCore::VoxModel model = RandomModel(random, 2, 150, 48);
    for (std::uint32_t sample = 0; sample < 16u; ++sample)
    {
      const std::uint32_t part = random.Below(2u);
      Placement placement = NeuronCore::PlacePart(model, 0, 0, part, RandomTransform(random, 8192.0f));
      placement.detonation = RandomDetonation(random, placement, random.Uniform(0.0f, 4.0f));
      const NeuronCore::ModelInstance& instance = model.instances[part];
      for (std::uint32_t i = 0; i < placement.recordCount; ++i)
      {
        const std::uint32_t record = model.records[instance.firstRecord + i];
        const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(instance.firstRecord + i, RestCenter(record),
                                                                     placement.detonation->parameters, placement.detonation->timeSeconds);
        const NeuronCore::Box box = NeuronCore::PlacedVoxelBox(placement, i, record);
        const std::wstring what = std::format(L"sample {}, voxel {}", sample, i);
        const double magnitude = Magnitude(placement.transform) + NeuronCore::Length(pose.center);
        Assert::IsTrue(WithinRounding(box.center, TransformInDouble(placement.transform, pose.center), magnitude), what.c_str());
        const NeuronCore::RigidTransform turn{placement.transform.rotation, {0.0f, 0.0f, 0.0f}};
        Assert::IsTrue(WithinRounding(box.axisX, TransformInDouble(turn, pose.axisX), 2.0), what.c_str());
        Assert::IsTrue(WithinRounding(box.axisY, TransformInDouble(turn, pose.axisY), 2.0), what.c_str());
        Assert::IsTrue(WithinRounding(box.axisZ, TransformInDouble(turn, pose.axisZ), 2.0), what.c_str());
      }
    }
  }

  // §7.7, ADR-014: a detonated voxel hashes by its index within its model, so a model's debris does not depend on where
  // its records sit in the scene's buffer.
  TEST_METHOD(DebrisDoesNotDependOnTheSceneOrder)
  {
    SeededRandom random(20261009u);
    const NeuronCore::VoxModel first = RandomModel(random, 1, 60, 16);
    const NeuronCore::VoxModel second = RandomModel(random, 2, 60, 16);
    const std::vector<NeuronCore::VoxModel> forward{first, second};
    const std::vector<NeuronCore::VoxModel> backward{second, first};
    const NeuronCore::RigidTransform transform = RandomTransform(random, 100.0f);
    Placement ahead = NeuronCore::PlacePart(second, 1, NeuronCore::ModelFirstRecords(forward)[1], 1, transform);
    Placement behind = NeuronCore::PlacePart(second, 0, NeuronCore::ModelFirstRecords(backward)[0], 1, transform);
    Assert::IsTrue(ahead.firstRecord != behind.firstRecord, L"the part sits at two places in the two buffers");
    ahead.detonation = RandomDetonation(random, ahead, 1.5f);
    behind.detonation = ahead.detonation;
    const std::vector<std::uint32_t> aheadRecords = NeuronCore::SceneRecords(forward);
    const std::vector<std::uint32_t> behindRecords = NeuronCore::SceneRecords(backward);
    for (std::uint32_t i = 0; i < ahead.recordCount; ++i)
    {
      Assert::IsTrue(SameBox(NeuronCore::PlacedVoxelBox(ahead, i, aheadRecords[ahead.firstRecord + i]),
                             NeuronCore::PlacedVoxelBox(behind, i, behindRecords[behind.firstRecord + i])),
                     std::format(L"voxel {}", i).c_str());
    }
  }

  // §7.3: the ids are a running sum over the placements, and a frame whose ids would reach NO_VOXEL is refused.
  TEST_METHOD(AssignsIdsAsARunningSum)
  {
    std::vector<Placement> placements(5);
    const std::array<std::uint32_t, 5> counts{7u, 0u, 3u, 1000u, 1u};
    for (std::size_t i = 0; i < placements.size(); ++i)
    {
      placements[i].recordCount = counts[i];
      placements[i].firstVoxel = 12345u;
    }
    Assert::IsTrue(NeuronCore::AssignVoxelIds(placements));
    const std::array<std::uint32_t, 5> expected{0u, 7u, 7u, 10u, 1010u};
    for (std::size_t i = 0; i < placements.size(); ++i)
    {
      Assert::AreEqual(expected[i], placements[i].firstVoxel);
    }

    // The last id must stay below NO_VOXEL: 0xFFFFFFFF voxels fit, one more does not.
    std::vector<Placement> full(2);
    full[0].recordCount = 0x80000000u;
    full[1].recordCount = 0x7FFFFFFFu;
    Assert::IsTrue(NeuronCore::AssignVoxelIds(full));
    Assert::AreEqual(0x80000000u, full[1].firstVoxel);
    full[1].recordCount = 0x80000000u;
    full[1].firstVoxel = 99u;
    Assert::IsFalse(NeuronCore::AssignVoxelIds(full));
    Assert::AreEqual(99u, full[1].firstVoxel, L"a refused frame's placements are left as they were");
  }

  // §7.3: every id finds the placement that holds it, empty placements included, and through it its record and palette;
  // no other id finds any.
  TEST_METHOD(FindsThePlacementOfEveryId)
  {
    SeededRandom random(20261010u);
    Assert::AreEqual(NeuronCore::NO_PLACEMENT, NeuronCore::FindPlacement({}, 0u), L"no placements");
    for (std::uint32_t frame = 0; frame < 64u; ++frame)
    {
      std::vector<Placement> placements(1u + random.Below(60u));
      for (Placement& placement : placements)
      {
        placement.recordCount = random.Below(4u) == 0u ? 0u : random.Below(300u);
        placement.firstRecord = random.Below(100000u);
        placement.paletteIndex = random.Below(3u);
      }
      Assert::IsTrue(NeuronCore::AssignVoxelIds(placements));
      const std::uint32_t total = placements.back().firstVoxel + placements.back().recordCount;
      for (std::uint32_t voxel = 0; voxel < total; ++voxel)
      {
        const std::uint32_t found = NeuronCore::FindPlacement(placements, voxel);
        const std::wstring what = std::format(L"frame {}, voxel {}", frame, voxel);
        Assert::IsTrue(found < placements.size() && placements[found].firstVoxel <= voxel &&
                         voxel - placements[found].firstVoxel < placements[found].recordCount,
                       what.c_str());
        const std::optional<NeuronCore::PlacedVoxel> placed = NeuronCore::FindVoxel(placements, voxel);
        Assert::IsTrue(placed.has_value(), what.c_str());
        Assert::AreEqual(placements[found].firstRecord + (voxel - placements[found].firstVoxel), placed->record, what.c_str());
        Assert::AreEqual(placements[found].paletteIndex, placed->paletteIndex, what.c_str());
      }
      for (const std::uint32_t beyond : {total, total + 1u, NeuronCore::NO_VOXEL})
      {
        Assert::AreEqual(NeuronCore::NO_PLACEMENT, NeuronCore::FindPlacement(placements, beyond), std::format(L"frame {}", frame).c_str());
        Assert::IsFalse(NeuronCore::FindVoxel(placements, beyond).has_value(), std::format(L"frame {}", frame).c_str());
      }
    }
  }

  // §7.4: a placement's sphere holds every corner of every box it draws, whole or detonated, at any time.
  TEST_METHOD(SpheresHoldEverythingTheyPlace)
  {
    SeededRandom random(20261011u);
    const NeuronCore::VoxModel model = RandomModel(random, 1, 300, 40);
    for (std::uint32_t sample = 0; sample < 24u; ++sample)
    {
      Placement placement = NeuronCore::PlacePart(model, 0, 0, 0, RandomTransform(random, 8192.0f));
      if (sample % 2u == 1u)
      {
        placement.detonation = RandomDetonation(random, placement, random.Uniform(0.0f, 6.0f));
      }
      const NeuronCore::Sphere sphere = NeuronCore::PlacementSphere(placement);
      float farthest = 0.0f;
      for (std::uint32_t i = 0; i < placement.recordCount; ++i)
      {
        const NeuronCore::Box box = NeuronCore::PlacedVoxelBox(placement, i, model.records[i]);
        for (std::uint32_t corner = 0; corner < 8u; ++corner)
        {
          const float x = (corner & 1u) != 0u ? 0.5f : -0.5f;
          const float y = (corner & 2u) != 0u ? 0.5f : -0.5f;
          const float z = (corner & 4u) != 0u ? 0.5f : -0.5f;
          const Float3 point = box.center + box.axisX * x + box.axisY * y + box.axisZ * z;
          farthest = std::max(farthest, NeuronCore::Length(point - sphere.center));
        }
      }
      // Rounding at the translation's magnitude, and none beyond it.
      const float allowed = static_cast<float>(4.0 * Magnitude(placement.transform) / 16777216.0);
      Assert::IsTrue(farthest <= sphere.radius + allowed,
                     std::format(L"sample {}: a corner lies {} from the center, of {}", sample, farthest, sphere.radius).c_str());
    }
  }
};

} // namespace NeuronCoreTests
