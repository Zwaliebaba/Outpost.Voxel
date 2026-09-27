#include "pch.h"

#include "Box.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "TraceHit.h"
#include "VoxFile.h"
#include "VoxModel.h"
#include "VoxelGrid.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VoxelCoreTests
{
namespace
{

using VoxelCore::Float3;
using VoxelCore::Int3;

constexpr Int3 MODEL_SIZE{207, 228, 255};
constexpr Int3 MODEL_ORIGIN{-103, -114, 0}; // the translation 0 0 127, minus floor(size / 2)
constexpr std::uint32_t VOXEL_COUNT = 225048;

// _relative in _start or the nearest directory above it that holds it.
[[nodiscard]] std::optional<std::filesystem::path> FindAbove(const std::filesystem::path& _start, const std::filesystem::path& _relative)
{
  for (std::filesystem::path directory = _start; !directory.empty(); directory = directory.parent_path())
  {
    if (std::filesystem::exists(directory / _relative))
    {
      return directory / _relative;
    }
    if (directory == directory.parent_path())
    {
      break;
    }
  }
  return std::nullopt;
}

// Where the tests look for the repository's copy of the asset: above the working directory, which is the repository
// root under CI and the output directory under Test Explorer, then above this source file. A missing asset fails the
// test rather than skipping it.
[[nodiscard]] std::filesystem::path FindMilitaryStation()
{
  const std::filesystem::path relative = std::filesystem::path("GameData") / "MilitaryStation.vox";
  std::optional<std::filesystem::path> found = FindAbove(std::filesystem::current_path(), relative);
  if (!found)
  {
    found = FindAbove(std::filesystem::path(std::source_location::current().file_name()).parent_path(), relative);
  }
  Assert::IsTrue(found.has_value(), L"GameData/MilitaryStation.vox is not above the working directory or the test sources");
  return found.value_or(std::filesystem::path());
}

[[nodiscard]] VoxelCore::VoxModel LoadMilitaryStation()
{
  auto model = VoxelCore::LoadVoxModel(FindMilitaryStation());
  if (!model)
  {
    const std::string_view name = VoxelCore::VoxErrorName(model.error());
    Assert::Fail(std::format(L"MilitaryStation.vox was refused: {}", std::wstring(name.begin(), name.end())).c_str());
  }
  return std::move(*model);
}

void AreEqualInt3(Int3 _expected, Int3 _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _what);
  Assert::AreEqual(_expected.y, _actual.y, _what);
  Assert::AreEqual(_expected.z, _actual.z, _what);
}

[[nodiscard]] const VoxelCore::VoxAttributes& RenderObject(const VoxelCore::VoxModel& _model, std::string_view _type)
{
  const auto found = std::ranges::find_if(_model.renderObjects,
                                          [_type](const VoxelCore::VoxAttributes& _attributes)
                                          {
                                            const auto type = _attributes.find("_type");
                                            return type != _attributes.end() && type->second == _type;
                                          });
  Assert::IsTrue(found != _model.renderObjects.end(), std::format(L"no rOBJ of type {}", std::wstring(_type.begin(), _type.end())).c_str());
  return *found;
}

void ExpectAttribute(const VoxelCore::VoxAttributes& _attributes, std::string_view _key, std::string_view _value)
{
  const auto found = _attributes.find(_key);
  const std::wstring what = std::format(L"{} = {}", std::wstring(_key.begin(), _key.end()), std::wstring(_value.begin(), _value.end()));
  Assert::IsTrue(found != _attributes.end(), what.c_str());
  Assert::AreEqual(std::string(_value), found->second, what.c_str());
}

// Brute force and the grid must agree exactly: they run the same IntersectBox on the same boxes.
void ExpectSameHit(const VoxelCore::TraceHit& _expected, const VoxelCore::TraceHit& _actual, const std::wstring& _ray)
{
  Assert::AreEqual(_expected.voxel, _actual.voxel, _ray.c_str());
  if (_expected.voxel != VoxelCore::NO_VOXEL)
  {
    Assert::AreEqual(_expected.distance, _actual.distance, _ray.c_str());
    Assert::AreEqual(_expected.normal.x, _actual.normal.x, _ray.c_str());
    Assert::AreEqual(_expected.normal.y, _actual.normal.y, _ray.c_str());
    Assert::AreEqual(_expected.normal.z, _actual.normal.z, _ray.c_str());
  }
}

} // namespace

// The figures of Design/SampleRenderer.md §3, measured once by a throwaway script and pinned here in C++.
TEST_CLASS(MilitaryStationTests)
{
public:
  TEST_METHOD(IsOneModelOfTheMeasuredSize)
  {
    Assert::AreEqual(std::uintmax_t{925571}, std::filesystem::file_size(FindMilitaryStation()), L"file size");
    const VoxelCore::VoxModel model = LoadMilitaryStation();
    Assert::AreEqual(200, model.version);
    Assert::AreEqual(std::size_t{1}, model.instances.size());
    const VoxelCore::ModelInstance& instance = model.instances.front();
    AreEqualInt3(MODEL_SIZE, instance.size, L"size");
    AreEqualInt3(MODEL_ORIGIN, instance.origin, L"origin");
    Assert::AreEqual(0u, instance.firstRecord);
    Assert::AreEqual(VOXEL_COUNT, instance.recordCount);
    Assert::AreEqual(std::size_t{VOXEL_COUNT}, model.records.size());
  }

  TEST_METHOD(UsesSevenEntriesInTheMeasuredCounts)
  {
    const VoxelCore::VoxModel model = LoadMilitaryStation();
    std::array<std::uint32_t, VoxelCore::PALETTE_ENTRY_COUNT> counts{};
    Int3 lower{255, 255, 255};
    Int3 upper{0, 0, 0};
    for (const std::uint32_t record : model.records)
    {
      const VoxelCore::VoxelRecord voxel = VoxelCore::UnpackVoxelRecord(record);
      ++counts[voxel.color];
      lower = {std::min<std::int32_t>(lower.x, voxel.x), std::min<std::int32_t>(lower.y, voxel.y),
               std::min<std::int32_t>(lower.z, voxel.z)};
      upper = {std::max<std::int32_t>(upper.x, voxel.x), std::max<std::int32_t>(upper.y, voxel.y),
               std::max<std::int32_t>(upper.z, voxel.z)};
    }

    // By palette entry, 1-16.
    const std::array<std::uint32_t, VoxelCore::PALETTE_ENTRY_COUNT> expected{0,     59013, 0, 0, 0, 0, 0,   68956,
                                                                             84490, 8025,  0, 0, 6, 0, 773, 3785};
    for (std::size_t i = 0; i < counts.size(); ++i)
    {
      Assert::AreEqual(expected[i], counts[i], std::format(L"voxels in entry {}", i + 1).c_str());
    }
    AreEqualInt3({1, 1, 0}, lower, L"lowest occupied position");
    AreEqualInt3({205, 227, 254}, upper, L"highest occupied position");

    std::uint32_t emissive = 0;
    for (std::size_t i = 0; i < counts.size(); ++i)
    {
      emissive += model.palette[i].emissive ? counts[i] : 0u;
    }
    Assert::AreEqual(12583u, emissive, L"emissive voxels");
  }

  TEST_METHOD(HoldsTheEgaPaletteWithThreeEmissiveEntries)
  {
    const VoxelCore::VoxModel model = LoadMilitaryStation();
    for (std::size_t i = 0; i < model.palette.size(); ++i)
    {
      const VoxelCore::PaletteEntry& entry = model.palette[i];
      const std::wstring what = std::format(L"entry {}", i + 1);
      Assert::AreEqual(EGA_PALETTE[i][0], entry.red, what.c_str());
      Assert::AreEqual(EGA_PALETTE[i][1], entry.green, what.c_str());
      Assert::AreEqual(EGA_PALETTE[i][2], entry.blue, what.c_str());
      Assert::AreEqual(EGA_PALETTE[i][3], entry.alpha, what.c_str());

      // Entries 10, 15 and 16 are _emit with _emit 0.6 and _flux 2; the rest carry no _type.
      const bool emissive = i == 9 || i == 14 || i == 15;
      Assert::AreEqual(emissive, entry.emissive, what.c_str());
      Assert::AreEqual(emissive ? 0.6f : 0.0f, entry.emit, what.c_str());
      Assert::AreEqual(emissive ? 2.0f : 0.0f, entry.flux, what.c_str());
    }
  }

  TEST_METHOD(KeepsTheRenderSettings)
  {
    const VoxelCore::VoxModel model = LoadMilitaryStation();
    Assert::AreEqual(std::size_t{15}, model.renderObjects.size());

    const VoxelCore::VoxAttributes& sun = RenderObject(model, "_inf");
    ExpectAttribute(sun, "_i", "0.7");
    ExpectAttribute(sun, "_k", "255 255 255");
    ExpectAttribute(sun, "_angle", "50 50");
    ExpectAttribute(sun, "_area", "0.07");
    const VoxelCore::VoxAttributes& sky = RenderObject(model, "_uni");
    ExpectAttribute(sky, "_i", "0.7");
    ExpectAttribute(sky, "_k", "255 255 255");
    ExpectAttribute(RenderObject(model, "_lens"), "_fov", "45");
    const VoxelCore::VoxAttributes& film = RenderObject(model, "_film");
    ExpectAttribute(film, "_expo", "1");
    ExpectAttribute(film, "_aces", "1");
    ExpectAttribute(film, "_gam", "2.2");
    ExpectAttribute(RenderObject(model, "_ground"), "_color", "80 80 80");
    ExpectAttribute(RenderObject(model, "_bg"), "_color", "0 0 0");
    ExpectAttribute(RenderObject(model, "_ibl"), "_path", "HDR_041_Path_Env.hdr");
    ExpectAttribute(RenderObject(model, "_setting"), "_ground", "1");
  }

  TEST_METHOD(RestsOnTheGround)
  {
    const VoxelCore::VoxModel model = LoadMilitaryStation();
    const VoxelCore::VoxelGrid grid(model);

    // The occupied cells, in world space: the lowest layer sits exactly on z = 0.
    AreEqualInt3({-102, -113, 0}, grid.Origin(), L"grid origin");
    AreEqualInt3({205, 227, 255}, grid.Size(), L"grid size");

    // The default view frames this box's bounding sphere: radius about 199 around (0.5, 0.5, 127.5).
    const Int3 size = grid.Size();
    const double radius = 0.5 * std::hypot(static_cast<double>(size.x), static_cast<double>(size.y), static_cast<double>(size.z));
    Assert::AreEqual(199.1, radius, 0.05, L"bounding sphere radius");
    Assert::AreEqual(0.5, grid.Origin().x + 0.5 * size.x, 0.0, L"centre x");
    Assert::AreEqual(0.5, grid.Origin().y + 0.5 * size.y, 0.0, L"centre y");
    Assert::AreEqual(127.5, grid.Origin().z + 0.5 * size.z, 0.0, L"centre z");
  }

  TEST_METHOD(GridHoldsEveryVoxel)
  {
    const VoxelCore::VoxModel model = LoadMilitaryStation();
    const VoxelCore::VoxelGrid grid(model);
    const VoxelCore::ModelInstance& instance = model.instances.front();
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      const VoxelCore::VoxelRecord voxel = VoxelCore::UnpackVoxelRecord(model.records[i]);
      const Int3 position = instance.origin + Int3{voxel.x, voxel.y, voxel.z};
      if (grid.VoxelAt(position) != i)
      {
        Assert::Fail(std::format(L"record {} is not at {} {} {}", i, position.x, position.y, position.z).c_str());
      }
    }
    Assert::AreEqual(VoxelCore::NO_VOXEL, grid.VoxelAt({0, 0, 300}), L"above the station");
  }

  TEST_METHOD(GridTracesAsBruteForceDoes)
  {
    const VoxelCore::VoxModel model = LoadMilitaryStation();
    const VoxelCore::VoxelGrid grid(model);
    const VoxelCore::ModelInstance& instance = model.instances.front();
    std::vector<VoxelCore::Box> boxes;
    boxes.reserve(model.records.size());
    for (const std::uint32_t record : model.records)
    {
      boxes.push_back(VoxelCore::VoxelBox(instance, record));
    }

    // Three views of the bounding sphere at the design's distance, in the file's 45-degree field of view, and the sun
    // straight overhead, each sampled on a lattice over the station's silhouette. The level view's centre row and column
    // and every ray of the sun have exactly-zero components.
    const Float3 center{0.5f, 0.5f, 127.5f};
    constexpr float DISTANCE = 520.3f;
    const std::array<Float3, 3> offsets{Float3{0.0f, -1.0f, 0.0f}, VoxelCore::Normalize({1.0f, -1.2f, 0.8f}),
                                        VoxelCore::Normalize({1.0f, 0.3f, -0.1f})};
    std::uint32_t hits = 0;
    std::uint32_t rays = 0;
    for (const Float3& offset : offsets)
    {
      const VoxelCore::PerspectiveView view =
        VoxelCore::MakePerspectiveView(center + offset * DISTANCE, center, {0.0f, 0.0f, 1.0f}, 0.785398163f, 0.1f, 161, 91);
      for (const std::uint32_t pixelY : {40u, 45u, 52u, 58u, 66u})
      {
        for (const std::uint32_t pixelX : {70u, 76u, 80u, 86u, 92u})
        {
          const VoxelCore::Ray ray = VoxelCore::PerspectiveRay(view, pixelX, pixelY);
          const VoxelCore::TraceHit expected = VoxelCore::TraceBoxes<false>(boxes, ray, view.nearPlane);
          ExpectSameHit(expected, grid.Trace(ray, view.nearPlane), std::format(L"pixel {} {}", pixelX, pixelY));
          hits += expected.voxel != VoxelCore::NO_VOXEL ? 1u : 0u;
          ++rays;
        }
      }
    }

    const VoxelCore::OrthographicView sun =
      VoxelCore::MakeOrthographicView({0.5f, 0.5f, 300.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, 120.0f, 120.0f, 310.0f, 64, 64);
    for (const std::uint32_t pixelY : {14u, 26u, 34u, 44u, 52u})
    {
      for (const std::uint32_t pixelX : {12u, 24u, 32u, 40u, 52u})
      {
        const VoxelCore::Ray ray = VoxelCore::OrthographicRay(sun, pixelX, pixelY);
        const VoxelCore::TraceHit expected = VoxelCore::TraceBoxes<false>(boxes, ray, 0.0f);
        ExpectSameHit(expected, grid.Trace(ray, 0.0f), std::format(L"sun pixel {} {}", pixelX, pixelY));
        hits += expected.voxel != VoxelCore::NO_VOXEL ? 1u : 0u;
        ++rays;
      }
    }
    Logger::WriteMessage(std::format(L"{} of {} rays hit the station", hits, rays).c_str());
    Assert::IsTrue(2 * hits > rays, L"most rays hit, or the comparison says little");
  }
};

} // namespace VoxelCoreTests
