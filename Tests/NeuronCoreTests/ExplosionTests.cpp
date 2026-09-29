#include "pch.h"

#include "SeededRandom.h"

#include "Explosion.h"
#include "Float3.h"
#include "Fragmentation.h"
#include "Sphere.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <set>
#include <tuple>
#include <utility>
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

[[nodiscard]] bool Same(Float3 _a, Float3 _b) noexcept
{
  return _a.x == _b.x && _a.y == _b.y && _a.z == _b.z;
}

[[nodiscard]] bool SamePose(const NeuronCore::VoxelPose& _a, const NeuronCore::VoxelPose& _b) noexcept
{
  const auto same = [](Float3 _u, Float3 _v) { return _u.x == _v.x && _u.y == _v.y && _u.z == _v.z; };
  return same(_a.center, _b.center) && same(_a.axisX, _b.axisX) && same(_a.axisY, _b.axisY) && same(_a.axisZ, _b.axisZ);
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
// seed (Design/Archive/SpaceScene.md §7.7). Each number is drawn in its own statement or list element, so that every compiler
// draws them in the same order.
[[nodiscard]] NeuronCore::ExplosionParameters RandomParameters(SeededRandom& _random, Float3 _lower, Float3 _upper)
{
  NeuronCore::ExplosionParameters parameters{.blastOrigin = _random.InBox(_lower, _upper),
                                             .launchSpeed = _random.Uniform(0.0f, 400.0f),
                                             .falloffDistance = _random.Uniform(5.0f, 300.0f),
                                             .directionJitter = _random.Uniform(0.0f, 1.5f),
                                             .speedSpread = _random.Uniform(0.0f, 0.6f),
                                             .drag = _random.Uniform(0.2f, 5.0f),
                                             .minDrag = 0.0f,
                                             .maxSpinRadians = _random.Uniform(0.0f, 20.0f),
                                             .shockSpeed = _random.Uniform(20.0f, 2000.0f),
                                             .inheritedVelocity = {0.0f, 0.0f, 0.0f},
                                             .seed = 0u};
  parameters.minDrag = parameters.drag * _random.Uniform(0.05f, 1.0f);
  const Float3 heading = _random.Direction();
  parameters.inheritedVelocity = heading * _random.Uniform(0.0f, 300.0f);
  parameters.seed = _random.Below(0xFFFFFFFFu);
  return parameters;
}

// Random voxels in a 16-voxel cube, dense enough that many fragments hold several voxels, broken under random
// fragmentation parameters: each voxel's center, its fragment, the fragments, the box around the voxels, and the farthest
// any center lies from its fragment's pivot.
struct BrokenVoxels
{
  std::vector<Float3> centers;
  std::vector<std::uint32_t> fragmentOf;
  std::vector<NeuronCore::Fragment> fragments;
  Float3 lower{1.0e9f, 1.0e9f, 1.0e9f};
  Float3 upper{-1.0e9f, -1.0e9f, -1.0e9f};
  float radius = 0.0f;
};

[[nodiscard]] BrokenVoxels MakeBrokenVoxels(SeededRandom& _random, std::uint32_t _draws)
{
  std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> cells;
  std::vector<NeuronCore::VoxelRecord> voxels;
  for (std::uint32_t draw = 0; draw < _draws; ++draw)
  {
    const std::uint32_t x = _random.Below(16u);
    const std::uint32_t y = _random.Below(16u);
    const std::uint32_t z = _random.Below(16u);
    if (cells.emplace(x, y, z).second)
    {
      voxels.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(z), 0u});
    }
  }
  const NeuronCore::VoxModel model = ModelOf(voxels);
  const NeuronCore::FragmentationParameters fragmentation{.blastOrigin = _random.InBox({0.0f, 0.0f, 0.0f}, {16.0f, 16.0f, 16.0f}),
                                                          .shatterDistance = _random.Uniform(1.0f, 20.0f),
                                                          .growthSteps = _random.Below(8u)};
  NeuronCore::ModelFragments broken = NeuronCore::FragmentModel(model, fragmentation);
  BrokenVoxels result;
  result.fragmentOf = std::move(broken.fragmentOf);
  result.fragments = std::move(broken.fragments);
  result.radius = broken.partRadius.front();
  for (const NeuronCore::VoxelRecord& voxel : voxels)
  {
    const Float3 center{static_cast<float>(voxel.x) + 0.5f, static_cast<float>(voxel.y) + 0.5f, static_cast<float>(voxel.z) + 0.5f};
    result.centers.push_back(center);
    result.lower = {std::min(result.lower.x, center.x - 0.5f), std::min(result.lower.y, center.y - 0.5f),
                    std::min(result.lower.z, center.z - 0.5f)};
    result.upper = {std::max(result.upper.x, center.x + 0.5f), std::max(result.upper.y, center.y + 0.5f),
                    std::max(result.upper.z, center.z + 0.5f)};
  }
  return result;
}

// Voxel _voxel of _broken, posed.
[[nodiscard]] NeuronCore::VoxelPose PoseOf(const BrokenVoxels& _broken, std::uint32_t _voxel,
                                           const NeuronCore::ExplosionParameters& _parameters, float _timeSeconds) noexcept
{
  const std::uint32_t fragment = _broken.fragmentOf[_voxel];
  return NeuronCore::ExplosionPose(fragment, _broken.fragments[fragment], _broken.centers[_voxel], _parameters, _timeSeconds);
}

// A voxel in the 4 × 4 × 4 block about the origin, by index.
[[nodiscard]] Float3 BlockCenter(std::uint32_t _voxel) noexcept
{
  const std::uint32_t column = _voxel % 4u;
  const std::uint32_t row = _voxel / 4u % 4u;
  const std::uint32_t layer = _voxel / 16u;
  return {static_cast<float>(column) - 1.5f, static_cast<float>(row) - 1.5f, static_cast<float>(layer) - 1.5f};
}

// How long after the detonation every fragment of a block of _parameters has made all of its motion a float can hold:
// the blast has reached every pivot by the envelope's stop time, and each fragment's drag is at least the least.
[[nodiscard]] float MotionDoneSeconds(const NeuronCore::ExplosionParameters& _parameters, float _stopSeconds) noexcept
{
  return _stopSeconds + MOTION_DONE / _parameters.minDrag;
}

} // namespace

// The detonation's motion (Design/Archive/SpaceScene.md §5.5, Design/ADR/ADR-024) on synthetic fragments and random parameter
// blocks; the station under the defaults is MilitaryStationTests', and how models break is FragmentationTests'.
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

  // A lone voxel at the blast origin has no direction away from it and flies along its jitter, at the launch speed, which
  // falls off with no distance, and starts at once: it ends the launch speed over the drag away, in some direction.
  TEST_METHOD(VoxelAtTheOriginFliesAlongItsJitter)
  {
    const Float3 origin{3.5f, 2.5f, 7.5f};
    NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(origin);
    parameters.directionJitter = 0.0f;
    parameters.speedSpread = 0.0f;
    const float reach = parameters.launchSpeed / parameters.drag;
    for (std::uint32_t fragment = 0; fragment < 64; ++fragment)
    {
      const NeuronCore::VoxelPose end =
        NeuronCore::ExplosionPose(fragment, {origin, 1.0f}, origin, parameters, MOTION_DONE / parameters.drag);
      const float travelled = NeuronCore::Length(end.center - origin);
      Assert::AreEqual(reach, travelled, 1.0e-4f * reach,
                       std::format(L"fragment {} travels the launch speed over the drag", fragment).c_str());
    }
  }

  // Once its fragment's motion is done to the float's last bit, a voxel no longer moves (ADR-024: it ends turned any way).
  TEST_METHOD(EndsStill)
  {
    SeededRandom random(20260928u);
    for (std::uint32_t block = 0; block < 8; ++block)
    {
      const BrokenVoxels broken = MakeBrokenVoxels(random, 600);
      const NeuronCore::ExplosionParameters parameters = RandomParameters(random, broken.lower, broken.upper);
      const float stop = NeuronCore::BoundExplosion(parameters, broken.lower, broken.upper, broken.radius).stopSeconds;
      const float done = MotionDoneSeconds(parameters, stop);
      for (std::uint32_t voxel = 0; voxel < broken.centers.size(); ++voxel)
      {
        const NeuronCore::VoxelPose end = PoseOf(broken, voxel, parameters, done);
        for (const float later : {2.0f * done, 10.0f * done})
        {
          Assert::IsTrue(SamePose(end, PoseOf(broken, voxel, parameters, later)), std::format(L"block {}, voxel {}", block, voxel).c_str());
        }
      }
    }
  }

  TEST_METHOD(NoSpinMeansNoRotation)
  {
    SeededRandom random(20261101u);
    const BrokenVoxels broken = MakeBrokenVoxels(random, 600);
    NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters({8.0f, 8.0f, 8.0f});
    parameters.maxSpinRadians = 0.0f;
    const float stop = NeuronCore::BoundExplosion(parameters, broken.lower, broken.upper, broken.radius).stopSeconds;
    for (std::uint32_t voxel = 0; voxel < broken.centers.size(); ++voxel)
    {
      for (const float fraction : {0.01f, 0.1f, 0.5f, 1.0f, 2.0f})
      {
        Assert::IsTrue(IsIdentity(PoseOf(broken, voxel, parameters, stop * fraction)));
      }
    }
  }

  // ADR-024: a fragment is one rigid body. Every voxel of it turns alike, exactly, and any two keep their distance.
  TEST_METHOD(FragmentsMoveAsRigidBodies)
  {
    SeededRandom random(20261102u);
    std::uint32_t pairs = 0;
    for (std::uint32_t block = 0; block < 12; ++block)
    {
      const BrokenVoxels broken = MakeBrokenVoxels(random, 900);
      const NeuronCore::ExplosionParameters parameters = RandomParameters(random, broken.lower, broken.upper);
      // The first voxel of each fragment, in voxel order.
      std::vector<std::uint32_t> firstOf(broken.fragments.size(), 0xFFFFFFFFu);
      for (const float time : {0.05f, 0.4f, 1.5f, 6.0f})
      {
        for (std::uint32_t voxel = 0; voxel < broken.centers.size(); ++voxel)
        {
          std::uint32_t& first = firstOf[broken.fragmentOf[voxel]];
          if (first == 0xFFFFFFFFu)
          {
            first = voxel;
            continue;
          }
          const NeuronCore::VoxelPose a = PoseOf(broken, first, parameters, time);
          const NeuronCore::VoxelPose b = PoseOf(broken, voxel, parameters, time);
          const std::wstring what = std::format(L"block {}, voxels {} and {}, {} s", block, first, voxel, time);
          Assert::IsTrue(SamePose({a.center, b.axisX, b.axisY, b.axisZ}, a), what.c_str());
          const float rest = NeuronCore::Length(broken.centers[voxel] - broken.centers[first]);
          const float now = NeuronCore::Length(b.center - a.center);
          Assert::AreEqual(rest, now, POSITION_ROUNDING * 8.0f * (NeuronCore::Length(a.center) + NeuronCore::Length(b.center) + 1.0f),
                           what.c_str());
          ++pairs;
        }
      }
    }
    Logger::WriteMessage(std::format(L"{} pairs of voxels in one fragment followed\n", pairs).c_str());
    Assert::IsTrue(pairs > 1000u, L"enough fragments hold several voxels for the test to say something");
  }

  // ADR-024: a fragment starts once the blast has crossed the model to its pivot; until then only the inherited velocity
  // moves it, and without one it is exactly intact.
  TEST_METHOD(NothingMovesBeforeTheBlastArrives)
  {
    SeededRandom random(20261103u);
    for (std::uint32_t block = 0; block < 8; ++block)
    {
      const BrokenVoxels broken = MakeBrokenVoxels(random, 600);
      NeuronCore::ExplosionParameters parameters = RandomParameters(random, broken.lower, broken.upper);
      parameters.inheritedVelocity = {0.0f, 0.0f, 0.0f};
      parameters.shockSpeed = random.Uniform(1.0f, 40.0f);
      for (std::uint32_t voxel = 0; voxel < broken.centers.size(); ++voxel)
      {
        const NeuronCore::Fragment& shape = broken.fragments[broken.fragmentOf[voxel]];
        const float delay = NeuronCore::Length(shape.pivot - parameters.blastOrigin) / parameters.shockSpeed;
        const std::wstring what = std::format(L"block {}, voxel {}", block, voxel);
        for (const float fraction : {0.0f, 0.5f, 0.99f})
        {
          const NeuronCore::VoxelPose pose = PoseOf(broken, voxel, parameters, delay * fraction);
          Assert::IsTrue(IsIdentity(pose) && Same(pose.center, broken.centers[voxel]), what.c_str());
        }
      }
    }
  }

  // ADR-024: a larger fragment, of a smaller size scale, launches slower under a lower drag, and ends nearer: its pivot
  // travels s / max(drag √s, minDrag) of what a lone voxel's does from the same place.
  TEST_METHOD(LargerFragmentsFlySlowerAndLessFar)
  {
    SeededRandom random(20261104u);
    for (std::uint32_t block = 0; block < 16; ++block)
    {
      const NeuronCore::ExplosionParameters parameters = RandomParameters(random, {-20.0f, -20.0f, -20.0f}, {20.0f, 20.0f, 20.0f});
      NeuronCore::ExplosionParameters still = parameters;
      still.inheritedVelocity = {0.0f, 0.0f, 0.0f};
      const Float3 pivot = random.InBox({-20.0f, -20.0f, -20.0f}, {20.0f, 20.0f, 20.0f});
      const float scale = random.Uniform(0.05f, 1.0f);
      const std::uint32_t fragment = random.Below(100000u);
      const float done = MotionDoneSeconds(still, NeuronCore::Length(pivot - still.blastOrigin) / still.shockSpeed);
      const float lone = NeuronCore::Length(NeuronCore::ExplosionPose(fragment, {pivot, 1.0f}, pivot, still, done).center - pivot);
      const float large = NeuronCore::Length(NeuronCore::ExplosionPose(fragment, {pivot, scale}, pivot, still, done).center - pivot);
      const float expected = lone * scale / std::max(still.drag * std::sqrt(scale), still.minDrag) * still.drag;
      Assert::AreEqual(expected, large, 1.0e-4f * (lone + 1.0f), std::format(L"block {}", block).c_str());
      Assert::IsTrue(large <= lone * (1.0f + 1.0e-5f), std::format(L"block {}", block).c_str());
    }
  }

  // Design/Archive/SpaceScene.md §7.7: a detonation's seed changes every fragment's randomness, and the same seed repeats it
  // (D3).
  TEST_METHOD(SeedsGiveDifferentDebris)
  {
    const NeuronCore::ExplosionParameters first = NeuronCore::DefaultExplosionParameters({0.0f, 0.0f, 0.0f});
    NeuronCore::ExplosionParameters second = first;
    second.seed = 1u;
    std::uint32_t moved = 0;
    for (std::uint32_t fragment = 0; fragment < 64u; ++fragment)
    {
      const Float3 restCenter = BlockCenter(fragment);
      const NeuronCore::Fragment shape{restCenter, 1.0f};
      const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(fragment, shape, restCenter, second, 1.0f);
      Assert::IsTrue(SamePose(pose, NeuronCore::ExplosionPose(fragment, shape, restCenter, second, 1.0f)), L"the same seed repeats itself");
      moved += SamePose(pose, NeuronCore::ExplosionPose(fragment, shape, restCenter, first, 1.0f)) ? 0u : 1u;
    }
    Assert::AreEqual(64u, moved, L"every fragment flies differently under another seed");
  }

  // §7.7: the inherited velocity carries every fragment alike, under the lone voxel's drag, by the velocity times the
  // motion made over the drag, and leaves every turn as it was.
  TEST_METHOD(InheritedVelocityCarriesEveryVoxel)
  {
    SeededRandom random(20261015u);
    for (std::uint32_t block = 0; block < 16u; ++block)
    {
      const BrokenVoxels broken = MakeBrokenVoxels(random, 300);
      const NeuronCore::ExplosionParameters carried = RandomParameters(random, broken.lower, broken.upper);
      NeuronCore::ExplosionParameters still = carried;
      still.inheritedVelocity = {0.0f, 0.0f, 0.0f};
      for (const float time : {0.0f, 0.3f, 1.0f, 4.0f, MOTION_DONE / carried.drag})
      {
        const float made = time > 0.0f ? 1.0f - std::exp(-carried.drag * time) : 0.0f;
        const Float3 expected = carried.inheritedVelocity * (made / carried.drag);
        for (std::uint32_t voxel = 0; voxel < broken.centers.size(); ++voxel)
        {
          const NeuronCore::VoxelPose moving = PoseOf(broken, voxel, carried, time);
          const NeuronCore::VoxelPose resting = PoseOf(broken, voxel, still, time);
          const std::wstring what = std::format(L"block {}, voxel {}, {} s", block, voxel, time);
          const float allowed = POSITION_ROUNDING * 4.0f * (NeuronCore::Length(moving.center) + NeuronCore::Length(resting.center) + 1.0f);
          Assert::IsTrue(NeuronCore::Length(moving.center - resting.center - expected) <= allowed, what.c_str());
          Assert::IsTrue(SamePose({moving.center, resting.axisX, resting.axisY, resting.axisZ}, moving), what.c_str());
        }
      }
    }
  }

  // The closed-form envelope of §5.5 against the motion itself, for parameter blocks well beyond the defaults, with a lone
  // voxel at the blast origin in each: every voxel exactly intact at time 0, every sampled box inside the sphere at its
  // time and inside the sphere around the whole drift, and, from the stop time on, every corner within
  // EXPLOSION_STOP_DISTANCE of where it ends.
  TEST_METHOD(RandomExplosionsStayInsideTheirEnvelope)
  {
    SeededRandom random(20260927u);
    std::uint32_t followed = 0;
    for (std::uint32_t block = 0; block < 24; ++block)
    {
      BrokenVoxels broken = MakeBrokenVoxels(random, 400);
      const NeuronCore::ExplosionParameters parameters = RandomParameters(random, broken.lower, broken.upper);
      broken.centers.push_back(parameters.blastOrigin);
      broken.fragmentOf.push_back(static_cast<std::uint32_t>(broken.fragments.size()));
      broken.fragments.push_back({parameters.blastOrigin, 1.0f});
      const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, broken.lower, broken.upper, broken.radius);
      Assert::IsTrue(envelope.stopSeconds >= 0.0f, std::format(L"block {} stops", block).c_str());
      const NeuronCore::Sphere whole = NeuronCore::EnvelopeSphere(envelope);
      const float slack =
        POSITION_ROUNDING * (envelope.radius + NeuronCore::Length(envelope.drift) + NeuronCore::Length(envelope.center)) + 1.0e-3f;
      const float done = MotionDoneSeconds(parameters, envelope.stopSeconds);
      for (std::uint32_t voxel = 0; voxel < broken.centers.size(); ++voxel)
      {
        const Float3 restCenter = broken.centers[voxel];
        const NeuronCore::VoxelPose intact = PoseOf(broken, voxel, parameters, 0.0f);
        Assert::IsTrue(IsIdentity(intact) && Same(intact.center, restCenter),
                       std::format(L"block {}, voxel {} is intact at time 0", block, voxel).c_str());
        const NeuronCore::VoxelPose end = PoseOf(broken, voxel, parameters, done);
        for (std::uint32_t sample = 0; sample <= FLIGHT_SAMPLES; ++sample)
        {
          const float time = 2.0f * envelope.stopSeconds * static_cast<float>(sample) / static_cast<float>(FLIGHT_SAMPLES);
          const NeuronCore::VoxelPose pose = PoseOf(broken, voxel, parameters, time);
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

  // A center moves no faster than its fragment's launch, which is at most the launch speed at the origin with all the
  // variation, plus the fastest turn at the farthest a center lies from its pivot, plus the inherited velocity: the drag
  // only slows them.
  TEST_METHOD(RandomExplosionsMoveNoFasterThanTheirLaunch)
  {
    constexpr float STEP_SECONDS = 1.0e-3f;
    SeededRandom random(20260929u);
    for (std::uint32_t block = 0; block < 16; ++block)
    {
      const BrokenVoxels broken = MakeBrokenVoxels(random, 300);
      const NeuronCore::ExplosionParameters parameters = RandomParameters(random, broken.lower, broken.upper);
      const float fastest = parameters.launchSpeed * std::exp(3.0f * parameters.speedSpread) +
                            parameters.maxSpinRadians * parameters.drag * broken.radius + NeuronCore::Length(parameters.inheritedVelocity);
      const float stop = NeuronCore::BoundExplosion(parameters, broken.lower, broken.upper, broken.radius).stopSeconds;
      for (std::uint32_t voxel = 0; voxel < broken.centers.size(); ++voxel)
      {
        for (std::uint32_t sample = 0; sample < 32; ++sample)
        {
          const float time = stop * static_cast<float>(sample) / 32.0f;
          const Float3 before = PoseOf(broken, voxel, parameters, time).center;
          const Float3 after = PoseOf(broken, voxel, parameters, time + STEP_SECONDS).center;
          const float moved = NeuronCore::Length(after - before);
          Assert::IsTrue(moved <= fastest * STEP_SECONDS * 1.001f + POSITION_ROUNDING * (NeuronCore::Length(after) + 1.0f) * 4.0f,
                         std::format(L"block {}, voxel {} moves {} in {} s at {} s", block, voxel, moved, STEP_SECONDS, time).c_str());
        }
      }
    }
  }
};

} // namespace NeuronCoreTests
