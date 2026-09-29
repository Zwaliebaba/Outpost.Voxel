#include "pch.h"

#include "SeededRandom.h"

#include "Explosion.h"
#include "Float3.h"
#include "Sphere.h"
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

// Samples along each followed voxel's flight, from the detonation to twice its envelope's stop time.
constexpr std::uint32_t FLIGHT_SAMPLES = 160;

// How long after the detonation, in units of 1 / drag, a voxel has made all of its motion that a float can hold:
// e^-32 is far below the half of 2^-24 at which 1 - e^-32 rounds to 1.
constexpr float MOTION_DONE = 32.0f;

// Rounding allowed on a position, relative to its distance from the origin: a float holds a coordinate 1,000 voxels
// out to 6 × 10^-5, and the pose adds a few such roundings.
constexpr float POSITION_ROUNDING = 1.0e-6f;

[[nodiscard]] bool IsIdentity(const NeuronCore::VoxelPose& _pose) noexcept
{
  return _pose.axisX.x == 1.0f && _pose.axisX.y == 0.0f && _pose.axisX.z == 0.0f && _pose.axisY.x == 0.0f && _pose.axisY.y == 1.0f &&
         _pose.axisY.z == 0.0f && _pose.axisZ.x == 0.0f && _pose.axisZ.y == 0.0f && _pose.axisZ.z == 1.0f;
}

[[nodiscard]] bool SamePose(const NeuronCore::VoxelPose& _a, const NeuronCore::VoxelPose& _b) noexcept
{
  const auto same = [](Float3 _u, Float3 _v) { return _u.x == _v.x && _u.y == _v.y && _u.z == _v.z; };
  return same(_a.center, _b.center) && same(_a.axisX, _b.axisX) && same(_a.axisY, _b.axisY) && same(_a.axisZ, _b.axisZ);
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

// How far the farthest corner of the unit cube posed as _a is from the same corner posed as _b.
[[nodiscard]] float CornerDistance(const NeuronCore::VoxelPose& _a, const NeuronCore::VoxelPose& _b) noexcept
{
  float farthest = 0.0f;
  for (std::uint32_t corner = 0; corner < 8; ++corner)
  {
    const float x = (corner & 1u) != 0u ? 0.5f : -0.5f;
    const float y = (corner & 2u) != 0u ? 0.5f : -0.5f;
    const float z = (corner & 4u) != 0u ? 0.5f : -0.5f;
    const Float3 a = _a.center + _a.axisX * x + _a.axisY * y + _a.axisZ * z;
    const Float3 b = _b.center + _b.axisX * x + _b.axisY * y + _b.axisZ * z;
    farthest = std::max(farthest, NeuronCore::Length(a - b));
  }
  return farthest;
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

// A parameter block well beyond the defaults, around a blast origin in _lower-_upper, with an inherited velocity and a
// seed (Design/SpaceScene.md §7.7). Each number is drawn in its own statement or list element, so that every compiler
// draws them in the same order.
[[nodiscard]] NeuronCore::ExplosionParameters RandomParameters(SeededRandom& _random, Float3 _lower, Float3 _upper)
{
  NeuronCore::ExplosionParameters parameters{.blastOrigin = _random.InBox(_lower, _upper),
                                             .launchSpeed = _random.Uniform(0.0f, 400.0f),
                                             .falloffDistance = _random.Uniform(5.0f, 300.0f),
                                             .directionJitter = _random.Uniform(0.0f, 1.5f),
                                             .speedJitter = _random.Uniform(0.0f, 0.9f),
                                             .drag = _random.Uniform(0.2f, 5.0f),
                                             .maxQuarterTurns = _random.Below(9),
                                             .inheritedVelocity = {0.0f, 0.0f, 0.0f},
                                             .seed = 0u};
  const Float3 heading = _random.Direction();
  parameters.inheritedVelocity = heading * _random.Uniform(0.0f, 300.0f);
  parameters.seed = _random.Below(0xFFFFFFFFu);
  return parameters;
}

// A random voxel's center in integer cells of a 60-voxel box, and the box around them all.
struct RandomVoxels
{
  std::vector<Float3> centers;
  Float3 lower{1.0e9f, 1.0e9f, 1.0e9f};
  Float3 upper{-1.0e9f, -1.0e9f, -1.0e9f};
};

[[nodiscard]] RandomVoxels MakeRandomVoxels(SeededRandom& _random, std::uint32_t _count)
{
  RandomVoxels voxels;
  for (std::uint32_t voxel = 0; voxel < _count; ++voxel)
  {
    const Float3 center{std::floor(_random.Uniform(-30.0f, 30.0f)) + 0.5f, std::floor(_random.Uniform(-30.0f, 30.0f)) + 0.5f,
                        std::floor(_random.Uniform(-30.0f, 30.0f)) + 0.5f};
    voxels.centers.push_back(center);
    voxels.lower = {std::min(voxels.lower.x, center.x - 0.5f), std::min(voxels.lower.y, center.y - 0.5f),
                    std::min(voxels.lower.z, center.z - 0.5f)};
    voxels.upper = {std::max(voxels.upper.x, center.x + 0.5f), std::max(voxels.upper.y, center.y + 0.5f),
                    std::max(voxels.upper.z, center.z + 0.5f)};
  }
  return voxels;
}

} // namespace

// The detonation's motion (Design/SpaceScene.md §5.5, Design/ADR/ADR-013) on synthetic voxels and random parameter
// blocks; the station under the defaults is MilitaryStationTests'.
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

  // A voxel at the blast origin has no direction away from it and flies along its jitter, at the launch speed, which
  // falls off with no distance: it ends the launch speed over the drag away, in some direction.
  TEST_METHOD(VoxelAtTheOriginFliesAlongItsJitter)
  {
    const Float3 origin{3.5f, -2.5f, 7.5f};
    NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(origin);
    parameters.directionJitter = 0.0f;
    parameters.speedJitter = 0.0f;
    const float reach = parameters.launchSpeed / parameters.drag;
    for (std::uint32_t voxel = 0; voxel < 64; ++voxel)
    {
      const NeuronCore::VoxelPose end = NeuronCore::ExplosionPose(voxel, origin, parameters, MOTION_DONE / parameters.drag);
      const float travelled = NeuronCore::Length(end.center - origin);
      Assert::AreEqual(reach, travelled, 1.0e-4f * reach, std::format(L"voxel {} travels the launch speed over the drag", voxel).c_str());
    }
  }

  // Once its motion is done to the float's last bit, a voxel no longer moves, and it is square to the axes: each of its
  // spins has turned a whole number of quarter turns.
  TEST_METHOD(EndsStillAndSquare)
  {
    SeededRandom random(20260928u);
    for (std::uint32_t block = 0; block < 8; ++block)
    {
      const NeuronCore::ExplosionParameters parameters = RandomParameters(random, {-8.0f, -8.0f, -8.0f}, {8.0f, 8.0f, 8.0f});
      const float done = MOTION_DONE / parameters.drag;
      for (std::uint32_t voxel = 0; voxel < 64; ++voxel)
      {
        const std::uint32_t column = voxel % 4u;
        const std::uint32_t row = voxel / 4u % 4u;
        const std::uint32_t layer = voxel / 16u;
        const Float3 restCenter{static_cast<float>(column) - 1.5f, static_cast<float>(row) - 1.5f, static_cast<float>(layer) - 1.5f};
        const NeuronCore::VoxelPose end = NeuronCore::ExplosionPose(voxel, restCenter, parameters, done);
        const std::wstring what = std::format(L"block {}, voxel {}", block, voxel);
        Assert::IsTrue(IsCubeRotation(end), what.c_str());
        for (const float later : {2.0f * done, 10.0f * done})
        {
          Assert::IsTrue(SamePose(end, NeuronCore::ExplosionPose(voxel, restCenter, parameters, later)), what.c_str());
        }
      }
    }
  }

  TEST_METHOD(NoQuarterTurnsMeansNoRotation)
  {
    NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters({0.0f, 5.0f, 0.0f});
    parameters.maxQuarterTurns = 0;
    const float stop = NeuronCore::BoundExplosion(parameters, {-16.0f, 0.0f, 0.0f}, {16.0f, 4.0f, 1.0f}).stopSeconds;
    for (std::uint32_t voxel = 0; voxel < 32; ++voxel)
    {
      const Float3 restCenter{static_cast<float>(voxel) - 15.5f, 3.5f, 0.5f};
      for (const float fraction : {0.01f, 0.1f, 0.5f, 1.0f, 2.0f})
      {
        Assert::IsTrue(IsIdentity(NeuronCore::ExplosionPose(voxel, restCenter, parameters, stop * fraction)));
      }
    }
  }

  // Design/SpaceScene.md §7.7: a detonation's seed changes every voxel's randomness, and the same seed repeats it (D3).
  TEST_METHOD(SeedsGiveDifferentDebris)
  {
    const NeuronCore::ExplosionParameters first = NeuronCore::DefaultExplosionParameters({0.0f, 0.0f, 0.0f});
    NeuronCore::ExplosionParameters second = first;
    second.seed = 1u;
    std::uint32_t moved = 0;
    for (std::uint32_t voxel = 0; voxel < 64u; ++voxel)
    {
      const std::uint32_t column = voxel % 4u;
      const std::uint32_t row = voxel / 4u % 4u;
      const std::uint32_t layer = voxel / 16u;
      const Float3 restCenter{static_cast<float>(column) - 1.5f, static_cast<float>(row) - 1.5f, static_cast<float>(layer) - 1.5f};
      const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(voxel, restCenter, second, 1.0f);
      Assert::IsTrue(SamePose(pose, NeuronCore::ExplosionPose(voxel, restCenter, second, 1.0f)), L"the same seed repeats itself");
      moved += SamePose(pose, NeuronCore::ExplosionPose(voxel, restCenter, first, 1.0f)) ? 0u : 1u;
    }
    Assert::AreEqual(64u, moved, L"every voxel flies differently under another seed");
  }

  // §7.7: the inherited velocity joins every launch under the same drag, so it carries every voxel alike, by the
  // velocity times the motion made over the drag, and leaves every spin as it was.
  TEST_METHOD(InheritedVelocityCarriesEveryVoxel)
  {
    SeededRandom random(20261015u);
    for (std::uint32_t block = 0; block < 16u; ++block)
    {
      const RandomVoxels voxels = MakeRandomVoxels(random, 32);
      const NeuronCore::ExplosionParameters carried = RandomParameters(random, voxels.lower, voxels.upper);
      NeuronCore::ExplosionParameters still = carried;
      still.inheritedVelocity = {0.0f, 0.0f, 0.0f};
      for (const float time : {0.0f, 0.3f, 1.0f, 4.0f, MOTION_DONE / carried.drag})
      {
        const float made = time > 0.0f ? 1.0f - std::exp(-carried.drag * time) : 0.0f;
        const Float3 expected = carried.inheritedVelocity * (made / carried.drag);
        for (std::uint32_t voxel = 0; voxel < voxels.centers.size(); ++voxel)
        {
          const NeuronCore::VoxelPose moving = NeuronCore::ExplosionPose(voxel, voxels.centers[voxel], carried, time);
          const NeuronCore::VoxelPose resting = NeuronCore::ExplosionPose(voxel, voxels.centers[voxel], still, time);
          const std::wstring what = std::format(L"block {}, voxel {}, {} s", block, voxel, time);
          const float allowed = POSITION_ROUNDING * 4.0f * (NeuronCore::Length(moving.center) + NeuronCore::Length(resting.center) + 1.0f);
          Assert::IsTrue(NeuronCore::Length(moving.center - resting.center - expected) <= allowed, what.c_str());
          Assert::IsTrue(SamePose({moving.center, resting.axisX, resting.axisY, resting.axisZ}, moving), what.c_str());
        }
      }
    }
  }

  // The closed-form envelope of §5.5 against the motion itself, for parameter blocks well beyond the defaults, with one
  // voxel at the blast origin in each: every voxel exactly intact at time 0, every sampled box inside the sphere at its
  // time and inside the sphere around the whole drift, and, from the stop time on, every corner within
  // EXPLOSION_STOP_DISTANCE of where it ends.
  TEST_METHOD(RandomExplosionsStayInsideTheirEnvelope)
  {
    SeededRandom random(20260927u);
    std::uint32_t followed = 0;
    for (std::uint32_t block = 0; block < 24; ++block)
    {
      RandomVoxels voxels = MakeRandomVoxels(random, 48);
      const NeuronCore::ExplosionParameters parameters = RandomParameters(random, voxels.lower, voxels.upper);
      voxels.centers.back() = parameters.blastOrigin;
      const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, voxels.lower, voxels.upper);
      Assert::IsTrue(envelope.stopSeconds >= 0.0f, std::format(L"block {} stops", block).c_str());
      const NeuronCore::Sphere whole = NeuronCore::EnvelopeSphere(envelope);
      const float slack =
        POSITION_ROUNDING * (envelope.radius + NeuronCore::Length(envelope.drift) + NeuronCore::Length(envelope.center)) + 1.0e-3f;
      const float done = MOTION_DONE / parameters.drag;
      for (std::uint32_t voxel = 0; voxel < voxels.centers.size(); ++voxel)
      {
        const Float3 restCenter = voxels.centers[voxel];
        const NeuronCore::VoxelPose intact = NeuronCore::ExplosionPose(voxel, restCenter, parameters, 0.0f);
        Assert::IsTrue(IsIdentity(intact) && intact.center.x == restCenter.x && intact.center.y == restCenter.y &&
                         intact.center.z == restCenter.z,
                       std::format(L"block {}, voxel {} is intact at time 0", block, voxel).c_str());
        const NeuronCore::VoxelPose end = NeuronCore::ExplosionPose(voxel, restCenter, parameters, done);
        for (std::uint32_t sample = 0; sample <= FLIGHT_SAMPLES; ++sample)
        {
          const float time = 2.0f * envelope.stopSeconds * static_cast<float>(sample) / static_cast<float>(FLIGHT_SAMPLES);
          const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(voxel, restCenter, parameters, time);
          const std::wstring where = std::format(L"block {}, voxel {}, {} s", block, voxel, time);
          const NeuronCore::Sphere now = NeuronCore::EnvelopeSphereAt(envelope, parameters, time);
          const float reached = NeuronCore::Length(pose.center - now.center) + NeuronCore::VOXEL_BOUNDING_RADIUS;
          Assert::IsTrue(reached <= now.radius + slack, where.c_str());
          const float reachedEver = NeuronCore::Length(pose.center - whole.center) + NeuronCore::VOXEL_BOUNDING_RADIUS;
          Assert::IsTrue(reachedEver <= whole.radius + slack, where.c_str());
          if (time >= envelope.stopSeconds)
          {
            const float left = CornerDistance(pose, end);
            Assert::IsTrue(left <= NeuronCore::EXPLOSION_STOP_DISTANCE + POSITION_ROUNDING * NeuronCore::Length(end.center),
                           std::format(L"{}: {} left to go after the stop", where, left).c_str());
          }
        }
        ++followed;
      }
    }
    Logger::WriteMessage(std::format(L"{} voxels followed under 24 random parameter blocks\n", followed).c_str());
  }

  // A center moves no faster than its launch, which is at most the launch speed at the origin with all the variation,
  // plus the inherited velocity: the drag only slows it.
  TEST_METHOD(RandomExplosionsMoveNoFasterThanTheirLaunch)
  {
    constexpr float STEP_SECONDS = 1.0e-3f;
    SeededRandom random(20260929u);
    for (std::uint32_t block = 0; block < 16; ++block)
    {
      const RandomVoxels voxels = MakeRandomVoxels(random, 32);
      const NeuronCore::ExplosionParameters parameters = RandomParameters(random, voxels.lower, voxels.upper);
      const float fastest = parameters.launchSpeed * (1.0f + parameters.speedJitter) + NeuronCore::Length(parameters.inheritedVelocity);
      const float stop = NeuronCore::BoundExplosion(parameters, voxels.lower, voxels.upper).stopSeconds;
      for (std::uint32_t voxel = 0; voxel < voxels.centers.size(); ++voxel)
      {
        for (std::uint32_t sample = 0; sample < 32; ++sample)
        {
          const float time = stop * static_cast<float>(sample) / 32.0f;
          const Float3 before = NeuronCore::ExplosionPose(voxel, voxels.centers[voxel], parameters, time).center;
          const Float3 after = NeuronCore::ExplosionPose(voxel, voxels.centers[voxel], parameters, time + STEP_SECONDS).center;
          const float moved = NeuronCore::Length(after - before);
          Assert::IsTrue(moved <= fastest * STEP_SECONDS * 1.001f + POSITION_ROUNDING * (NeuronCore::Length(after) + 1.0f) * 4.0f,
                         std::format(L"block {}, voxel {} moves {} in {} s at {} s", block, voxel, moved, STEP_SECONDS, time).c_str());
        }
      }
    }
  }
};

} // namespace NeuronCoreTests
