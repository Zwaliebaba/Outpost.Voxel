#include "pch.h"

#include "GraphicsDevice.h"
#include "SplatPass.h"
#include "TestSupport.h"
#include "VoxelScene.h"

#include "Box.h"
#include "Float3.h"
#include "OctahedralNormal.h"
#include "PerspectiveView.h"
#include "Ray.h"
#include "TraceHit.h"
#include "VoxModel.h"
#include "VoxelGrid.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float3;

// The odd size gives a level camera a whole row and column of rays with exactly-zero components (§4.2, item 6).
constexpr std::uint32_t WIDTH_PIXELS = 161;
constexpr std::uint32_t HEIGHT_PIXELS = 91;

// How close to the edge of a face a ray may land and still be answered differently by the GPU and the tracer, in voxel
// units. The two agree to rounding, not bit for bit (§14), and a ray that grazes an edge can land on either side of it.
// A pixel here spans several voxel units or more, so this is a sliver of it.
constexpr float EDGE_EPSILON = 1.0f / 256.0f;

// Mismatches on an edge allowed per image, which §14 sets from the first measured run and gives a reason for. That run,
// on WARP in CI on 2026-09-27, found none in any image. The bound is not zero because §14 assumes no bit equality between
// CPU and GPU: an MSVC or WARP update that contracts or rounds differently can move a ray that lands within rounding of
// an edge to its other side. Such a ray is rare, so four is headroom; the seams themselves are checked exactly by
// SeamsLetNoBackgroundThrough.
constexpr std::uint32_t EDGE_MISMATCH_LIMIT = 4;

constexpr Float3 WORLD_UP{0.0f, 0.0f, 1.0f};

struct Comparison
{
  std::uint32_t hitPixels = 0;
  std::uint32_t backgroundPixels = 0;
  std::uint32_t edgeMismatches = 0;
  std::vector<std::wstring> failures;
};

[[nodiscard]] bool HitsCube(const NeuronCore::Ray& _ray, Float3 _center, float _radius) noexcept
{
  const NeuronCore::Box box = NeuronCore::MakeAxisAlignedBox(_center, {_radius, _radius, _radius});
  float distance = 0.0f;
  Float3 normal{};
  return NeuronCore::IntersectBox<false, false>(box, _ray.origin, _ray.direction, NeuronCore::InverseDirection(_ray), distance, normal);
}

// Where _hit lands on its face lies within EDGE_EPSILON of the face's boundary.
[[nodiscard]] bool LandsOnAnEdge(const NeuronCore::Ray& _ray, const NeuronCore::TraceHit& _hit, const NeuronCore::Box& _box) noexcept
{
  const Float3 offset = NeuronCore::Abs(_ray.origin + _ray.direction * _hit.distance - _box.center);
  const Float3 limit = _box.radius - Float3{EDGE_EPSILON, EDGE_EPSILON, EDGE_EPSILON};
  if (_hit.normal.x != 0.0f)
  {
    return offset.y >= limit.y || offset.z >= limit.z;
  }
  if (_hit.normal.y != 0.0f)
  {
    return offset.x >= limit.x || offset.z >= limit.z;
  }
  return offset.x >= limit.x || offset.y >= limit.y;
}

// The per-pixel comparison rule of §14: away from edges any mismatch fails; on an edge, where the tracer's hit lands
// within EDGE_EPSILON of its face's boundary or the GPU's grazes its box, a mismatch is counted against the limit.
[[nodiscard]] Comparison Compare(const NeuronCore::PerspectiveView& _view, const NeuronCore::VoxModel& _model,
                                 const NeuronCore::VoxelGrid& _grid, const SplatImage& _image)
{
  Comparison result;
  for (std::uint32_t y = 0; y < _view.heightPixels; ++y)
  {
    for (std::uint32_t x = 0; x < _view.widthPixels; ++x)
    {
      const std::size_t pixel = static_cast<std::size_t>(y) * _view.widthPixels + x;
      const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(_view, x, y);
      const NeuronCore::TraceHit expected = _grid.Trace(ray, _view.nearPlane);
      const std::uint32_t voxel = _image.visibility[2 * pixel];
      const float depth = _image.depth[pixel];
      if (voxel == expected.voxel && voxel == NeuronCore::NO_VOXEL)
      {
        ++result.backgroundPixels;
        if (depth != NeuronCore::PERSPECTIVE_FAR_DEPTH)
        {
          result.failures.push_back(std::format(L"({}, {}): no voxel, yet depth {}", x, y, depth));
        }
        continue;
      }
      if (voxel == expected.voxel)
      {
        ++result.hitPixels;
        const Float3 normal = NeuronCore::UnpackOctahedralNormal(_image.visibility[2 * pixel + 1]);
        if (normal.x != expected.normal.x || normal.y != expected.normal.y || normal.z != expected.normal.z)
        {
          result.failures.push_back(std::format(L"({}, {}): voxel {} with normal ({}, {}, {}), the tracer's ({}, {}, {})", x, y, voxel,
                                                normal.x, normal.y, normal.z, expected.normal.x, expected.normal.y, expected.normal.z));
        }
        const float expectedDepth = NeuronCore::PerspectiveDepth(_view, expected.distance);
        if (std::abs(depth - expectedDepth) > 1.0e-5f * expectedDepth)
        {
          result.failures.push_back(std::format(L"({}, {}): voxel {} at depth {}, the tracer's {}", x, y, voxel, depth, expectedDepth));
        }
        continue;
      }
      const bool tracerOnEdge = expected.voxel != NeuronCore::NO_VOXEL && LandsOnAnEdge(ray, expected, RecordBox(_model, expected.voxel));
      const bool gpuNear = voxel == NeuronCore::NO_VOXEL || HitsCube(ray, RecordBox(_model, voxel).center, 0.5f + EDGE_EPSILON);
      const bool gpuGrazes =
        voxel != NeuronCore::NO_VOXEL && gpuNear && !HitsCube(ray, RecordBox(_model, voxel).center, 0.5f - EDGE_EPSILON);
      if ((tracerOnEdge && gpuNear) || (expected.voxel == NeuronCore::NO_VOXEL && gpuGrazes))
      {
        ++result.edgeMismatches;
        continue;
      }
      result.failures.push_back(std::format(L"({}, {}): the tracer finds voxel {}, the GPU {}", x, y, expected.voxel, voxel));
    }
  }
  return result;
}

void Report(const wchar_t* _camera, const Comparison& _comparison)
{
  Logger::WriteMessage(std::format(L"{}: {} hits, {} background, {} mismatches on an edge, {} failures\n", _camera, _comparison.hitPixels,
                                   _comparison.backgroundPixels, _comparison.edgeMismatches, _comparison.failures.size())
                         .c_str());
  for (std::size_t i = 0; i < std::min<std::size_t>(_comparison.failures.size(), 10); ++i)
  {
    Logger::WriteMessage((_comparison.failures[i] + L"\n").c_str());
  }
  Assert::IsTrue(_comparison.failures.empty(),
                 std::format(L"{}: {} pixels disagree with the tracer", _camera, _comparison.failures.size()).c_str());
  Assert::IsTrue(
    _comparison.edgeMismatches <= EDGE_MISMATCH_LIMIT,
    std::format(L"{}: {} mismatches on an edge, more than {}", _camera, _comparison.edgeMismatches, EDGE_MISMATCH_LIMIT).c_str());
}

// The centre of the voxel nearest the south of the station, and among those the one nearest (0, *, 60): where a close
// camera finds a wall to look at.
[[nodiscard]] Float3 SouthernmostVoxel(const NeuronCore::VoxModel& _model)
{
  Float3 best{};
  float bestY = std::numeric_limits<float>::infinity();
  float bestOffset = std::numeric_limits<float>::infinity();
  for (std::uint32_t record = 0; record < _model.records.size(); ++record)
  {
    const Float3 center = RecordBox(_model, record).center;
    const float offset = std::abs(center.x) + std::abs(center.z - 60.0f);
    if (center.y < bestY || (center.y == bestY && offset < bestOffset))
    {
      best = center;
      bestY = center.y;
      bestOffset = offset;
    }
  }
  return best;
}

[[nodiscard]] NeuronCore::PerspectiveView TestView(Float3 _eye, Float3 _target, std::uint32_t _widthPixels,
                                                   std::uint32_t _heightPixels) noexcept
{
  return NeuronCore::MakePerspectiveView(_eye, _target, WORLD_UP, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE, _widthPixels, _heightPixels);
}

// An upright square wall of _side × _side voxels in the plane y = 0, records row after row with x fastest.
[[nodiscard]] NeuronCore::VoxModel Wall(std::uint32_t _side)
{
  NeuronCore::VoxModel model{};
  model.version = 150;
  for (std::uint32_t z = 0; z < _side; ++z)
  {
    for (std::uint32_t x = 0; x < _side; ++x)
    {
      model.records.push_back(NeuronCore::PackVoxelRecord({static_cast<std::uint8_t>(x), 0, static_cast<std::uint8_t>(z),
                                                           static_cast<std::uint8_t>((x + z) % NeuronCore::PALETTE_ENTRY_COUNT)}));
    }
  }
  const auto side = static_cast<std::int32_t>(_side);
  model.instances.push_back({{0, 0, 0}, {side, 1, side}, 0, static_cast<std::uint32_t>(model.records.size())});
  for (NeuronCore::PaletteEntry& entry : model.palette)
  {
    entry = {128, 128, 128, 255, false, 0.0f, 0.0f};
  }
  return model;
}

} // namespace

// The view splat against the reference tracer, pixel for pixel (Design/SampleRenderer.md §14).
TEST_CLASS(ViewSplatTests)
{
public:
  TEST_METHOD(StationMatchesTheTracer)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronCore::VoxelGrid grid(model);
        const NeuronClient::VoxelScene scene(_device, model);
        // The measurement variants (§9.3, §11) must draw exactly what the standard pass draws.
        const NeuronClient::SplatPass standard(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronClient::SplatPass plainDepth(_device, NeuronClient::SplatPass::Kind::View,
                                                 NeuronClient::SplatPass::Permutation::Aligned,
                                                 NeuronClient::SplatPass::Variant::PlainDepth);
        const NeuronClient::SplatPass overdraw(_device, NeuronClient::SplatPass::Kind::View, NeuronClient::SplatPass::Permutation::Aligned,
                                               NeuronClient::SplatPass::Variant::Overdraw);
        struct Drawing
        {
          const wchar_t* suffix;
          const NeuronClient::SplatPass* pass;
        };
        const std::array<Drawing, 3> drawings{{{L"", &standard}, {L", plain depth", &plainDepth}, {L", overdraw", &overdraw}}};

        const Float3 center{0.5f, 0.5f, 127.5f};
        const Float3 wall = SouthernmostVoxel(model);
        const float face = wall.y - 0.5f;
        struct Camera
        {
          const wchar_t* name;
          Float3 eye;
          Float3 target;
        };
        const std::array<Camera, 5> cameras{{
          // The application's default: elevated three-quarter view at the framing distance (§3).
          {L"three-quarter", center + Float3{-318.43f, -318.43f, 260.0f}, center},
          // Level and axis-aligned, so that the centre row and column have exactly-zero components.
          {L"level from the west", {-519.5f, 0.5f, 127.5f}, center},
          {L"from above", {40.5f, -30.5f, 690.0f}, center},
          // Close enough that voxels span more than 20 pixels and take the precise bounds.
          {L"close to the south wall", {wall.x + 0.3f, face - 3.0f, wall.z + 0.2f}, wall},
          // Along the wall at a grazing angle from just in front of it, so that boxes beside the eye cross the near plane.
          {L"grazing the south wall", {wall.x - 0.4f, face - 0.35f, wall.z + 0.3f}, wall + Float3{10.0f, 0.0f, 0.0f}},
        }};
        for (const Camera& camera : cameras)
        {
          const NeuronCore::PerspectiveView view = TestView(camera.eye, camera.target, WIDTH_PIXELS, HEIGHT_PIXELS);
          for (const Drawing& drawing : drawings)
          {
            Report((std::wstring(camera.name) + drawing.suffix).c_str(),
                   Compare(view, model, grid, RenderSplat(_device, scene, *drawing.pass, view)));
          }
        }
      });
  }

  // §4.2, item 12: head-on from a camera on integer coordinates, the centre column and row of an odd image run exactly
  // along seams between voxels. A strict face test would show the background through them; the watertight one hits both
  // neighbours, and the depth test keeps the lower record, as the tracer does.
  TEST_METHOD(SeamsLetNoBackgroundThrough)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        constexpr std::uint32_t SIDE = 8;
        constexpr std::uint32_t IMAGE_PIXELS = 81;
        const NeuronCore::VoxModel model = Wall(SIDE);
        const NeuronCore::VoxelGrid grid(model);
        const NeuronClient::VoxelScene scene(_device, model);
        const NeuronClient::SplatPass pass(_device, NeuronClient::SplatPass::Kind::View);
        const NeuronCore::PerspectiveView view = TestView({4.0f, -20.0f, 4.0f}, {4.0f, 0.0f, 4.0f}, IMAGE_PIXELS, IMAGE_PIXELS);
        const SplatImage image = RenderSplat(_device, scene, pass, view);
        Report(L"wall", Compare(view, model, grid, image));

        std::uint32_t seamPixels = 0;
        for (std::uint32_t y = 0; y < IMAGE_PIXELS; ++y)
        {
          for (std::uint32_t x = 0; x < IMAGE_PIXELS; ++x)
          {
            // Every ray that meets the wall's plane inside its outline must find a voxel.
            const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(view, x, y);
            const Float3 onWall = ray.origin + ray.direction * (-ray.origin.y / ray.direction.y);
            const bool inside =
              onWall.x > EDGE_EPSILON && onWall.x < SIDE - EDGE_EPSILON && onWall.z > EDGE_EPSILON && onWall.z < SIDE - EDGE_EPSILON;
            const std::uint32_t voxel = image.visibility[2 * (static_cast<std::size_t>(y) * IMAGE_PIXELS + x)];
            if (inside)
            {
              Assert::AreNotEqual(NeuronCore::NO_VOXEL, voxel, std::format(L"({}, {}) sees through the wall", x, y).c_str());
            }
            // On a seam, exactly the lower record: the corner at (4, 4) is record 3 × 8 + 3.
            const std::uint32_t middle = IMAGE_PIXELS / 2;
            if (x == middle || y == middle)
            {
              ++seamPixels;
              const std::uint32_t expected = grid.Trace(ray, view.nearPlane).voxel;
              Assert::AreEqual(expected, voxel, std::format(L"({}, {}) on a seam", x, y).c_str());
            }
          }
        }
        Assert::AreEqual(27u, image.visibility[2 * (static_cast<std::size_t>(IMAGE_PIXELS / 2) * IMAGE_PIXELS + IMAGE_PIXELS / 2)],
                         L"the corner of four voxels keeps the lowest record");
        Logger::WriteMessage(std::format(L"{} pixels on a seam\n", seamPixels).c_str());
      });
  }
};

} // namespace NeuronClientTests
