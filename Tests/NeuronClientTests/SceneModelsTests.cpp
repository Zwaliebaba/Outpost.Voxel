#include "pch.h"

#include "SceneModels.h"
#include "TestSupport.h"

#include "Composite.h"
#include "Explosion.h"
#include "Float3.h"
#include "Message.h"
#include "Placement.h"
#include "Quaternion.h"
#include "RigidTransform.h"
#include "Sphere.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
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

// _models, each alone and where it is, as a world of single models names them, with no side.
[[nodiscard]] SceneModels Measured(std::span<const NeuronCore::VoxModel> _models)
{
  return SceneModels(_models, NeuronCore::SingleModelComposites(_models.size()), 0);
}

// A hull and a module fitted to it, as a design fits one at its mount (Design/ADR/ADR-029): the random block, model 0,
// where it is; and the two-part model, model 1, turned by a symmetry of the cube that moves every axis and moved by whole
// voxels.
[[nodiscard]] NeuronCore::CompositeModel HullAndModule()
{
  const NeuronCore::Rotation turn{{0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
  return {{{0, {0, 0, 0}, IDENTITY}, {1, {3, 9, -2}, NeuronCore::QuaternionOf(turn)}}};
}

// The model and the part of each of HullAndModule's placements, in the order SceneModels places them.
constexpr std::array<std::array<std::uint32_t, 2>, 3> MODEL_PARTS{{{0, 0}, {1, 0}, {1, 1}}};

// Where the center of _record, a record of part _part of the model _component places, lies in the composite's space, in
// double: the component's turn of the voxel's center in its model, plus its translation.
[[nodiscard]] Double3 ComponentCenter(const NeuronCore::VoxModel& _model, const NeuronCore::CompositeComponent& _component,
                                      std::uint32_t _part, std::uint32_t _record)
{
  const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(_record);
  const NeuronCore::Int3 origin = _model.instances[_part].origin;
  const Double3 turned =
    Rotate(NeuronCore::RotationOf(_component.rotation), {origin.x + voxel.x + 0.5, origin.y + voxel.y + 0.5, origin.z + voxel.z + 0.5});
  return {turned.x + _component.translation.x, turned.y + _component.translation.y, turned.z + _component.translation.z};
}

// Calls _visit with every voxel's center in _composite's space, in double, component after component and part after part.
template <typename Visit>
void ForEachCenter(std::span<const NeuronCore::VoxModel> _models, const NeuronCore::CompositeModel& _composite, const Visit& _visit)
{
  for (const NeuronCore::CompositeComponent& component : _composite.components)
  {
    const NeuronCore::VoxModel& model = _models[component.model];
    for (std::uint32_t part = 0; part < model.instances.size(); ++part)
    {
      const NeuronCore::ModelInstance& instance = model.instances[part];
      for (std::uint32_t voxel = 0; voxel < instance.recordCount; ++voxel)
      {
        _visit(ComponentCenter(model, component, part, model.records[instance.firstRecord + voxel]));
      }
    }
  }
}

// The box around every voxel of _composite, in its space, in double: its voxels' centers, and half a voxel beyond them.
struct DoubleBox
{
  Double3 lower;
  Double3 upper;
};

[[nodiscard]] DoubleBox CompositeBox(std::span<const NeuronCore::VoxModel> _models, const NeuronCore::CompositeModel& _composite)
{
  constexpr double LARGEST = std::numeric_limits<double>::max();
  DoubleBox box{{LARGEST, LARGEST, LARGEST}, {-LARGEST, -LARGEST, -LARGEST}};
  ForEachCenter(
    _models, _composite,
    [&box](Double3 _center)
    {
      box.lower = {std::min(box.lower.x, _center.x - 0.5), std::min(box.lower.y, _center.y - 0.5), std::min(box.lower.z, _center.z - 0.5)};
      box.upper = {std::max(box.upper.x, _center.x + 0.5), std::max(box.upper.y, _center.y + 0.5), std::max(box.upper.z, _center.z + 0.5)};
    });
  return box;
}

// Where _point, in the composite's space, lies in the world, in double: the entity's position, plus its rotation of the
// point less the middle of the composite's box, _box.
[[nodiscard]] Double3 CompositePoint(const DoubleBox& _box, Double3 _point, const SampledEntity& _entity)
{
  const Double3 turned = Rotate(NeuronCore::RotationOf(_entity.rotation),
                                {_point.x - 0.5 * (_box.lower.x + _box.upper.x), _point.y - 0.5 * (_box.lower.y + _box.upper.y),
                                 _point.z - 0.5 * (_box.lower.z + _box.upper.z)});
  return {_entity.position.x + turned.x, _entity.position.y + turned.y, _entity.position.z + turned.z};
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
    const SceneModels models = Measured({&station, 1});
    Assert::AreEqual(199.1, static_cast<double>(models.Radius(0)), 0.05, L"the station's sphere, about the middle of its box");
    for (const Quaternion rotation : {IDENTITY, NeuronCore::QuaternionOf({{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}})})
    {
      const SampledEntity entity{1, 0, 0, {-1287.0f, 212.0f, 941.0f}, rotation, {0.0f, 0.0f, 0.0f}, std::nullopt};
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
    const SceneModels models = Measured(scene);
    const SampledEntity entity{4, 1, 0, {350.25f, -75.5f, 1200.75f}, Tilted(), {0.0f, 0.0f, 0.0f}, std::nullopt};
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
    const SceneModels models = Measured({&model, 1});
    const Float3 velocity{12.0f, -3.0f, 44.0f};
    SampledEntity entity{8, 0, 0, {-40.0f, 900.0f, 15.5f}, Tilted(), {0.0f, 0.0f, 0.0f}, std::nullopt};
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
    const SceneModels models = Measured({&station, 1});
    SampledEntity entity{1, 0, 0, {600.0f, -50.0f, -2200.0f}, IDENTITY, {0.0f, 0.0f, 0.0f}, std::nullopt};
    const NeuronCore::Sphere reach = models.Reach(entity);
    const NeuronCore::Sphere intact = models.Extent(entity);
    Assert::AreEqual(entity.position.x, intact.center.x, L"whole, about its position");
    Assert::AreEqual(models.Radius(0), intact.radius, L"whole, its model's sphere");
    Assert::IsTrue(Holds(reach, intact), L"the reach holds the whole station");

    // Through the flight to past the stop, 40.5 s (ADR-024).
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

  TEST_METHOD(PlacesEachComponentWhereItsCompositePutsIt)
  {
    // Design/ADR/ADR-029: a composite's module turned and moved onto its hull, about the middle of the composite's box.
    const std::vector<NeuronCore::VoxModel> scene{RandomBlock(), TwoPartModel()};
    const NeuronCore::CompositeModel composite = HullAndModule();
    const SceneModels models(scene, {&composite, 1}, 0);
    const DoubleBox box = CompositeBox(scene, composite);
    const double halfDiagonal = 0.5 * std::sqrt((box.upper.x - box.lower.x) * (box.upper.x - box.lower.x) +
                                                (box.upper.y - box.lower.y) * (box.upper.y - box.lower.y) +
                                                (box.upper.z - box.lower.z) * (box.upper.z - box.lower.z));
    Assert::AreEqual(halfDiagonal, static_cast<double>(models.Radius(0)), 1.0e-5, L"the sphere, about the middle of the box");

    // Tilted at a fractional position, to rounding; and turned by a quarter turn at a whole position, as a station is:
    // every product exact, and every placement aligned, the module's included (S18, §7.2).
    const Quaternion quarterTurn = NeuronCore::QuaternionOf({{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    const std::array<SampledEntity, 2> entities{
      SampledEntity{3, 0, 0, {350.25f, -75.5f, 1200.75f}, Tilted(), {0.0f, 0.0f, 0.0f}, std::nullopt},
      SampledEntity{3, 0, 0, {-1287.0f, 212.0f, 941.0f}, quarterTurn, {0.0f, 0.0f, 0.0f}, std::nullopt}};
    for (std::size_t which = 0; which < entities.size(); ++which)
    {
      const SampledEntity& entity = entities[which];
      const bool isStation = which == 1;
      std::vector<NeuronCore::Placement> placements;
      models.Place(entity, placements);
      Assert::AreEqual(std::size_t{3}, placements.size(), L"the hull's part and the module's two");
      std::size_t index = 0;
      for (const NeuronCore::CompositeComponent& component : composite.components)
      {
        const NeuronCore::VoxModel& model = scene[component.model];
        for (std::uint32_t part = 0; part < model.instances.size(); ++part)
        {
          const NeuronCore::Placement& placement = placements[index++];
          const std::wstring what = std::format(L"entity {}, model {}, part {}", which, component.model, part);
          Assert::AreEqual(isStation, NeuronCore::IsAlignedPlacement(placement), (what + L": aligned").c_str());
          for (std::uint32_t voxel = 0; voxel < placement.recordCount; ++voxel)
          {
            const std::uint32_t record = model.records[model.instances[part].firstRecord + voxel];
            AreClose(CompositePoint(box, ComponentCenter(model, component, part, record), entity),
                     NeuronCore::PlacedVoxelBox(placement, voxel, record).center, isStation ? 0.0 : 1.0e-3,
                     std::format(L"{}, voxel {}", what, voxel));
          }
        }
      }
    }
  }

  TEST_METHOD(DrawsWithItsSidesPalettes)
  {
    // Each model's own palette, then its variant for each side, model after model (NeuronCore::SidePaletteIndex).
    const std::vector<NeuronCore::VoxModel> scene{RandomBlock(), TwoPartModel()};
    const NeuronCore::CompositeModel composite = HullAndModule();
    const SceneModels models(scene, {&composite, 1}, 2);
    for (std::uint8_t side = 0; side <= 2; ++side)
    {
      std::vector<NeuronCore::Placement> placements;
      models.Place({5, 0, side, {0.0f, 0.0f, 0.0f}, IDENTITY, {0.0f, 0.0f, 0.0f}, std::nullopt}, placements);
      const std::array<std::uint32_t, 3> expected{side, 3u + side, 3u + side};
      for (std::size_t index = 0; index < placements.size(); ++index)
      {
        Assert::AreEqual(expected[index], placements[index].paletteIndex, std::format(L"side {}, placement {}", side, index).c_str());
      }
    }
  }

  TEST_METHOD(DetonatesACompositeFromTheMeanOfItsVoxels)
  {
    const std::vector<NeuronCore::VoxModel> scene{RandomBlock(), TwoPartModel()};
    const NeuronCore::CompositeModel composite = HullAndModule();
    const SceneModels models(scene, {&composite, 1}, 1);
    const Float3 velocity{-8.0f, 21.0f, 3.5f};
    SampledEntity entity{2, 0, 1, {75.5f, -410.0f, 3.25f}, Tilted(), velocity, std::nullopt};
    std::vector<NeuronCore::Placement> whole;
    models.Place(entity, whole);

    // At time 0 the debris is the whole composite, to the bit, in every part of every component (§7.7).
    entity.detonation = SampledDetonation{{2, 0x5EEDu, 90, velocity}, 0.0f};
    std::vector<NeuronCore::Placement> resting;
    models.Place(entity, resting);
    Assert::AreEqual(whole.size(), resting.size(), L"as many placements");
    const std::vector<std::uint32_t> records = NeuronCore::SceneRecords(scene);
    for (std::size_t index = 0; index < whole.size(); ++index)
    {
      for (std::uint32_t voxel = 0; voxel < whole[index].recordCount; ++voxel)
      {
        const std::uint32_t record = records[whole[index].firstRecord + voxel];
        const Float3 expected = NeuronCore::PlacedVoxelBox(whole[index], voxel, record).center;
        const Float3 actual = NeuronCore::PlacedVoxelBox(resting[index], voxel, record).center;
        Assert::IsTrue(expected.x == actual.x && expected.y == actual.y && expected.z == actual.z,
                       std::format(L"placement {}, voxel {} at rest", index, voxel).c_str());
      }
    }

    // Every part of every component is blasted from one point, the mean of the composite's voxels, and flies on with the
    // entity's velocity.
    Double3 sum{0.0, 0.0, 0.0};
    double count = 0.0;
    ForEachCenter(scene, composite,
                  [&sum, &count](Double3 _center)
                  {
                    sum = {sum.x + _center.x, sum.y + _center.y, sum.z + _center.z};
                    count += 1.0;
                  });
    const DoubleBox box = CompositeBox(scene, composite);
    const Double3 blastOrigin = CompositePoint(box, {sum.x / count, sum.y / count, sum.z / count}, entity);
    entity.detonation->seconds = 2.0f;
    std::vector<NeuronCore::Placement> flying;
    models.Place(entity, flying);
    const NeuronCore::Sphere reach = models.Reach(entity);
    for (std::size_t index = 0; index < flying.size(); ++index)
    {
      const NeuronCore::PlacementDetonation detonation = flying[index].detonation.value_or(NeuronCore::PlacementDetonation{});
      const std::wstring what = std::format(L"placement {}", index);
      AreClose(blastOrigin, NeuronCore::TransformPoint(flying[index].transform, detonation.parameters.blastOrigin), 1.0e-3,
               what + L": the blast origin");
      AreClose({velocity.x, velocity.y, velocity.z},
               NeuronCore::RotateVector(flying[index].transform.rotation, detonation.parameters.inheritedVelocity), 5.0e-4,
               what + L": the velocity");
      Assert::AreEqual(0x5EEDu, detonation.parameters.seed, (what + L": the seed").c_str());
      Assert::IsTrue(Holds(reach, NeuronCore::PlacementSphere(flying[index])), (what + L": within the reach").c_str());
      // Each part breaks into the fragments its own model breaks into (Design/ADR/ADR-024).
      const NeuronCore::PartFragments expected = models.Fragments().Part(MODEL_PARTS[index][0], MODEL_PARTS[index][1]);
      Assert::IsTrue(detonation.fragments.fragmentOf.data() == expected.fragmentOf.data() &&
                       detonation.fragments.fragmentOf.size() == flying[index].recordCount,
                     (what + L": its part's fragments").c_str());
      Assert::AreEqual(expected.firstFragment, detonation.fragments.firstFragment, (what + L": its model's first fragment").c_str());
      Assert::AreEqual(expected.radius, detonation.fragments.radius, (what + L": its part's radius").c_str());
    }
  }
};

} // namespace NeuronClientTests
