#include "pch.h"

#include "ExplosionConstants.h"
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
#include "Ray.h"
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
constexpr Float3 WORLD_UP{0.0f, 0.0f, 1.0f};

// How far inside or outside a box a ray may pass and still be answered differently by the GPU and the twin, in voxel
// units: the sliver the other splat tests use (Design/SampleRenderer.md §14).
constexpr float EDGE_EPSILON = 1.0f / 256.0f;

// The GPU poses each voxel with its own sin, cos and sqrt, so an exploded box sits where the twin puts it to rounding,
// not bit for bit. A hit's depth agrees to this fraction, and its normal, which the visibility buffer also packs (§7.3),
// to this much in each component.
constexpr float POSED_DEPTH_TOLERANCE = 1.0e-4f;
constexpr float POSED_NORMAL_TOLERANCE = 2.0e-3f;

// Mismatches on an edge allowed per image or map: set from the first measured run, which the tests print.
constexpr std::uint32_t EDGE_MISMATCH_LIMIT = 16;

// The block's times: early in its first flight, around its bounces, and once every voxel has come to rest.
constexpr std::array<float, 4> BLOCK_TIMES_SECONDS{0.1f, 0.4f, 1.0f, 3.0f};

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

// The block's explosion: the defaults' shape, scaled to an 8-voxel block, so that its debris stays within a camera's
// reach and every voxel still covers a few pixels.
[[nodiscard]] NeuronCore::ExplosionParameters BlockExplosion(const NeuronCore::VoxModel& _model)
{
  NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(_model));
  parameters.gravity = 10.0f;
  parameters.launchSpeed = 6.0f;
  parameters.falloffDistance = 8.0f;
  return parameters;
}

// Every record's box where the twin poses it at _timeSeconds, its half-extents changed by _change: grown boxes are hit
// wherever a ray passes near an edge, shrunk ones only well inside.
[[nodiscard]] std::vector<NeuronCore::Box> PosedBoxes(const NeuronCore::VoxModel& _model,
                                                      const NeuronCore::ExplosionParameters& _parameters, float _timeSeconds, float _change)
{
  std::vector<NeuronCore::Box> boxes;
  boxes.reserve(_model.records.size());
  const float radius = 0.5f + _change;
  for (std::uint32_t record = 0; record < _model.records.size(); ++record)
  {
    const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(record, RecordBox(_model, record).center, _parameters, _timeSeconds);
    boxes.push_back(NeuronCore::MakeOrientedBox(pose.center, {radius, radius, radius}, pose.axisX, pose.axisY, pose.axisZ));
  }
  return boxes;
}

// The sphere around the envelope, and a view of it from an elevated three-quarter direction that frames it.
[[nodiscard]] NeuronCore::PerspectiveView EnvelopeView(const NeuronCore::ExplosionEnvelope& _envelope)
{
  const Float3 center = (_envelope.lower + _envelope.upper) * 0.5f;
  const float radius = NeuronCore::Length(_envelope.upper - center);
  const Float3 direction = NeuronCore::Normalize({0.6f, -0.8f, 0.55f});
  const float distance = radius / std::sin(0.5f * TEST_FOV_Y_RADIANS);
  return NeuronCore::MakePerspectiveView(center + direction * distance, center, WORLD_UP, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE,
                                         VIEW_WIDTH_PIXELS, VIEW_HEIGHT_PIXELS);
}

// The sun's view of the envelope: the station's sun, over a square that holds the envelope, deep enough for all of it.
[[nodiscard]] NeuronCore::OrthographicView EnvelopeShadowView(const NeuronCore::ExplosionEnvelope& _envelope)
{
  const Float3 center = (_envelope.lower + _envelope.upper) * 0.5f;
  const float halfExtent = NeuronCore::Length(_envelope.upper - center);
  const Float3 toSun = NeuronCore::SunDirection(50.0f * RADIANS_PER_DEGREE, 50.0f * RADIANS_PER_DEGREE);
  return NeuronCore::MakeShadowView(toSun, center, halfExtent, _envelope.lower, _envelope.upper, MAP_PIXELS);
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

// The oriented splat permutations, which draw the explosion (Design/SampleRenderer.md §9.2, §12, §14).
TEST_CLASS(ExplosionSplatTests)
{
public:
  // §12, §14: at time 0 every rotation is the identity, and the oriented permutations draw what the aligned ones do.
  TEST_METHOD(OrientedMatchesAlignedAtTimeZero)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronClient::VoxelScene scene(_device, model);
        const NeuronClient::ExplosionConstants intact =
          NeuronClient::MakeExplosionConstants(NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model)), 0.0f);

        const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView({180.0f, -260.0f, 210.0f}, {0.0f, 0.0f, 110.0f}, WORLD_UP,
                                                                                 TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE, 161, 91);
        const NeuronClient::SplatPass aligned(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronClient::SplatPass oriented(_device, NeuronClient::SplatPass::Kind::View,
                                               NeuronClient::SplatPass::Permutation::Oriented);
        const SplatImage alignedImage = RenderSplat(_device, scene, aligned, view);
        const SplatImage orientedImage = RenderSplat(_device, scene, oriented, view, intact);
        Comparison viewComparison;
        for (std::size_t pixel = 0; pixel < alignedImage.depth.size(); ++pixel)
        {
          const std::uint32_t voxel = alignedImage.visibility[2 * pixel];
          ++(voxel == NeuronCore::NO_VOXEL ? viewComparison.misses : viewComparison.hits);
          const bool same = voxel == orientedImage.visibility[2 * pixel] &&
                            alignedImage.visibility[2 * pixel + 1] == orientedImage.visibility[2 * pixel + 1] &&
                            std::abs(alignedImage.depth[pixel] - orientedImage.depth[pixel]) <= 1.0e-6f * alignedImage.depth[pixel];
          viewComparison.edgeMismatches += same ? 0u : 1u;
        }
        Report(L"view at time 0", viewComparison);

        const NeuronCore::OrthographicView sun =
          TestShadowView(model, NeuronCore::SunDirection(50.0f * RADIANS_PER_DEGREE, 50.0f * RADIANS_PER_DEGREE), 160.0f, MAP_PIXELS);
        const NeuronClient::SplatPass alignedShadow(_device, NeuronClient::SplatPass::Kind::Shadow);
        const NeuronClient::SplatPass orientedShadow(_device, NeuronClient::SplatPass::Kind::Shadow,
                                                     NeuronClient::SplatPass::Permutation::Oriented);
        const std::vector<float> alignedMap = RenderShadowSplat(_device, scene, alignedShadow, sun);
        const std::vector<float> orientedMap = RenderShadowSplat(_device, scene, orientedShadow, sun, intact);
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
  TEST_METHOD(ExplodedBlockMatchesTheTwin)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = RandomBlock();
        const NeuronClient::VoxelScene scene(_device, model);
        const NeuronCore::ExplosionParameters parameters = BlockExplosion(model);
        const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f, 8.0f});
        const NeuronCore::PerspectiveView view = EnvelopeView(envelope);
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::View, NeuronClient::SplatPass::Permutation::Oriented);
        for (const float time : BLOCK_TIMES_SECONDS)
        {
          const SplatImage image = RenderSplat(_device, scene, pass, view, NeuronClient::MakeExplosionConstants(parameters, time));
          const Comparison comparison =
            CompareView(view, PosedBoxes(model, parameters, time, 0.0f), PosedBoxes(model, parameters, time, EDGE_EPSILON),
                        PosedBoxes(model, parameters, time, -EDGE_EPSILON), image);
          Report(std::format(L"block at {} s", time), comparison);
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
        const NeuronClient::VoxelScene scene(_device, model);
        const NeuronCore::ExplosionParameters parameters = BlockExplosion(model);
        const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f, 8.0f});
        const NeuronCore::OrthographicView view = EnvelopeShadowView(envelope);
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::Shadow, NeuronClient::SplatPass::Permutation::Oriented);
        for (const float time : BLOCK_TIMES_SECONDS)
        {
          const std::vector<float> depth =
            RenderShadowSplat(_device, scene, pass, view, NeuronClient::MakeExplosionConstants(parameters, time));
          const Comparison comparison =
            CompareShadow(view, PosedBoxes(model, parameters, time, 0.0f), PosedBoxes(model, parameters, time, EDGE_EPSILON),
                          PosedBoxes(model, parameters, time, -EDGE_EPSILON), depth);
          Report(std::format(L"block's shadow at {} s", time), comparison);
        }
      });
  }
};

} // namespace NeuronClientTests
