#include "pch.h"

#include "SceneModels.h"
#include "TestSupport.h"

#include "Explosion.h"
#include "Float3.h"
#include "Placement.h"
#include "Quaternion.h"
#include "RigidTransform.h"
#include "Sphere.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronClient::SampledDetonation;
using NeuronClient::SampledEntity;
using NeuronClient::SceneModels;
using NeuronCore::Float3;
using NeuronCore::Quaternion;

constexpr Quaternion IDENTITY{0.0f, 0.0f, 0.0f, 1.0f};

// Double-precision references.
struct Double3
{
  double x;
  double y;
  double z;
};

// A turn of 37 degrees about an axis off every coordinate plane, as a ship's might be.
[[nodiscard]] Quaternion Tilted()
{
  constexpr double HALF_ANGLE_RADIANS = 0.5 * 37.0 * 3.14159265358979323846 / 180.0;
  const double length = std::sqrt(0.4 * 0.4 + 0.5 * 0.5 + 0.3 * 0.3);
  const double sine = std::sin(HALF_ANGLE_RADIANS) / length;
  return {static_cast<float>(0.4 * sine), static_cast<float>(-0.5 * sine), static_cast<float>(0.3 * sine),
          static_cast<float>(std::cos(HALF_ANGLE_RADIANS))};
}

[[nodiscard]] Double3 Rotate(const NeuronCore::Rotation& _rotation, Double3 _vector) noexcept
{
  const NeuronCore::Rotation& r = _rotation;
  return {r.axisX.x * _vector.x + r.axisY.x * _vector.y + r.axisZ.x * _vector.z,
          r.axisX.y * _vector.x + r.axisY.y * _vector.y + r.axisZ.y * _vector.z,
          r.axisX.z * _vector.x + r.axisY.z * _vector.y + r.axisZ.z * _vector.z};
}

// Where _point, in _model's own space, lies in the world, in double: the entity's position, plus its rotation of the
// point less the middle of the model's box (Design/Archive/SpaceScene.md §5.1, §7.2).
[[nodiscard]] Double3 ReferencePoint(const NeuronCore::VoxModel& _model, Double3 _point, const SampledEntity& _entity)
{
  const NeuronCore::VoxelBounds bounds = NeuronCore::OccupiedBounds(_model).value_or(NeuronCore::VoxelBounds{});
  const auto middle = [](std::int32_t _lower, std::int32_t _upper)
  { return 0.5 * (static_cast<double>(_lower) + static_cast<double>(_upper)); };
  const Double3 turned = Rotate(NeuronCore::RotationOf(_entity.rotation),
                                {_point.x - middle(bounds.lower.x, bounds.upper.x), _point.y - middle(bounds.lower.y, bounds.upper.y),
                                 _point.z - middle(bounds.lower.z, bounds.upper.z)});
  return {_entity.position.x + turned.x, _entity.position.y + turned.y, _entity.position.z + turned.z};
}

// Where the center of _record, a record of part _part of _model, lies in the world.
[[nodiscard]] Double3 ReferenceCenter(const NeuronCore::VoxModel& _model, std::uint32_t _part, std::uint32_t _record,
                                      const SampledEntity& _entity)
{
  const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(_record);
  const NeuronCore::Int3 origin = _model.instances[_part].origin;
  return ReferencePoint(_model, {origin.x + voxel.x + 0.5, origin.y + voxel.y + 0.5, origin.z + voxel.z + 0.5}, _entity);
}

void AreClose(Double3 _expected, Float3 _actual, double _tolerance, const std::wstring& _what)
{
  Assert::AreEqual(_expected.x, static_cast<double>(_actual.x), _tolerance, (_what + L", x").c_str());
  Assert::AreEqual(_expected.y, static_cast<double>(_actual.y), _tolerance, (_what + L", y").c_str());
  Assert::AreEqual(_expected.z, static_cast<double>(_actual.z), _tolerance, (_what + L", z").c_str());
}

// Whether _inner lies inside _outer, to a hundredth of a voxel: single-precision rounding of coordinates of a few
// thousand.
[[nodiscard]] bool Holds(const NeuronCore::Sphere& _outer, const NeuronCore::Sphere& _inner) noexcept
{
  return NeuronCore::Length(_inner.center - _outer.center) + _inner.radius <= _outer.radius + 0.01f;
}

// Two copies of the random block, the second at its own origin: a model of two parts, as NVF's tree of parts will
// give (Design/Archive/SpaceScene.md §7.1).
[[nodiscard]] NeuronCore::VoxModel TwoPartModel()
{
  NeuronCore::VoxModel model = RandomBlock();
  const auto count = static_cast<std::uint32_t>(model.records.size());
  model.records.insert(model.records.end(), model.records.begin(), model.records.end());
  model.instances.push_back({{10, -3, 4}, {8, 8, 8}, count, count});
  return model;
}

} // namespace

// Design/Archive/SpaceScene.md §5.1, §7.1, §7.2 and §7.7: where an entity's placements put its voxels, whole and detonated.
TEST_CLASS(SceneModelsTests)
{
public:
  TEST_METHOD(PutsTheMiddleOfTheBoxAtTheEntitysPosition)
  {
    // A station at a whole position, turned by a quarter turn about the vertical: every voxel's center exact, the
    // placement aligned (S18, §7.2).
    const NeuronCore::VoxModel station = LoadMilitaryStation();
    const SceneModels models({&station, 1});
    Assert::AreEqual(199.1, static_cast<double>(models.Radius(0)), 0.05, L"the station's sphere, about the middle of its box");
    for (const Quaternion rotation : {IDENTITY, NeuronCore::QuaternionOf({{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}})})
    {
      const SampledEntity entity{1, 0, {-1287.0f, 212.0f, 941.0f}, rotation, {0.0f, 0.0f, 0.0f}, std::nullopt};
      std::vector<NeuronCore::Placement> placements;
      models.Place(entity, placements);
      Assert::AreEqual(std::size_t{1}, placements.size(), L"one part");
      Assert::IsTrue(NeuronCore::IsAlignedPlacement(placements[0]), L"aligned");
      const NeuronCore::Placement& placement = placements[0];
      for (std::uint32_t voxel = 0; voxel < placement.recordCount; voxel += 997)
      {
        const std::uint32_t record = station.records[placement.firstRecord + voxel];
        const NeuronCore::Box box = NeuronCore::PlacedVoxelBox(placement, voxel, record);
        AreClose(ReferenceCenter(station, 0, record, entity), box.center, 0.0, std::format(L"voxel {}", voxel));
      }
    }
  }

  TEST_METHOD(PlacesEveryPartOfItsModel)
  {
    const NeuronCore::VoxModel model = TwoPartModel();
    const NeuronCore::VoxModel block = RandomBlock();
    // A model before it, so that the parts' records start past the first model's.
    const std::vector<NeuronCore::VoxModel> scene{block, model};
    const SceneModels models(scene);
    const SampledEntity entity{4, 1, {350.25f, -75.5f, 1200.75f}, Tilted(), {0.0f, 0.0f, 0.0f}, std::nullopt};
    std::vector<NeuronCore::Placement> placements;
    models.Place(entity, placements);
    Assert::AreEqual(std::size_t{2}, placements.size(), L"one placement a part");
    for (std::uint32_t part = 0; part < 2; ++part)
    {
      const NeuronCore::Placement& placement = placements[part];
      Assert::AreEqual(static_cast<std::uint32_t>(block.records.size()) + model.instances[part].firstRecord, placement.firstRecord,
                       L"the part's records, after the first model's");
      Assert::AreEqual(1u, placement.paletteIndex, L"its model's palette");
      for (std::uint32_t voxel = 0; voxel < placement.recordCount; ++voxel)
      {
        const std::uint32_t record = model.records[model.instances[part].firstRecord + voxel];
        AreClose(ReferenceCenter(model, part, record, entity), NeuronCore::PlacedVoxelBox(placement, voxel, record).center, 1.0e-3,
                 std::format(L"part {}, voxel {}", part, voxel));
      }
    }
    Assert::IsTrue(NeuronCore::AssignVoxelIds(placements), L"ids for both parts");
  }

  TEST_METHOD(DetonatesFromTheMeanOfItsModelsVoxels)
  {
    const NeuronCore::VoxModel model = TwoPartModel();
    const SceneModels models({&model, 1});
    const Float3 velocity{12.0f, -3.0f, 44.0f};
    SampledEntity entity{8, 0, {-40.0f, 900.0f, 15.5f}, Tilted(), {0.0f, 0.0f, 0.0f}, std::nullopt};
    std::vector<NeuronCore::Placement> whole;
    models.Place(entity, whole);

    // At time 0 the debris is the whole model, to the bit, in every part (§7.7).
    entity.detonation = SampledDetonation{{8, 0xBEEFu, 40, velocity}, 0.0f};
    std::vector<NeuronCore::Placement> resting;
    models.Place(entity, resting);
    for (std::uint32_t part = 0; part < 2; ++part)
    {
      for (std::uint32_t voxel = 0; voxel < whole[part].recordCount; ++voxel)
      {
        const std::uint32_t record = model.records[model.instances[part].firstRecord + voxel];
        const Float3 expected = NeuronCore::PlacedVoxelBox(whole[part], voxel, record).center;
        const Float3 actual = NeuronCore::PlacedVoxelBox(resting[part], voxel, record).center;
        Assert::IsTrue(expected.x == actual.x && expected.y == actual.y && expected.z == actual.z,
                       std::format(L"part {}, voxel {} at rest", part, voxel).c_str());
      }
    }

    // Every part is blasted from one point, the mean of the model's voxels, and flies on with the entity's velocity.
    const Float3 centroid = NeuronCore::VoxelCentroid(model);
    const Double3 blastOrigin = ReferencePoint(model, {centroid.x, centroid.y, centroid.z}, entity);
    entity.detonation->seconds = 2.0f;
    std::vector<NeuronCore::Placement> flying;
    models.Place(entity, flying);
    for (std::uint32_t part = 0; part < 2; ++part)
    {
      Assert::IsTrue(flying[part].detonation.has_value(), L"detonated");
      const NeuronCore::PlacementDetonation detonation = flying[part].detonation.value_or(NeuronCore::PlacementDetonation{});
      const std::wstring what = std::format(L"part {}", part);
      AreClose(blastOrigin, NeuronCore::TransformPoint(flying[part].transform, detonation.parameters.blastOrigin), 1.0e-3,
               what + L": the blast origin");
      AreClose({velocity.x, velocity.y, velocity.z},
               NeuronCore::RotateVector(flying[part].transform.rotation, detonation.parameters.inheritedVelocity), 5.0e-4,
               what + L": the velocity");
      Assert::AreEqual(0xBEEFu, detonation.parameters.seed, (what + L": the seed").c_str());
      Assert::AreEqual(2.0f, detonation.timeSeconds, (what + L": the time").c_str());
      Assert::AreEqual(static_cast<std::size_t>(flying[part].recordCount), detonation.fragments.fragmentOf.size(),
                       (what + L": broken into its part's fragments").c_str());
    }
  }

  TEST_METHOD(ReachesEverythingItDraws)
  {
    // The station, whole and then detonated where it stands: its reach before the event holds everything it draws after,
    // so that the sun's view fitted around it need not move when it detonates (§10).
    const NeuronCore::VoxModel station = LoadMilitaryStation();
    const SceneModels models({&station, 1});
    SampledEntity entity{1, 0, {600.0f, -50.0f, -2200.0f}, IDENTITY, {0.0f, 0.0f, 0.0f}, std::nullopt};
    const NeuronCore::Sphere reach = models.Reach(entity);
    const NeuronCore::Sphere intact = models.Extent(entity);
    Assert::AreEqual(entity.position.x, intact.center.x, L"whole, about its position");
    Assert::AreEqual(models.Radius(0), intact.radius, L"whole, its model's sphere");
    Assert::IsTrue(Holds(reach, intact), L"the reach holds the whole station");

    // Through the flight to past the stop, 40.4 s (ADR-024).
    for (const float seconds : {0.0f, 0.5f, 2.0f, 6.0f, 12.0f, 60.0f})
    {
      entity.detonation = SampledDetonation{{1, 99u, 12, {0.0f, 0.0f, 0.0f}}, seconds};
      const std::wstring what = std::format(L"at {} s", seconds);
      const NeuronCore::Sphere extent = models.Extent(entity);
      Assert::IsTrue(Holds(reach, extent), (what + L": the reach holds the debris").c_str());
      Assert::IsTrue(Holds(models.Reach(entity), extent), (what + L": so does the debris's own reach").c_str());
      std::vector<NeuronCore::Placement> placements;
      models.Place(entity, placements);
      for (const NeuronCore::Placement& placement : placements)
      {
        Assert::IsTrue(Holds(extent, NeuronCore::PlacementSphere(placement)),
                       (what + L": the extent holds what the views cull by").c_str());
      }
    }
  }
};

} // namespace NeuronClientTests
