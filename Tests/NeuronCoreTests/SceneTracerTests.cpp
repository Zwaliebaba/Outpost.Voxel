#include "pch.h"

#include "CubeSymmetry.h"
#include "SeededRandom.h"

#include "Box.h"
#include "Explosion.h"
#include "Float3.h"
#include "Placement.h"
#include "Ray.h"
#include "RigidTransform.h"
#include "SceneTracer.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <set>
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

// How far apart the whole placements of a random scene stand, so that no two overlap, as the world keeps them (§7.3).
constexpr float PLACEMENT_SPACING = 80.0f;

// A model of one or two parts of distinct random voxels, up to 24 cells across.
[[nodiscard]] NeuronCore::VoxModel RandomModel(SeededRandom& _random)
{
  NeuronCore::VoxModel model{};
  model.version = 150;
  const std::uint32_t parts = 1u + _random.Below(2u);
  for (std::uint32_t part = 0; part < parts; ++part)
  {
    const auto firstRecord = static_cast<std::uint32_t>(model.records.size());
    const std::uint32_t cells = 4u + _random.Below(21u);
    const std::uint32_t voxels = 20u + _random.Below(180u);
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> taken;
    for (std::uint32_t voxel = 0; voxel < voxels; ++voxel)
    {
      const std::uint32_t x = _random.Below(cells);
      const std::uint32_t y = _random.Below(cells);
      const std::uint32_t z = _random.Below(cells);
      if (taken.emplace(x, y, z).second)
      {
        model.records.push_back(
          NeuronCore::PackVoxelRecord({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(z), 0u}));
      }
    }
    const auto size = static_cast<std::int32_t>(cells);
    model.instances.push_back({{0, 0, 0}, {size, size, size}, firstRecord, static_cast<std::uint32_t>(model.records.size()) - firstRecord});
  }
  return model;
}

// A scene of the three kinds of placement: aligned, turned by a cube symmetry with a translation that need not be whole;
// rigid, turned any way; and detonated, at some time since. The whole ones stand on a lattice PLACEMENT_SPACING apart.
[[nodiscard]] std::vector<Placement> RandomPlacements(SeededRandom& _random, const std::vector<NeuronCore::VoxModel>& _models)
{
  const std::vector<std::uint32_t> firstRecords = NeuronCore::ModelFirstRecords(_models);
  const std::array<CubeSymmetry, 48> symmetries = CubeSymmetries();
  std::vector<Placement> placements;
  const std::uint32_t count = 3u + _random.Below(6u);
  for (std::uint32_t i = 0; i < count; ++i)
  {
    const std::uint32_t model = _random.Below(static_cast<std::uint32_t>(_models.size()));
    const std::uint32_t part = _random.Below(static_cast<std::uint32_t>(_models[model].instances.size()));
    const std::uint32_t kind = _random.Below(3u);
    NeuronCore::RigidTransform transform{};
    if (kind == 0u)
    {
      // Two neighbors in the list differ in one axis's sign, so one of the two is a rotation.
      const std::uint32_t symmetry = 2u * _random.Below(24u);
      transform.rotation = symmetries[symmetry].proper ? symmetries[symmetry].rotation : symmetries[symmetry + 1u].rotation;
    }
    else
    {
      _random.Rotation(transform.rotation.axisX, transform.rotation.axisY, transform.rotation.axisZ);
    }
    const std::uint32_t column = i % 3u;
    const std::uint32_t row = i / 3u % 3u;
    const std::uint32_t layer = i / 9u;
    const Float3 slot{static_cast<float>(column) * PLACEMENT_SPACING, static_cast<float>(row) * PLACEMENT_SPACING,
                      static_cast<float>(layer) * PLACEMENT_SPACING};
    transform.translation = slot + _random.InBox({-4.0f, -4.0f, -4.0f}, {4.0f, 4.0f, 4.0f});
    Placement placement = NeuronCore::PlacePart(_models[model], model, firstRecords[model], part, transform);
    if (kind == 2u)
    {
      NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(_random.InBox(placement.lower, placement.upper));
      parameters.launchSpeed = _random.Uniform(5.0f, 30.0f);
      const Float3 heading = _random.Direction();
      parameters.inheritedVelocity = heading * _random.Uniform(0.0f, 10.0f);
      parameters.seed = _random.Below(100u);
      placement.detonation = NeuronCore::PlacementDetonation{parameters, _random.Uniform(0.0f, 3.0f)};
    }
    placements.push_back(placement);
  }
  return placements;
}

// Brute force over every box every placement draws, through the permutation the GPU draws it with: the nearest hit at or
// beyond _minDistance, and the lower id where two are as near.
[[nodiscard]] NeuronCore::TraceHit BruteForce(const std::vector<std::uint32_t>& _records, const std::vector<Placement>& _placements,
                                              const NeuronCore::Ray& _ray, float _minDistance) noexcept
{
  NeuronCore::TraceHit best{NeuronCore::NO_VOXEL, std::numeric_limits<float>::infinity(), {0.0f, 0.0f, 0.0f}};
  const Float3 invDirection = NeuronCore::InverseDirection(_ray);
  for (const Placement& placement : _placements)
  {
    const bool aligned = NeuronCore::IsAlignedPlacement(placement);
    for (std::uint32_t i = 0; i < placement.recordCount; ++i)
    {
      const NeuronCore::Box box = NeuronCore::PlacedVoxelBox(placement, i, _records[placement.firstRecord + i]);
      float distance = 0.0f;
      Float3 normal{};
      const bool hit = aligned ? NeuronCore::IntersectBox<false, false>(box, _ray.origin, _ray.direction, invDirection, distance, normal)
                               : NeuronCore::IntersectBox<true, false>(box, _ray.origin, _ray.direction, invDirection, distance, normal);
      const std::uint32_t voxel = placement.firstVoxel + i;
      if (hit && distance >= _minDistance && (distance < best.distance || (distance == best.distance && voxel < best.voxel)))
      {
        best = {voxel, distance, normal};
      }
    }
  }
  return best;
}

void ExpectSameHit(const NeuronCore::TraceHit& _expected, const NeuronCore::TraceHit& _actual, const std::wstring& _what)
{
  Assert::AreEqual(_expected.voxel, _actual.voxel, _what.c_str());
  if (_expected.voxel != NeuronCore::NO_VOXEL)
  {
    Assert::AreEqual(_expected.distance, _actual.distance, _what.c_str());
    Assert::AreEqual(_expected.normal.x, _actual.normal.x, _what.c_str());
    Assert::AreEqual(_expected.normal.y, _actual.normal.y, _what.c_str());
    Assert::AreEqual(_expected.normal.z, _actual.normal.z, _what.c_str());
  }
}

} // namespace

// Design/SpaceScene.md §15: the scene tracer against brute force over every placed box.
TEST_CLASS(SceneTracerTests)
{
public:
  // Seeded random scenes of aligned, rigid and detonated placements, traced from random eyes: at random points of the
  // scene, and at the corners of random voxels, where a ray grazes edges and the walk's margin decides what it tests.
  TEST_METHOD(MatchesBruteForceOnRandomScenes)
  {
    SeededRandom random(20261014u);
    std::uint32_t rays = 0;
    std::uint32_t hits = 0;
    for (std::uint32_t scene = 0; scene < 12u; ++scene)
    {
      const std::vector<NeuronCore::VoxModel> models{RandomModel(random), RandomModel(random), RandomModel(random)};
      std::vector<Placement> placements = RandomPlacements(random, models);
      Assert::IsTrue(NeuronCore::AssignVoxelIds(placements));
      const std::vector<std::uint32_t> records = NeuronCore::SceneRecords(models);
      const NeuronCore::SceneTracer tracer(models, placements);
      for (std::uint32_t sample = 0; sample < 500u; ++sample)
      {
        const Placement& placement = placements[random.Below(static_cast<std::uint32_t>(placements.size()))];
        Float3 target{};
        if (sample % 2u == 0u && placement.recordCount > 0u)
        {
          const std::uint32_t voxel = random.Below(placement.recordCount);
          const NeuronCore::Box box = NeuronCore::PlacedVoxelBox(placement, voxel, records[placement.firstRecord + voxel]);
          const std::uint32_t corner = random.Below(8u);
          const float x = (corner & 1u) != 0u ? 0.5f : -0.5f;
          const float y = (corner & 2u) != 0u ? 0.5f : -0.5f;
          const float z = (corner & 4u) != 0u ? 0.5f : -0.5f;
          target = box.center + box.axisX * x + box.axisY * y + box.axisZ * z;
        }
        else
        {
          const Float3 offset = random.Direction();
          target = NeuronCore::PlacementSphere(placement).center + offset * random.Uniform(0.0f, 20.0f);
        }
        const Float3 away = random.Direction();
        const Float3 eye = target + away * random.Uniform(30.0f, 400.0f);
        const NeuronCore::Ray ray{eye, target - eye};
        const float minDistance = sample % 5u == 0u ? random.Uniform(0.0f, 0.5f) : 0.0f;
        const NeuronCore::TraceHit expected = BruteForce(records, placements, ray, minDistance);
        ExpectSameHit(expected, tracer.Trace(ray, minDistance), std::format(L"scene {}, ray {}", scene, sample));
        hits += expected.voxel != NeuronCore::NO_VOXEL ? 1u : 0u;
        ++rays;
      }
    }
    Logger::WriteMessage(std::format(L"{} of {} rays hit a placement\n", hits, rays).c_str());
    Assert::IsTrue(2u * hits > rays, L"most rays hit, or the comparison says little");
  }
};

} // namespace NeuronCoreTests
