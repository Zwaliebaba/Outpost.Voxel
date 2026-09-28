#include "pch.h"

#include "GraphicsDevice.h"
#include "SplatPass.h"
#include "TestSupport.h"
#include "VoxelScene.h"

#include "Box.h"
#include "Explosion.h"
#include "Float3.h"
#include "Lighting.h"
#include "OctahedralNormal.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "Placement.h"
#include "Quaternion.h"
#include "Ray.h"
#include "RigidTransform.h"
#include "Sphere.h"
#include "TraceHit.h"
#include "VoxModel.h"

#include <algorithm>
#include <array>
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

using NeuronCore::Float3;

constexpr float RADIANS_PER_DEGREE = 0.0174532925f;
constexpr Float3 WORLD_UP{0.0f, 1.0f, 0.0f};

// How far inside or outside a box a ray may pass and still be answered differently by the GPU and the twin, in voxel
// units: the sliver the other splat tests use (Design/Archive/SampleRenderer.md §14).
constexpr float EDGE_EPSILON = 1.0f / 256.0f;

// The GPU poses each voxel with its own sin, cos and sqrt, so an exploded box sits where the twin puts it to rounding,
// not bit for bit. A hit's depth agrees to this fraction, and its normal, which the visibility buffer also packs (§7.3),
// to this much in each component.
constexpr float POSED_DEPTH_TOLERANCE = 1.0e-4f;
constexpr float POSED_NORMAL_TOLERANCE = 2.0e-3f;

// Mismatches on an edge allowed per image or map, which §14 sets from the first measured run. That run, on WARP in CI on
// 2026-09-27, found none in any of its ten images and maps. The bound is not zero for the reason the other splat tests
// give: §14 assumes no bit equality between CPU and GPU, and the GPU poses each box with its own sin, cos and sqrt, so a
// ray within rounding of an edge can land on its other side after a toolchain update. Four is headroom.
constexpr std::uint32_t EDGE_MISMATCH_LIMIT = 4;

// The block's times: early in its flight, while it slows, and as it nears its end; and its envelope's stop time.
constexpr std::array<float, 3> BLOCK_TIMES_SECONDS{0.1f, 0.4f, 1.0f};

constexpr std::uint32_t VIEW_WIDTH_PIXELS = 241;
constexpr std::uint32_t VIEW_HEIGHT_PIXELS = 137;
constexpr std::uint32_t MAP_PIXELS = 256;

struct Comparison
{
  std::uint32_t hits = 0;
  std::uint32_t misses = 0;
  std::uint32_t edgeMismatches = 0;
  std::vector<std::wstring> failures;
};

void Report(const std::wstring& _image, const Comparison& _comparison)
{
  Logger::WriteMessage(std::format(L"{}: {} hits, {} misses, {} mismatches on an edge, {} failures\n", _image, _comparison.hits,
                                   _comparison.misses, _comparison.edgeMismatches, _comparison.failures.size())
                         .c_str());
  for (std::size_t i = 0; i < std::min<std::size_t>(_comparison.failures.size(), 10); ++i)
  {
    Logger::WriteMessage((_comparison.failures[i] + L"\n").c_str());
  }
  Assert::IsTrue(_comparison.hits > 0, std::format(L"{}: something is hit", _image).c_str());
  Assert::IsTrue(_comparison.failures.empty(), std::format(L"{}: {} pixels disagree", _image, _comparison.failures.size()).c_str());
  Assert::IsTrue(
    _comparison.edgeMismatches <= EDGE_MISMATCH_LIMIT,
    std::format(L"{}: {} mismatches on an edge, more than {}", _image, _comparison.edgeMismatches, EDGE_MISMATCH_LIMIT).c_str());
}

// Two drawings of one view that must agree: the same voxel and normal in every pixel, and the same depth to rounding. A
// pixel that differs counts as a mismatch on an edge, where rounding alone can tip a voxel's seam either way.
[[nodiscard]] Comparison CompareDrawings(const SplatImage& _expected, const SplatImage& _actual)
{
  Comparison result;
  for (std::size_t pixel = 0; pixel < _expected.depth.size(); ++pixel)
  {
    const std::uint32_t voxel = _expected.visibility[2 * pixel];
    ++(voxel == NeuronCore::NO_VOXEL ? result.misses : result.hits);
    const bool same = voxel == _actual.visibility[2 * pixel] && _expected.visibility[2 * pixel + 1] == _actual.visibility[2 * pixel + 1] &&
                      std::abs(_expected.depth[pixel] - _actual.depth[pixel]) <= 1.0e-6f * _expected.depth[pixel];
    result.edgeMismatches += same ? 0u : 1u;
  }
  return result;
}

// The block's detonation: the defaults' shape, scaled to an 8-voxel block, so that its debris stays within a camera's
// reach and every voxel still covers a few pixels.
[[nodiscard]] NeuronCore::ExplosionParameters BlockExplosion(const NeuronCore::VoxModel& _model)
{
  NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(_model));
  parameters.launchSpeed = 6.0f;
  parameters.falloffDistance = 8.0f;
  parameters.drag = 1.5f;
  return parameters;
}

// The block's times, BLOCK_TIMES_SECONDS and then its envelope's stop time.
[[nodiscard]] std::vector<float> BlockTimes(const NeuronCore::ExplosionEnvelope& _envelope)
{
  std::vector<float> times(BLOCK_TIMES_SECONDS.begin(), BLOCK_TIMES_SECONDS.end());
  times.push_back(_envelope.stopSeconds);
  return times;
}

// A view of the envelope's sphere from an elevated three-quarter direction that frames it.
[[nodiscard]] NeuronCore::PerspectiveView EnvelopeView(const NeuronCore::ExplosionEnvelope& _envelope)
{
  const Float3 direction = NeuronCore::Normalize({0.6f, 0.55f, -0.8f});
  const float distance = _envelope.radius / std::sin(0.5f * TEST_FOV_Y_RADIANS);
  return NeuronCore::MakePerspectiveView(_envelope.center + direction * distance, _envelope.center, WORLD_UP, TEST_FOV_Y_RADIANS,
                                         TEST_NEAR_PLANE, VIEW_WIDTH_PIXELS, VIEW_HEIGHT_PIXELS);
}

// The sun's view of the envelope: the station's sun, over a square that holds the envelope's sphere, deep enough for all
// of it.
[[nodiscard]] NeuronCore::OrthographicView EnvelopeShadowView(const NeuronCore::ExplosionEnvelope& _envelope)
{
  const Float3 toSun = NeuronCore::SunDirection(50.0f * RADIANS_PER_DEGREE, 50.0f * RADIANS_PER_DEGREE);
  const Float3 reach{_envelope.radius, _envelope.radius, _envelope.radius};
  return NeuronCore::MakeShadowView(toSun, _envelope.center, _envelope.radius, _envelope.center - reach, _envelope.center + reach,
                                    MAP_PIXELS);
}

// Whether _ray meets _box beyond _minDistance.
[[nodiscard]] bool Meets(const NeuronCore::Ray& _ray, const NeuronCore::Box& _box, float _minDistance) noexcept
{
  float distance = 0.0f;
  Float3 normal{};
  return NeuronCore::IntersectBox<true, false>(_box, _ray.origin, _ray.direction, NeuronCore::InverseDirection(_ray), distance, normal) &&
         distance >= _minDistance;
}

// Whether the point _distance along _ray lies within EDGE_EPSILON of an edge of _box, where two of its faces meet and
// rounding may pick either.
[[nodiscard]] bool NearAnEdge(const NeuronCore::Ray& _ray, float _distance, const NeuronCore::Box& _box) noexcept
{
  const Float3 offset = _ray.origin + _ray.direction * _distance - _box.center;
  std::uint32_t atFaces = 0;
  for (const float along : {NeuronCore::Dot(offset, _box.axisX), NeuronCore::Dot(offset, _box.axisY), NeuronCore::Dot(offset, _box.axisZ)})
  {
    atFaces += std::abs(along) >= 0.5f - EDGE_EPSILON ? 1u : 0u;
  }
  return atFaces >= 2u;
}

[[nodiscard]] float ShadowDepth(const NeuronCore::OrthographicView& _view, const NeuronCore::TraceHit& _hit) noexcept
{
  return _hit.voxel == NeuronCore::NO_VOXEL ? NeuronCore::ORTHOGRAPHIC_FAR_DEPTH : NeuronCore::OrthographicDepth(_view, _hit.distance);
}

// The per-pixel rule of §14 for posed boxes. Away from edges the GPU must find the twin's voxel, at its depth and with its
// normal, to rounding. Near an edge, within EDGE_EPSILON of one, it may find another face of the same voxel, or another
// voxel: one whose grown box the ray meets, where the twin's voxel, if any, is missed by its shrunk box.
[[nodiscard]] Comparison CompareView(const NeuronCore::PerspectiveView& _view, const std::vector<NeuronCore::Box>& _exact,
                                     const std::vector<NeuronCore::Box>& _grown, const std::vector<NeuronCore::Box>& _shrunk,
                                     const SplatImage& _image)
{
  Comparison result;
  for (std::uint32_t y = 0; y < _view.heightPixels; ++y)
  {
    for (std::uint32_t x = 0; x < _view.widthPixels; ++x)
    {
      const std::size_t pixel = static_cast<std::size_t>(y) * _view.widthPixels + x;
      const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(_view, x, y);
      const NeuronCore::TraceHit expected = NeuronCore::TraceBoxes<true>(_exact, ray, _view.nearPlane);
      const std::uint32_t voxel = _image.visibility[2 * pixel];
      const float depth = _image.depth[pixel];
      if (voxel == expected.voxel && voxel == NeuronCore::NO_VOXEL)
      {
        ++result.misses;
        if (depth != NeuronCore::PERSPECTIVE_FAR_DEPTH)
        {
          result.failures.push_back(std::format(L"({}, {}): no voxel, yet depth {}", x, y, depth));
        }
        continue;
      }
      if (voxel == expected.voxel)
      {
        ++result.hits;
        const Float3 normal = NeuronCore::UnpackOctahedralNormal(_image.visibility[2 * pixel + 1]);
        const Float3 difference = NeuronCore::Abs(normal - expected.normal);
        if (NeuronCore::MaxComponent(difference) > POSED_NORMAL_TOLERANCE && NearAnEdge(ray, expected.distance, _exact[voxel]))
        {
          ++result.edgeMismatches;
          continue;
        }
        if (NeuronCore::MaxComponent(difference) > POSED_NORMAL_TOLERANCE)
        {
          result.failures.push_back(std::format(L"({}, {}): voxel {} with normal ({}, {}, {}), the twin's ({}, {}, {})", x, y, voxel,
                                                normal.x, normal.y, normal.z, expected.normal.x, expected.normal.y, expected.normal.z));
        }
        const float expectedDepth = NeuronCore::PerspectiveDepth(_view, expected.distance);
        if (std::abs(depth - expectedDepth) > POSED_DEPTH_TOLERANCE * expectedDepth)
        {
          result.failures.push_back(std::format(L"({}, {}): voxel {} at depth {}, the twin's {}", x, y, voxel, depth, expectedDepth));
        }
        continue;
      }
      const bool gpuNear = voxel == NeuronCore::NO_VOXEL || (voxel < _grown.size() && Meets(ray, _grown[voxel], _view.nearPlane));
      const bool twinGrazed = expected.voxel == NeuronCore::NO_VOXEL || !Meets(ray, _shrunk[expected.voxel], _view.nearPlane);
      if (gpuNear && twinGrazed)
      {
        ++result.edgeMismatches;
        continue;
      }
      result.failures.push_back(std::format(L"({}, {}): the twin finds voxel {}, the GPU {}", x, y, expected.voxel, voxel));
    }
  }
  return result;
}

// The shadow map's rule: the depth the twin's boxes give, to rounding, or, near an edge, a depth between the answers for
// grown and shrunk boxes.
[[nodiscard]] Comparison CompareShadow(const NeuronCore::OrthographicView& _view, const std::vector<NeuronCore::Box>& _exact,
                                       const std::vector<NeuronCore::Box>& _grown, const std::vector<NeuronCore::Box>& _shrunk,
                                       const std::vector<float>& _depth)
{
  Comparison result;
  for (std::uint32_t y = 0; y < _view.heightPixels; ++y)
  {
    for (std::uint32_t x = 0; x < _view.widthPixels; ++x)
    {
      const NeuronCore::Ray ray = NeuronCore::OrthographicRay(_view, x, y);
      const NeuronCore::TraceHit hit = NeuronCore::TraceBoxes<true>(_exact, ray, 0.0f);
      const float expected = ShadowDepth(_view, hit);
      const float actual = _depth[static_cast<std::size_t>(y) * _view.widthPixels + x];
      ++(hit.voxel == NeuronCore::NO_VOXEL ? result.misses : result.hits);
      if (std::abs(actual - expected) <= POSED_DEPTH_TOLERANCE * std::max(expected, 1.0e-3f))
      {
        continue;
      }
      const float nearest = ShadowDepth(_view, NeuronCore::TraceBoxes<true>(_grown, ray, 0.0f));
      const float farthest = ShadowDepth(_view, NeuronCore::TraceBoxes<true>(_shrunk, ray, 0.0f));
      if (actual >= nearest - POSED_DEPTH_TOLERANCE && actual <= farthest + POSED_DEPTH_TOLERANCE)
      {
        ++result.edgeMismatches;
        continue;
      }
      result.failures.push_back(
        std::format(L"({}, {}): depth {}, the twin's {} (between {} and {} near an edge)", x, y, actual, expected, nearest, farthest));
    }
  }
  return result;
}

} // namespace

// The oriented splat permutations, which draw the detonation (Design/Archive/SampleRenderer.md §9.2, §14;
// Design/SpaceScene.md §5.5).
TEST_CLASS(ExplosionSplatTests)
{
public:
  // §14, Design/SpaceScene.md §5.5: at time 0 every voxel is intact, and the oriented permutations draw what the aligned
  // ones do.
  TEST_METHOD(OrientedMatchesAlignedAtTimeZero)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronClient::VoxelScene scene(_device, {&model, 1});
        const std::vector<NeuronCore::Placement> whole = WholePlacements(model);
        const std::vector<NeuronCore::Placement> intact =
          DetonatePlacements(whole, NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model)), 0.0f);

        const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView({180.0f, 210.0f, -260.0f}, {0.0f, 110.0f, 0.0f}, WORLD_UP,
                                                                                 TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE, 161, 91);
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::View);
        Report(L"view at time 0",
               CompareDrawings(RenderSplat(_device, scene, whole, pass, view), RenderSplat(_device, scene, intact, pass, view)));

        const NeuronCore::OrthographicView sun =
          TestShadowView(model, NeuronCore::SunDirection(50.0f * RADIANS_PER_DEGREE, 50.0f * RADIANS_PER_DEGREE), 160.0f, MAP_PIXELS);
        const NeuronClient::SplatPass shadowPass(_device, NeuronClient::SplatPass::Kind::Shadow);
        const std::vector<float> alignedMap = RenderShadowSplat(_device, scene, whole, shadowPass, sun);
        const std::vector<float> orientedMap = RenderShadowSplat(_device, scene, intact, shadowPass, sun);
        Comparison shadowComparison;
        for (std::size_t texel = 0; texel < alignedMap.size(); ++texel)
        {
          ++(alignedMap[texel] == NeuronCore::ORTHOGRAPHIC_FAR_DEPTH ? shadowComparison.misses : shadowComparison.hits);
          shadowComparison.edgeMismatches += std::abs(alignedMap[texel] - orientedMap[texel]) <= 1.0e-6f ? 0u : 1u;
        }
        Report(L"shadow map at time 0", shadowComparison);
      });
  }

  // §14: a synthetic 8³ model at several times, against brute-force intersection of every box where the twin poses it.
  // The oriented measurement variants (§9.3, §11) draw exactly what the standard pass draws.
  TEST_METHOD(ExplodedBlockMatchesTheTwin)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = RandomBlock();
        const NeuronClient::VoxelScene scene(_device, {&model, 1});
        const NeuronCore::ExplosionParameters parameters = BlockExplosion(model);
        const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f, 8.0f});
        const NeuronCore::PerspectiveView view = EnvelopeView(envelope);
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronClient::SplatPass plainDepth(_device, NeuronClient::SplatPass::Kind::View,
                                                 NeuronClient::SplatPass::Variant::PlainDepth);
        const NeuronClient::SplatPass overdraw(_device, NeuronClient::SplatPass::Kind::View, NeuronClient::SplatPass::Variant::Overdraw);
        for (const float time : BlockTimes(envelope))
        {
          const std::vector<NeuronCore::Placement> placements = DetonatePlacements(WholePlacements(model), parameters, time);
          const SplatImage image = RenderSplat(_device, scene, placements, pass, view);
          const Comparison comparison =
            CompareView(view, PlacedBoxes(model.records, placements, 0.0f), PlacedBoxes(model.records, placements, EDGE_EPSILON),
                        PlacedBoxes(model.records, placements, -EDGE_EPSILON), image);
          Report(std::format(L"block at {} s", time), comparison);
          Report(std::format(L"block at {} s, plain depth", time),
                 CompareDrawings(image, RenderSplat(_device, scene, placements, plainDepth, view)));
          Report(std::format(L"block at {} s, overdraw", time),
                 CompareDrawings(image, RenderSplat(_device, scene, placements, overdraw, view)));
        }
      });
  }

  // Design/SpaceScene.md §7.7, §15: the block turned any way and moved, detonated with a seed and an inherited velocity,
  // against brute force over the boxes the twin poses in its part's space and takes into the world, in the view and the
  // sun's map, at several times.
  TEST_METHOD(TurnedDebrisMatchesTheTwin)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = RandomBlock();
        const NeuronClient::VoxelScene scene(_device, {&model, 1});
        std::vector<NeuronCore::Placement> whole{
          PlaceCentered(model, 0, 0, 0, NeuronCore::RotationOf({-0.4f, 0.1f, 0.3f, 0.86f}), {30.0f, -12.0f, 55.0f})};
        Assert::IsTrue(NeuronCore::AssignVoxelIds(whole));
        // The block's detonation, in the world: from its centroid, carried off by a velocity of its own, with a seed.
        NeuronCore::ExplosionParameters parameters = BlockExplosion(model);
        parameters.blastOrigin = NeuronCore::TransformPoint(whole.front().transform, parameters.blastOrigin);
        parameters.inheritedVelocity = {1.5f, 0.5f, -1.0f};
        parameters.seed = 11u;

        // The envelope, bounded in the part's space and taken into the world, around the whole of its drift.
        const NeuronCore::ExplosionParameters partParameters = DetonatePlacements(whole, parameters, 1.0f).front().detonation->parameters;
        const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(partParameters, whole.front().lower, whole.front().upper);
        const NeuronCore::Sphere reach = NeuronCore::EnvelopeSphere(envelope);
        const NeuronCore::ExplosionEnvelope world{
          NeuronCore::TransformPoint(whole.front().transform, reach.center), reach.radius, {0.0f, 0.0f, 0.0f}, envelope.stopSeconds};
        const NeuronCore::PerspectiveView view = EnvelopeView(world);
        const NeuronCore::OrthographicView sun = EnvelopeShadowView(world);
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronClient::SplatPass shadowPass(_device, NeuronClient::SplatPass::Kind::Shadow);
        for (const float time : BlockTimes(envelope))
        {
          const std::vector<NeuronCore::Placement> placements = DetonatePlacements(whole, parameters, time);
          const std::vector<NeuronCore::Box> exact = PlacedBoxes(model.records, placements, 0.0f);
          const std::vector<NeuronCore::Box> grown = PlacedBoxes(model.records, placements, EDGE_EPSILON);
          const std::vector<NeuronCore::Box> shrunk = PlacedBoxes(model.records, placements, -EDGE_EPSILON);
          Report(std::format(L"turned block at {} s", time),
                 CompareView(view, exact, grown, shrunk, RenderSplat(_device, scene, placements, pass, view)));
          Report(std::format(L"turned block's shadow at {} s", time),
                 CompareShadow(sun, exact, grown, shrunk, RenderShadowSplat(_device, scene, placements, shadowPass, sun)));
        }
      });
  }

  // §10, §14: the oriented shadow splat against the twin's posed boxes, under the station's sun.
  TEST_METHOD(ExplodedBlockCastsTheTwinsShadow)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = RandomBlock();
        const NeuronClient::VoxelScene scene(_device, {&model, 1});
        const NeuronCore::ExplosionParameters parameters = BlockExplosion(model);
        const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f, 8.0f});
        const NeuronCore::OrthographicView view = EnvelopeShadowView(envelope);
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::Shadow);
        for (const float time : BlockTimes(envelope))
        {
          const std::vector<NeuronCore::Placement> placements = DetonatePlacements(WholePlacements(model), parameters, time);
          const std::vector<float> depth = RenderShadowSplat(_device, scene, placements, pass, view);
          const Comparison comparison =
            CompareShadow(view, PlacedBoxes(model.records, placements, 0.0f), PlacedBoxes(model.records, placements, EDGE_EPSILON),
                          PlacedBoxes(model.records, placements, -EDGE_EPSILON), depth);
          Report(std::format(L"block's shadow at {} s", time), comparison);
        }
      });
  }
};

} // namespace NeuronClientTests
