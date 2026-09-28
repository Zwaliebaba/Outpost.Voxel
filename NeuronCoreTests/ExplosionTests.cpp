#include "pch.h"

#include "SeededRandom.h"

#include "Explosion.h"
#include "Float3.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;

// How far below the ground a corner may seem to be, to rounding.
constexpr float GROUND_TOLERANCE = 1.0e-5f;

// Samples along each followed voxel's flights.
constexpr std::uint32_t FLIGHT_SAMPLES = 160;

// The lowest point of a posed voxel: its center less the rotated unit cube's half-extent along +Z.
[[nodiscard]] float LowestPoint(const NeuronCore::VoxelPose& _pose) noexcept
{
  return _pose.center.z - 0.5f * (std::abs(_pose.axisX.z) + std::abs(_pose.axisY.z) + std::abs(_pose.axisZ.z));
}

[[nodiscard]] bool IsIdentity(const NeuronCore::VoxelPose& _pose) noexcept
{
  return _pose.axisX.x == 1.0f && _pose.axisX.y == 0.0f && _pose.axisX.z == 0.0f && _pose.axisY.x == 0.0f && _pose.axisY.y == 1.0f &&
         _pose.axisY.z == 0.0f && _pose.axisZ.x == 0.0f && _pose.axisZ.y == 0.0f && _pose.axisZ.z == 1.0f;
}

// A rotation of the cube onto itself: every entry exactly 0 or ±1, and a rotation rather than a reflection.
[[nodiscard]] bool IsCubeRotation(const NeuronCore::VoxelPose& _pose) noexcept
{
  for (const Float3 axis : {_pose.axisX, _pose.axisY, _pose.axisZ})
  {
    for (const float entry : {axis.x, axis.y, axis.z})
    {
      if (entry != 0.0f && entry != 1.0f && entry != -1.0f)
      {
        return false;
      }
    }
  }
  const Float3 handed = NeuronCore::Cross(_pose.axisX, _pose.axisY);
  return handed.x == _pose.axisZ.x && handed.y == _pose.axisZ.y && handed.z == _pose.axisZ.z;
}

// A model of the given voxels, all in one instance at the origin.
[[nodiscard]] NeuronCore::VoxModel ModelOf(const std::vector<NeuronCore::VoxelRecord>& _voxels)
{
  NeuronCore::VoxModel model{};
  model.version = 150;
  for (const NeuronCore::VoxelRecord& voxel : _voxels)
  {
    model.records.push_back(NeuronCore::PackVoxelRecord(voxel));
  }
  model.instances.push_back({{0, 0, 0}, {256, 256, 256}, 0, static_cast<std::uint32_t>(model.records.size())});
  return model;
}

} // namespace

// The explosion's motion model (Design/SampleRenderer.md §12, §14) on synthetic voxels and random parameter blocks; the
// station under the defaults is MilitaryStationTests'.
TEST_CLASS(ExplosionTests)
{
public:
  TEST_METHOD(CentroidIsTheMeanCenter)
  {
    const NeuronCore::VoxModel model = ModelOf({{0, 0, 0, 0}, {2, 0, 0, 0}, {1, 3, 6, 0}});
    const Float3 centroid = NeuronCore::VoxelCentroid(model);
    Assert::AreEqual(1.5f, centroid.x);
    Assert::AreEqual(1.5f, centroid.y);
    Assert::AreEqual(2.5f, centroid.z);

    const Float3 none = NeuronCore::VoxelCentroid(ModelOf({}));
    Assert::IsTrue(none.x == 0.0f && none.y == 0.0f && none.z == 0.0f, L"an empty model's blast comes from the origin");
  }

  // A voxel on the ground may not turn while it stands on it: it is lifted, and its spin starts only once its center has
  // risen a bounding radius above the ground (Design/ADR/ADR-009). This one is launched down, from right under the blast.
  TEST_METHOD(GroundLayerRisesBeforeItTurns)
  {
    const Float3 restCenter{0.5f, 0.5f, 0.5f};
    NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters({0.5f, 0.5f, 10.5f});
    parameters.maxQuarterTurns = 8;
    for (std::uint32_t voxel = 0; voxel < 64; ++voxel)
    {
      const float rest = NeuronCore::ExplosionRestTime(voxel, restCenter, parameters);
      float highestUnturned = 0.0f;
      bool turned = false;
      for (std::uint32_t sample = 0; sample <= 4 * FLIGHT_SAMPLES; ++sample)
      {
        const float time = rest * static_cast<float>(sample) / static_cast<float>(4 * FLIGHT_SAMPLES);
        const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(voxel, restCenter, parameters, time);
        Assert::IsTrue(LowestPoint(pose) >= -GROUND_TOLERANCE, std::format(L"voxel {} at {} s", voxel, time).c_str());
        if (!turned && IsIdentity(pose))
        {
          highestUnturned = pose.center.z;
        }
        turned = turned || !IsIdentity(pose);
      }
      Assert::IsTrue(turned, std::format(L"voxel {} spins", voxel).c_str());
      Assert::IsTrue(highestUnturned <= NeuronCore::VOXEL_BOUNDING_RADIUS + 1.0e-3f,
                     std::format(L"voxel {} starts turning as it passes the bounding radius", voxel).c_str());
    }
  }

  // §12: the spin is done by the time the voxel falls through the bounding radius for the last time, so it lands square.
  TEST_METHOD(LandsAlreadySquare)
  {
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters({0.0f, 0.0f, 20.0f});
    for (std::uint32_t voxel = 0; voxel < 256; ++voxel)
    {
      const std::uint32_t column = voxel % 16u;
      const std::uint32_t row = voxel / 16u;
      const Float3 restCenter{static_cast<float>(column) - 7.5f, static_cast<float>(row) - 7.5f, 0.5f + static_cast<float>(voxel % 5u)};
      const float rest = NeuronCore::ExplosionRestTime(voxel, restCenter, parameters);
      for (std::uint32_t step = 0; step <= 16; ++step)
      {
        const float time = rest * (1.0f - 0.0005f * static_cast<float>(step));
        const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(voxel, restCenter, parameters, time);
        if (pose.center.z < NeuronCore::VOXEL_BOUNDING_RADIUS)
        {
          Assert::IsTrue(IsCubeRotation(pose), std::format(L"voxel {} below the bounding radius at {} s", voxel, time).c_str());
        }
      }
    }
  }

  TEST_METHOD(NoQuarterTurnsMeansNoRotation)
  {
    NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters({0.0f, 0.0f, 5.0f});
    parameters.maxQuarterTurns = 0;
    for (std::uint32_t voxel = 0; voxel < 32; ++voxel)
    {
      const Float3 restCenter{static_cast<float>(voxel) - 15.5f, 0.5f, 3.5f};
      const float rest = NeuronCore::ExplosionRestTime(voxel, restCenter, parameters);
      for (const float fraction : {0.1f, 0.5f, 0.9f, 1.0f, 2.0f})
      {
        Assert::IsTrue(IsIdentity(NeuronCore::ExplosionPose(voxel, restCenter, parameters, rest * fraction)));
      }
    }
  }

  // The closed-form envelope of §12 against the trajectories themselves, for parameter blocks well beyond the defaults:
  // every sampled box inside it, every voxel at rest by its time, and no corner ever below the ground.
  TEST_METHOD(RandomExplosionsStayInsideTheirEnvelope)
  {
    SeededRandom random(20260927u);
    std::uint32_t followed = 0;
    for (std::uint32_t block = 0; block < 24; ++block)
    {
      std::vector<Float3> centers;
      Float3 lower{1.0e9f, 1.0e9f, 1.0e9f};
      Float3 upper{-1.0e9f, -1.0e9f, -1.0e9f};
      for (std::uint32_t voxel = 0; voxel < 48; ++voxel)
      {
        // Integer cells, a quarter of them on the ground.
        const float z = voxel % 4u == 0u ? 0.0f : std::floor(random.Uniform(0.0f, 40.0f));
        const Float3 center{std::floor(random.Uniform(-30.0f, 30.0f)) + 0.5f, std::floor(random.Uniform(-30.0f, 30.0f)) + 0.5f, z + 0.5f};
        centers.push_back(center);
        lower = {std::min(lower.x, center.x - 0.5f), std::min(lower.y, center.y - 0.5f), std::min(lower.z, center.z - 0.5f)};
        upper = {std::max(upper.x, center.x + 0.5f), std::max(upper.y, center.y + 0.5f), std::max(upper.z, center.z + 0.5f)};
      }
      const NeuronCore::ExplosionParameters parameters{.blastOrigin = random.InBox(lower, upper),
                                                       .gravity = random.Uniform(5.0f, 60.0f),
                                                       .launchSpeed = random.Uniform(0.0f, 80.0f),
                                                       .falloffDistance = random.Uniform(5.0f, 300.0f),
                                                       .upwardBias = random.Uniform(-1.0f, 2.0f),
                                                       .directionJitter = random.Uniform(0.0f, 1.5f),
                                                       .speedJitter = random.Uniform(0.0f, 0.9f),
                                                       .restitution = random.Uniform(0.0f, 0.95f),
                                                       .horizontalDamping = random.Uniform(0.0f, 1.0f),
                                                       .maxQuarterTurns = random.Below(9)};
      const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, lower, upper);
      for (std::uint32_t voxel = 0; voxel < centers.size(); ++voxel)
      {
        const float rest = NeuronCore::ExplosionRestTime(voxel, centers[voxel], parameters);
        Assert::IsTrue(rest <= envelope.restTimeSeconds * 1.0001f,
                       std::format(L"block {}, voxel {} rests by the bound", block, voxel).c_str());
        for (std::uint32_t sample = 0; sample <= FLIGHT_SAMPLES; ++sample)
        {
          const float time = rest * static_cast<float>(sample) / static_cast<float>(FLIGHT_SAMPLES);
          const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, time);
          const std::wstring where = std::format(L"block {}, voxel {}, {} s", block, voxel, time);
          const float slack = 1.0e-3f + 1.0e-5f * Length(pose.center);
          const float radius = NeuronCore::VOXEL_BOUNDING_RADIUS;
          Assert::IsTrue(pose.center.x - radius >= envelope.lower.x - slack && pose.center.x + radius <= envelope.upper.x + slack,
                         where.c_str());
          Assert::IsTrue(pose.center.y - radius >= envelope.lower.y - slack && pose.center.y + radius <= envelope.upper.y + slack,
                         where.c_str());
          Assert::IsTrue(pose.center.z + radius <= envelope.upper.z + slack, where.c_str());
          Assert::IsTrue(LowestPoint(pose) >= std::min(envelope.lower.z, 0.0f) - GROUND_TOLERANCE, where.c_str());
        }
        ++followed;
      }
    }
    Logger::WriteMessage(std::format(L"{} voxels followed under 24 random parameter blocks\n", followed).c_str());
  }
};

} // namespace NeuronCoreTests
