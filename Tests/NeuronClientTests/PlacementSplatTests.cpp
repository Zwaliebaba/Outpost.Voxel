#include "pch.h"

#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "LightingConstants.h"
#include "LightingPass.h"
#include "PaletteConstants.h"
#include "ShadowMap.h"
#include "ShadowViewConstants.h"
#include "SplatPass.h"
#include "TestSupport.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "Box.h"
#include "Explosion.h"
#include "Float3.h"
#include "Half.h"
#include "Lighting.h"
#include "OctahedralNormal.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "Placement.h"
#include "Quaternion.h"
#include "Ray.h"
#include "RigidTransform.h"
#include "SceneTracer.h"
#include "Sphere.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Placement;

constexpr std::uint32_t WIDTH_PIXELS = 161;
constexpr std::uint32_t HEIGHT_PIXELS = 91;
constexpr std::uint32_t MAP_PIXELS = 256;
constexpr std::uint32_t LIGHTING_MAP_PIXELS = 1024;
constexpr Float3 WORLD_UP{0.0f, 1.0f, 0.0f};
constexpr float RADIANS_PER_DEGREE = 0.0174532925f;

// How far inside or outside a box a ray may pass and still be answered differently by the GPU and the tracer, in voxel
// units: the sliver the other splat tests use (Design/Archive/SampleRenderer.md §14).
constexpr float EDGE_EPSILON = 1.0f / 256.0f;

// A turned box sits where the twin puts it to rounding, not bit for bit: the GPU may fuse a multiply and an add that the
// C++ does not. A hit's depth agrees to this fraction, and its normal, which the visibility buffer packs (§7.3), to this
// much in each component: the bounds ExplosionSplatTests gives posed boxes.
constexpr float PLACED_DEPTH_TOLERANCE = 1.0e-4f;
constexpr float PLACED_NORMAL_TOLERANCE = 2.0e-3f;

// Mismatches on an edge allowed per image or map, which Design/Archive/SpaceScene.md §15 sets from the first measured run. That
// run, on WARP in CI on 2026-09-28, found none against the scene tracer in any image or map, the one from 10,000 units
// included, and one in each of two views drawn both ways: symmetric placements through either permutation, and
// placements detonated at time 0 against the whole ones. The bound is not zero for the reason the other splat tests give:
// SampleRenderer.md §14 assumes no bit equality between CPU and GPU, and the two permutations intersect a box each in
// their own way, so a ray within rounding of an edge can land on either side of it. Four is headroom.
constexpr std::uint32_t EDGE_MISMATCH_LIMIT = 4;

// The lighting's tolerances, LightingPassTests': half-precision HDR color and 8-bit filter weights.
constexpr float HDR_RELATIVE_TOLERANCE = 1.0e-3f;
constexpr float HDR_ABSOLUTE_TOLERANCE = 4.0e-3f;
constexpr std::uint32_t SHADOW_FLIP_LIMIT = 4;

// The white of Design/Archive/SpaceScene.md §4: palette entry 16, record color 15. The station's glows; the capital ship's does
// not.
constexpr std::uint32_t WHITE = 15;

// Two of the cube's symmetries, which a whole placement draws aligned with, and two rotations that are not, which draw
// oriented.
constexpr NeuronCore::Rotation QUARTER_TURN_ABOUT_Y{{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
constexpr NeuronCore::Rotation HALF_TURN_ABOUT_X{{1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
constexpr NeuronCore::Rotation TILTED = NeuronCore::RotationOf({0.21f, -0.37f, 0.12f, 0.896f});
constexpr NeuronCore::Rotation LEANING = NeuronCore::RotationOf({-0.4f, 0.1f, 0.3f, 0.86f});

// The three models of Design/Archive/SpaceScene.md §4 as one scene's models, in this order.
constexpr std::uint32_t STATION = 0;
constexpr std::uint32_t CAPITAL_SHIP = 1;
constexpr std::uint32_t FRIGATE = 2;

struct ThreeModels
{
  std::vector<NeuronCore::VoxModel> models;
  std::vector<std::uint32_t> records; // the scene's record buffer
  std::vector<std::uint32_t> firstRecords;
};

[[nodiscard]] ThreeModels LoadThreeModels()
{
  ThreeModels three;
  three.models.push_back(LoadGameData(L"MilitaryStation.vox"));
  three.models.push_back(LoadGameData(L"CapitalShip.vox"));
  three.models.push_back(LoadGameData(L"Frigate.vox"));
  three.records = NeuronCore::SceneRecords(three.models);
  three.firstRecords = NeuronCore::ModelFirstRecords(three.models);
  return three;
}

[[nodiscard]] Placement Place(const ThreeModels& _three, std::uint32_t _model, const NeuronCore::Rotation& _rotation, Float3 _center)
{
  return PlaceCentered(_three.models[_model], _model, _three.firstRecords[_model], 0, _rotation, _center);
}

// §15's scene, seen from the origin along +Z: the station a quarter turn about the vertical, 700 units ahead, and a
// capital ship a half turn about x, both aligned; a capital ship and frigates turned any way, oriented, one of them a
// few units from the eye; and a frigate unturned. No two spheres overlap, as the world keeps them (§7.3).
[[nodiscard]] std::vector<Placement> SeveralPlacements(const ThreeModels& _three)
{
  std::vector<Placement> placements{Place(_three, STATION, QUARTER_TURN_ABOUT_Y, {0.0f, 0.0f, 700.0f}),
                                    Place(_three, CAPITAL_SHIP, TILTED, {-95.0f, -25.0f, 260.0f}),
                                    Place(_three, FRIGATE, LEANING, {55.0f, 25.0f, 120.0f}),
                                    Place(_three, FRIGATE, NeuronCore::IDENTITY_ROTATION, {-30.0f, 70.0f, 380.0f}),
                                    Place(_three, CAPITAL_SHIP, HALF_TURN_ABOUT_X, {120.0f, -60.0f, 420.0f}),
                                    Place(_three, FRIGATE, TILTED, {9.0f, -9.0f, 12.0f})};
  Assert::IsTrue(NeuronCore::AssignVoxelIds(placements), L"the scene's ids fit");
  return placements;
}

[[nodiscard]] NeuronCore::PerspectiveView FromTheOrigin() noexcept
{
  return NeuronCore::MakePerspectiveView({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, WORLD_UP, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE,
                                         WIDTH_PIXELS, HEIGHT_PIXELS);
}

// The same scene from 10,000 units further back, narrowed to frame it as before: where a standard-Z buffer could not
// separate the surfaces (Design/Archive/SpaceScene.md §9).
[[nodiscard]] NeuronCore::PerspectiveView FromFarAway() noexcept
{
  constexpr float DISTANCE = 10000.0f;
  const float fovYRadians = 2.0f * std::atan(320.0f / (DISTANCE + 700.0f));
  return NeuronCore::MakePerspectiveView({0.0f, 0.0f, -DISTANCE}, {0.0f, 0.0f, 1.0f}, WORLD_UP, fovYRadians, TEST_NEAR_PLANE, WIDTH_PIXELS,
                                         HEIGHT_PIXELS);
}

// A camera a third of a voxel off the face of _ship's voxel nearest the origin that looks most towards it, looking along
// the face, so that the boxes beside the eye cross the near plane: the case the station's "grazing the south wall"
// covers for an aligned model, here for an oriented one.
[[nodiscard]] NeuronCore::PerspectiveView GrazingTheShip(const ThreeModels& _three, const Placement& _ship)
{
  float nearest = std::numeric_limits<float>::infinity();
  NeuronCore::Box chosen{};
  for (std::uint32_t i = 0; i < _ship.recordCount; ++i)
  {
    const NeuronCore::Box box = NeuronCore::PlacedVoxelBox(_ship, i, _three.records[_ship.firstRecord + i]);
    const float distance = NeuronCore::Length(box.center);
    if (distance < nearest)
    {
      nearest = distance;
      chosen = box;
    }
  }
  const Float3 toEye = NeuronCore::Normalize(-chosen.center);
  Float3 normal = chosen.axisX;
  for (const Float3 axis : {chosen.axisY, chosen.axisZ})
  {
    if (std::abs(NeuronCore::Dot(axis, toEye)) > std::abs(NeuronCore::Dot(normal, toEye)))
    {
      normal = axis;
    }
  }
  normal = NeuronCore::Dot(normal, toEye) < 0.0f ? -normal : normal;
  const Float3 along = NeuronCore::Normalize(NeuronCore::Cross(normal, WORLD_UP));
  const Float3 eye = chosen.center + normal * 0.85f + along * 0.3f;
  return NeuronCore::MakePerspectiveView(eye, eye + along * 10.0f - normal * 0.5f, WORLD_UP, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE,
                                         WIDTH_PIXELS, HEIGHT_PIXELS);
}

// The sun's view of the placements' spheres: the station's sun, over a square that holds them all.
[[nodiscard]] NeuronCore::OrthographicView SunOver(const std::vector<Placement>& _placements, std::uint32_t _pixels)
{
  constexpr float NONE = std::numeric_limits<float>::infinity();
  Float3 lower{NONE, NONE, NONE};
  Float3 upper{-NONE, -NONE, -NONE};
  for (const Placement& placement : _placements)
  {
    const NeuronCore::Sphere sphere = NeuronCore::PlacementSphere(placement);
    const Float3 reach{sphere.radius, sphere.radius, sphere.radius};
    const Float3 low = sphere.center - reach;
    const Float3 high = sphere.center + reach;
    lower = {std::min(lower.x, low.x), std::min(lower.y, low.y), std::min(lower.z, low.z)};
    upper = {std::max(upper.x, high.x), std::max(upper.y, high.y), std::max(upper.z, high.z)};
  }
  const Float3 toSun = NeuronCore::SunDirection(50.0f * RADIANS_PER_DEGREE, 50.0f * RADIANS_PER_DEGREE);
  const float halfExtent = 0.5f * NeuronCore::Length(upper - lower);
  return NeuronCore::MakeShadowView(toSun, (lower + upper) * 0.5f, halfExtent, lower, upper, _pixels);
}

struct Comparison
{
  std::uint32_t hits = 0;
  std::uint32_t misses = 0;
  std::uint32_t edgeMismatches = 0;
  std::vector<std::wstring> failures;
};

void Report(const std::wstring& _image, const Comparison& _comparison, std::uint32_t _edgeLimit)
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
  Assert::IsTrue(_comparison.edgeMismatches <= _edgeLimit,
                 std::format(L"{}: {} mismatches on an edge, more than {}", _image, _comparison.edgeMismatches, _edgeLimit).c_str());
}

// The twin's box for every id, exact and grown and shrunk by EDGE_EPSILON, for the edge rule.
struct IdBoxes
{
  std::vector<NeuronCore::Box> exact;
  std::vector<NeuronCore::Box> grown;
  std::vector<NeuronCore::Box> shrunk;
};

[[nodiscard]] IdBoxes BoxesOf(const ThreeModels& _three, const std::vector<Placement>& _placements)
{
  return {PlacedBoxes(_three.records, _placements, 0.0f), PlacedBoxes(_three.records, _placements, EDGE_EPSILON),
          PlacedBoxes(_three.records, _placements, -EDGE_EPSILON)};
}

// Whether _ray meets _box beyond _minDistance.
[[nodiscard]] bool Meets(const NeuronCore::Ray& _ray, const NeuronCore::Box& _box, float _minDistance) noexcept
{
  float distance = 0.0f;
  Float3 normal{};
  return NeuronCore::IntersectBox<true, false>(_box, _ray.origin, _ray.direction, NeuronCore::InverseDirection(_ray), distance, normal) &&
         distance >= _minDistance;
}

// Whether the point _distance along _ray lies within EDGE_EPSILON of an edge of _box, where two of its faces meet.
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

// §14's rule against the scene tracer (Design/Archive/SpaceScene.md §15). Away from edges the GPU must find the tracer's voxel,
// at its depth and with its normal, to rounding. Near an edge it may find another face of the same voxel, or another
// voxel: one whose grown box the ray meets, where the tracer's voxel, if any, is missed by its shrunk box.
[[nodiscard]] Comparison CompareWithTracer(const NeuronCore::PerspectiveView& _view, const NeuronCore::SceneTracer& _tracer,
                                           const IdBoxes& _boxes, const SplatImage& _image)
{
  Comparison result;
  for (std::uint32_t y = 0; y < _view.heightPixels; ++y)
  {
    for (std::uint32_t x = 0; x < _view.widthPixels; ++x)
    {
      const std::size_t pixel = static_cast<std::size_t>(y) * _view.widthPixels + x;
      const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(_view, x, y);
      const NeuronCore::TraceHit expected = _tracer.Trace(ray, _view.nearPlane);
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
        const float difference = NeuronCore::MaxComponent(NeuronCore::Abs(normal - expected.normal));
        if (difference > PLACED_NORMAL_TOLERANCE)
        {
          if (NearAnEdge(ray, expected.distance, _boxes.exact[voxel]))
          {
            ++result.edgeMismatches;
            continue;
          }
          result.failures.push_back(std::format(L"({}, {}): voxel {} with normal ({}, {}, {}), the tracer's ({}, {}, {})", x, y, voxel,
                                                normal.x, normal.y, normal.z, expected.normal.x, expected.normal.y, expected.normal.z));
        }
        const float expectedDepth = NeuronCore::PerspectiveDepth(_view, expected.distance);
        if (std::abs(depth - expectedDepth) > PLACED_DEPTH_TOLERANCE * expectedDepth)
        {
          result.failures.push_back(std::format(L"({}, {}): voxel {} at depth {}, the tracer's {}", x, y, voxel, depth, expectedDepth));
        }
        continue;
      }
      const bool gpuNear =
        voxel == NeuronCore::NO_VOXEL || (voxel < _boxes.grown.size() && Meets(ray, _boxes.grown[voxel], _view.nearPlane));
      const bool tracerGrazed = expected.voxel == NeuronCore::NO_VOXEL || !Meets(ray, _boxes.shrunk[expected.voxel], _view.nearPlane);
      if (gpuNear && tracerGrazed)
      {
        ++result.edgeMismatches;
        continue;
      }
      result.failures.push_back(std::format(L"({}, {}): the tracer finds voxel {}, the GPU {}", x, y, expected.voxel, voxel));
    }
  }
  return result;
}

[[nodiscard]] float ShadowDepth(const NeuronCore::OrthographicView& _view, const NeuronCore::TraceHit& _hit) noexcept
{
  return _hit.voxel == NeuronCore::NO_VOXEL ? NeuronCore::ORTHOGRAPHIC_FAR_DEPTH : NeuronCore::OrthographicDepth(_view, _hit.distance);
}

// The shadow map's rule against the scene tracer: its depth to rounding, or, near an edge, a depth between the answers
// for grown and shrunk boxes.
[[nodiscard]] Comparison CompareShadowWithTracer(const NeuronCore::OrthographicView& _view, const NeuronCore::SceneTracer& _tracer,
                                                 const IdBoxes& _boxes, const std::vector<float>& _depth)
{
  Comparison result;
  for (std::uint32_t y = 0; y < _view.heightPixels; ++y)
  {
    for (std::uint32_t x = 0; x < _view.widthPixels; ++x)
    {
      const NeuronCore::Ray ray = NeuronCore::OrthographicRay(_view, x, y);
      const NeuronCore::TraceHit hit = _tracer.Trace(ray, 0.0f);
      const float expected = ShadowDepth(_view, hit);
      const float actual = _depth[static_cast<std::size_t>(y) * _view.widthPixels + x];
      ++(hit.voxel == NeuronCore::NO_VOXEL ? result.misses : result.hits);
      if (std::abs(actual - expected) <= PLACED_DEPTH_TOLERANCE * std::max(expected, 1.0e-3f))
      {
        continue;
      }
      const float nearest = ShadowDepth(_view, NeuronCore::TraceBoxes<true>(_boxes.grown, ray, 0.0f));
      const float farthest = ShadowDepth(_view, NeuronCore::TraceBoxes<true>(_boxes.shrunk, ray, 0.0f));
      if (actual >= nearest - PLACED_DEPTH_TOLERANCE && actual <= farthest + PLACED_DEPTH_TOLERANCE)
      {
        ++result.edgeMismatches;
        continue;
      }
      result.failures.push_back(
        std::format(L"({}, {}): depth {}, the tracer's {} (between {} and {} near an edge)", x, y, actual, expected, nearest, farthest));
    }
  }
  return result;
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

// The same for two shadow maps.
[[nodiscard]] Comparison CompareMaps(const std::vector<float>& _expected, const std::vector<float>& _actual)
{
  Comparison result;
  for (std::size_t texel = 0; texel < _expected.size(); ++texel)
  {
    ++(_expected[texel] == NeuronCore::ORTHOGRAPHIC_FAR_DEPTH ? result.misses : result.hits);
    result.edgeMismatches += std::abs(_expected[texel] - _actual[texel]) <= 1.0e-6f ? 0u : 1u;
  }
  return result;
}

[[nodiscard]] bool Close(float _expected, float _actual) noexcept
{
  return std::abs(_actual - _expected) <= HDR_RELATIVE_TOLERANCE * std::abs(_expected) + HDR_ABSOLUTE_TOLERANCE;
}

} // namespace

// Design/Archive/SpaceScene.md §15's placement tests on WARP: several placements of the three models against the scene tracer,
// near and far and across the near plane; the two permutations drawing the same where both may; the measurement variants
// on turned placements; and the lighting through each model's palette.
TEST_CLASS(PlacementSplatTests)
{
public:
  // Aligned and rigid placements of all three models, against the scene tracer: id, normal and depth in every pixel, from
  // the origin, grazing a turned ship's hull and from 10,000 units back; and the sun's map of them.
  TEST_METHOD(SeveralPlacementsMatchTheSceneTracer)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const ThreeModels three = LoadThreeModels();
        const NeuronClient::VoxelScene scene(_device, three.models);
        const std::vector<Placement> placements = SeveralPlacements(three);
        const NeuronCore::SceneTracer tracer(three.models, placements);
        const IdBoxes boxes = BoxesOf(three, placements);
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::View);
        struct Camera
        {
          const wchar_t* name;
          NeuronCore::PerspectiveView view;
        };
        const std::array<Camera, 3> cameras{{{L"from the origin", FromTheOrigin()},
                                             {L"grazing a turned ship", GrazingTheShip(three, placements[1])},
                                             {L"from 10,000 units", FromFarAway()}}};
        for (const Camera& camera : cameras)
        {
          Report(camera.name, CompareWithTracer(camera.view, tracer, boxes, RenderSplat(_device, scene, placements, pass, camera.view)),
                 EDGE_MISMATCH_LIMIT);
        }

        const NeuronClient::SplatPass shadowPass(_device, NeuronClient::SplatPass::Kind::Shadow);
        const NeuronCore::OrthographicView sun = SunOver(placements, MAP_PIXELS);
        Report(L"the sun's map",
               CompareShadowWithTracer(sun, tracer, boxes, RenderShadowSplat(_device, scene, placements, shadowPass, sun)),
               EDGE_MISMATCH_LIMIT);
      });
  }

  // §7.2, §15: a whole placement turned by a symmetry of the cube draws the same through the oriented permutation as
  // through the aligned one, in the view and in the sun's map.
  TEST_METHOD(SymmetricPlacementsDrawTheSameEitherWay)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const ThreeModels three = LoadThreeModels();
        const NeuronClient::VoxelScene scene(_device, three.models);
        std::vector<Placement> placements{Place(three, STATION, QUARTER_TURN_ABOUT_Y, {0.0f, 0.0f, 700.0f}),
                                          Place(three, CAPITAL_SHIP, HALF_TURN_ABOUT_X, {120.0f, -60.0f, 420.0f}),
                                          Place(three, FRIGATE, NeuronCore::IDENTITY_ROTATION, {-30.0f, 70.0f, 380.0f})};
        Assert::IsTrue(NeuronCore::AssignVoxelIds(placements));
        for (const Placement& placement : placements)
        {
          Assert::IsTrue(NeuronCore::IsAlignedPlacement(placement), L"each draws aligned as it is placed");
        }
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronCore::PerspectiveView view = FromTheOrigin();
        Report(L"symmetries, the view",
               CompareDrawings(RenderSplat(_device, scene, placements, pass, view),
                               RenderSplat(_device, scene, placements, pass, view, Permutations::AllOriented)),
               EDGE_MISMATCH_LIMIT);
        const NeuronClient::SplatPass shadowPass(_device, NeuronClient::SplatPass::Kind::Shadow);
        const NeuronCore::OrthographicView sun = SunOver(placements, MAP_PIXELS);
        Report(L"symmetries, the sun's map",
               CompareMaps(RenderShadowSplat(_device, scene, placements, shadowPass, sun),
                           RenderShadowSplat(_device, scene, placements, shadowPass, sun, Permutations::AllOriented)),
               EDGE_MISMATCH_LIMIT);
      });
  }

  // §7.7, §15: a placement detonated at time 0 draws what the whole one draws, aligned or turned.
  TEST_METHOD(DetonatedAtTimeZeroDrawsTheWholeImage)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const ThreeModels three = LoadThreeModels();
        const NeuronClient::VoxelScene scene(_device, three.models);
        const std::vector<Placement> whole = SeveralPlacements(three);
        const std::vector<Placement> detonated =
          DetonatePlacements(whole, NeuronCore::DefaultExplosionParameters({0.0f, 0.0f, 400.0f}), 0.0f);
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronCore::PerspectiveView view = FromTheOrigin();
        Report(L"detonated at time 0, the view",
               CompareDrawings(RenderSplat(_device, scene, whole, pass, view), RenderSplat(_device, scene, detonated, pass, view)),
               EDGE_MISMATCH_LIMIT);
        const NeuronClient::SplatPass shadowPass(_device, NeuronClient::SplatPass::Kind::Shadow);
        const NeuronCore::OrthographicView sun = SunOver(whole, MAP_PIXELS);
        Report(L"detonated at time 0, the sun's map",
               CompareMaps(RenderShadowSplat(_device, scene, whole, shadowPass, sun),
                           RenderShadowSplat(_device, scene, detonated, shadowPass, sun)),
               EDGE_MISMATCH_LIMIT);
      });
  }

  // §15: the view splat's measurement variants (§9.3, §11) draw what the standard pass draws, for aligned and turned
  // placements, whole and detonated.
  TEST_METHOD(MeasurementVariantsDrawWhatTheStandardDraws)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const ThreeModels three = LoadThreeModels();
        const NeuronClient::VoxelScene scene(_device, three.models);
        const std::vector<Placement> whole = SeveralPlacements(three);
        const NeuronClient::SplatPass standard(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronClient::SplatPass plainDepth(_device, NeuronClient::SplatPass::Kind::View,
                                                 NeuronClient::SplatPass::Variant::PlainDepth);
        const NeuronClient::SplatPass overdraw(_device, NeuronClient::SplatPass::Kind::View, NeuronClient::SplatPass::Variant::Overdraw);
        const NeuronCore::PerspectiveView view = FromTheOrigin();
        NeuronCore::ExplosionParameters explosion = NeuronCore::DefaultExplosionParameters({0.0f, 0.0f, 400.0f});
        explosion.launchSpeed = 40.0f;
        explosion.inheritedVelocity = {5.0f, 0.0f, -3.0f};
        explosion.seed = 7u;
        for (const float time : {0.0f, 0.5f})
        {
          const std::vector<Placement> placements = time > 0.0f ? DetonatePlacements(whole, explosion, time) : whole;
          const SplatImage image = RenderSplat(_device, scene, placements, standard, view);
          Report(std::format(L"plain depth at {} s", time),
                 CompareDrawings(image, RenderSplat(_device, scene, placements, plainDepth, view)), EDGE_MISMATCH_LIMIT);
          Report(std::format(L"overdraw at {} s", time), CompareDrawings(image, RenderSplat(_device, scene, placements, overdraw, view)),
                 EDGE_MISMATCH_LIMIT);
        }
      });
  }

  // §7.1, §15: each model lights with its own palette. In one image, the station's white glows and the capital ship's
  // does not (Design/Archive/SpaceScene.md §4), and every pixel is what the lighting twin makes of the voxel the view splat
  // found, through its placement's palette.
  TEST_METHOD(LightsThroughEachModelsPalette)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const ThreeModels three = LoadThreeModels();
        const NeuronClient::VoxelScene scene(_device, three.models);
        const NeuronCore::Int3 origin = three.models[STATION].instances.front().origin;
        std::vector<Placement> placements{
          NeuronCore::PlacePart(
            three.models[STATION], STATION, three.firstRecords[STATION], 0,
            {NeuronCore::IDENTITY_ROTATION, {static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z)}}),
          Place(three, CAPITAL_SHIP, NeuronCore::IDENTITY_ROTATION, {-160.0f, 200.0f, -160.0f})};
        Assert::IsTrue(NeuronCore::AssignVoxelIds(placements));
        Assert::IsTrue(scene.PaletteValues(STATION).materials[WHITE].emissiveScale > 0.0f, L"the station's white glows");
        Assert::AreEqual(0.0f, scene.PaletteValues(CAPITAL_SHIP).materials[WHITE].emissiveScale, L"the capital ship's does not");

        // From above and to the side of the ship's white, with the station behind it: a view found by tracing candidates
        // on the CPU, which shows both whites.
        const NeuronCore::PerspectiveView view =
          NeuronCore::MakePerspectiveView({-251.4f, 257.7f, -72.0f}, {-111.9f, 180.5f, -113.9f}, WORLD_UP, TEST_FOV_Y_RADIANS,
                                          TEST_NEAR_PLANE, WIDTH_PIXELS, HEIGHT_PIXELS);
        const NeuronCore::LightingParameters parameters = NeuronCore::MakeLightingParameters(TestWorld(), 1.5f);
        const NeuronCore::OrthographicView shadowView = SunOver(placements, LIGHTING_MAP_PIXELS);

        const NeuronClient::SplatPass viewSplat(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronClient::SplatPass shadowSplat(_device, NeuronClient::SplatPass::Kind::Shadow);
        const NeuronClient::LightingPass lighting(_device);
        NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false, L"Test render target views");
        NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 3, false, L"Test depth stencil views");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 12, true, L"Test shader views");
        NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false, L"Test CPU-only views");
        NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
        targets.Resize(_device, view.widthPixels, view.heightPixels);
        const NeuronClient::ShadowMap shadowMap(_device, dsvHeap, shaderHeap, LIGHTING_MAP_PIXELS);
        NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Test constants");
        const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeViewConstants(view));
        const D3D12_GPU_VIRTUAL_ADDRESS shadowViewConstants = constants.Push(NeuronClient::MakeShadowViewConstants(shadowView));
        const D3D12_GPU_VIRTUAL_ADDRESS lightingConstants =
          constants.Push(NeuronClient::MakeLightingConstants(parameters, shadowView, static_cast<std::uint32_t>(placements.size())));
        const NeuronClient::SplatPlacements pushed = PushTestPlacements(constants, placements);

        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            shadowMap.BeginSplat(_list);
            shadowSplat.Record(_list, scene, shadowViewConstants, pushed.constants, pushed.draws);
            shadowMap.EndSplat(_list);
            targets.BeginSplat(_list);
            viewSplat.Record(_list, scene, viewConstants, pushed.constants, pushed.draws);
            targets.EndSplat(_list);
            targets.BeginLighting(_list);
            lighting.Record(_list, targets, shadowMap, scene, viewConstants, shadowViewConstants, lightingConstants, pushed.constants);
            targets.EndLighting(_list);
          });

        const std::size_t pixels = static_cast<std::size_t>(view.widthPixels) * view.heightPixels;
        std::vector<std::uint32_t> visibility(pixels * 2);
        const std::vector<std::byte> visibilityBytes = NeuronClient::ReadTexture2D(
          _device, targets.Visibility(), NeuronClient::ViewTargets::READABLE, NeuronClient::ViewTargets::VISIBILITY_BYTES_PER_PIXEL);
        std::memcpy(visibility.data(), visibilityBytes.data(), visibilityBytes.size());
        std::vector<float> depth(pixels);
        const std::vector<std::byte> depthBytes =
          NeuronClient::ReadTexture2D(_device, targets.Depth(), NeuronClient::ViewTargets::READABLE, sizeof(float));
        std::memcpy(depth.data(), depthBytes.data(), depthBytes.size());
        std::vector<float> shadowDepth(static_cast<std::size_t>(LIGHTING_MAP_PIXELS) * LIGHTING_MAP_PIXELS);
        const std::vector<std::byte> shadowBytes = NeuronClient::ReadTexture2D(
          _device, shadowMap.Depth(), NeuronClient::ShadowMap::READABLE, NeuronClient::ShadowMap::BYTES_PER_TEXEL);
        std::memcpy(shadowDepth.data(), shadowBytes.data(), shadowBytes.size());
        std::vector<std::uint16_t> hdr(pixels * 4);
        const std::vector<std::byte> hdrBytes = NeuronClient::ReadTexture2D(
          _device, targets.HdrColor(), NeuronClient::ViewTargets::READABLE, NeuronClient::ViewTargets::HDR_BYTES_PER_PIXEL);
        std::memcpy(hdr.data(), hdrBytes.data(), hdrBytes.size());

        const NeuronCore::ShadowMapImage shadowImage{LIGHTING_MAP_PIXELS, LIGHTING_MAP_PIXELS, shadowDepth};
        std::array<std::uint32_t, 2> whitePixels{};
        std::array<std::uint32_t, 2> modelPixels{};
        std::uint32_t flips = 0;
        std::vector<std::wstring> flipped;
        for (std::uint32_t y = 0; y < view.heightPixels; ++y)
        {
          for (std::uint32_t x = 0; x < view.widthPixels; ++x)
          {
            const std::size_t pixel = static_cast<std::size_t>(y) * view.widthPixels + x;
            const std::uint32_t voxel = visibility[2 * pixel];
            Float3 albedo{};
            float emissiveScale = 0.0f;
            if (const std::optional<NeuronCore::PlacedVoxel> placed = NeuronCore::FindVoxel(placements, voxel))
            {
              const std::uint32_t color = NeuronCore::UnpackVoxelRecord(three.records[placed->record]).color;
              const NeuronClient::PaletteMaterial& material = scene.PaletteValues(placed->paletteIndex).materials[color];
              albedo = material.albedo;
              emissiveScale = material.emissiveScale;
              ++modelPixels[placed->paletteIndex];
              whitePixels[placed->paletteIndex] += color == WHITE ? 1u : 0u;
            }
            const Float3 expected = NeuronCore::LightPixel(view, x, y, voxel, NeuronCore::UnpackOctahedralNormal(visibility[2 * pixel + 1]),
                                                           depth[pixel], albedo, emissiveScale, shadowImage, shadowView, parameters);
            const Float3 actual{NeuronCore::HalfToFloat(hdr[4 * pixel]), NeuronCore::HalfToFloat(hdr[4 * pixel + 1]),
                                NeuronCore::HalfToFloat(hdr[4 * pixel + 2])};
            if (!Close(expected.x, actual.x) || !Close(expected.y, actual.y) || !Close(expected.z, actual.z))
            {
              ++flips;
              flipped.push_back(std::format(L"({}, {}): voxel {}, ({}, {}, {}), the twin's ({}, {}, {})", x, y, voxel, actual.x, actual.y,
                                            actual.z, expected.x, expected.y, expected.z));
            }
          }
        }
        Logger::WriteMessage(std::format(L"station {} pixels, {} of them white; capital ship {} pixels, {} of them white; {} beyond the "
                                         L"tolerance\n",
                                         modelPixels[STATION], whitePixels[STATION], modelPixels[CAPITAL_SHIP], whitePixels[CAPITAL_SHIP],
                                         flips)
                               .c_str());
        for (std::size_t i = 0; i < std::min<std::size_t>(flipped.size(), 10); ++i)
        {
          Logger::WriteMessage((flipped[i] + L"\n").c_str());
        }
        Assert::IsTrue(whitePixels[STATION] > 0 && whitePixels[CAPITAL_SHIP] > 0, L"the image shows both whites");
        Assert::IsTrue(flips <= SHADOW_FLIP_LIMIT,
                       std::format(L"{} pixels disagree with the lighting twin, more than {}", flips, SHADOW_FLIP_LIMIT).c_str());
      });
  }
};

} // namespace NeuronClientTests
