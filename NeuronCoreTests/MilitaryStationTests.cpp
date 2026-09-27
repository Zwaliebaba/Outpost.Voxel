#include "pch.h"

#include "Box.h"
#include "Explosion.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "RenderSettings.h"
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

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Int3;

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

[[nodiscard]] NeuronCore::VoxModel LoadMilitaryStation()
{
  auto model = NeuronCore::LoadVoxModel(FindMilitaryStation());
  if (!model)
  {
    const std::string_view name = NeuronCore::VoxErrorName(model.error());
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

[[nodiscard]] const NeuronCore::VoxAttributes& RenderObject(const NeuronCore::VoxModel& _model, std::string_view _type)
{
  const auto found = std::ranges::find_if(_model.renderObjects,
                                          [_type](const NeuronCore::VoxAttributes& _attributes)
                                          {
                                            const auto type = _attributes.find("_type");
                                            return type != _attributes.end() && type->second == _type;
                                          });
  Assert::IsTrue(found != _model.renderObjects.end(), std::format(L"no rOBJ of type {}", std::wstring(_type.begin(), _type.end())).c_str());
  return *found;
}

void ExpectAttribute(const NeuronCore::VoxAttributes& _attributes, std::string_view _key, std::string_view _value)
{
  const auto found = _attributes.find(_key);
  const std::wstring what = std::format(L"{} = {}", std::wstring(_key.begin(), _key.end()), std::wstring(_value.begin(), _value.end()));
  Assert::IsTrue(found != _attributes.end(), what.c_str());
  Assert::AreEqual(std::string(_value), found->second, what.c_str());
}

// How far below the ground a corner may seem to be, to rounding.
constexpr float GROUND_TOLERANCE = 1.0e-5f;

// The explosion tests follow every EXPLOSION_STRIDE-th voxel, and every voxel of the layer on the ground, which is
// launched differently (Design/ADR/ADR-009), through this many samples of its flights and this many in its last fall.
constexpr std::uint32_t EXPLOSION_STRIDE = 101;
constexpr std::uint32_t FLIGHT_SAMPLES = 128;
constexpr std::uint32_t LANDING_SAMPLES = 16;

// Every voxel's center while the station is intact, in record order, which is the voxel index the explosion hashes.
[[nodiscard]] std::vector<Float3> RestCenters(const NeuronCore::VoxModel& _model)
{
  std::vector<Float3> centers;
  centers.reserve(_model.records.size());
  for (const NeuronCore::ModelInstance& instance : _model.instances)
  {
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      centers.push_back(NeuronCore::VoxelBox(instance, _model.records[instance.firstRecord + i]).center);
    }
  }
  return centers;
}

[[nodiscard]] std::vector<std::uint32_t> FollowedVoxels(const std::vector<Float3>& _centers)
{
  std::vector<std::uint32_t> voxels;
  for (std::uint32_t voxel = 0; voxel < _centers.size(); ++voxel)
  {
    if (voxel % EXPLOSION_STRIDE == 0u || _centers[voxel].z < 1.0f)
    {
      voxels.push_back(voxel);
    }
  }
  return voxels;
}

// The times a followed voxel is sampled at: through its flights, then closely in the last thousandths before it rests.
[[nodiscard]] std::vector<float> SampleTimes(float _restSeconds)
{
  std::vector<float> times;
  for (std::uint32_t sample = 0; sample <= FLIGHT_SAMPLES; ++sample)
  {
    times.push_back(_restSeconds * static_cast<float>(sample) / static_cast<float>(FLIGHT_SAMPLES));
  }
  for (std::uint32_t sample = 1; sample <= LANDING_SAMPLES; ++sample)
  {
    times.push_back(_restSeconds * (1.0f - 0.0005f * static_cast<float>(sample)));
  }
  return times;
}

// The lowest point of a posed voxel: its center less the rotated unit cube's half-extent along +Z.
[[nodiscard]] float LowestPoint(const NeuronCore::VoxelPose& _pose) noexcept
{
  return _pose.center.z - 0.5f * (std::abs(_pose.axisX.z) + std::abs(_pose.axisY.z) + std::abs(_pose.axisZ.z));
}

[[nodiscard]] bool SamePose(const NeuronCore::VoxelPose& _a, const NeuronCore::VoxelPose& _b) noexcept
{
  const auto same = [](Float3 _u, Float3 _v) { return _u.x == _v.x && _u.y == _v.y && _u.z == _v.z; };
  return same(_a.center, _b.center) && same(_a.axisX, _b.axisX) && same(_a.axisY, _b.axisY) && same(_a.axisZ, _b.axisZ);
}

// Brute force and the grid must agree exactly: they run the same IntersectBox on the same boxes.
void ExpectSameHit(const NeuronCore::TraceHit& _expected, const NeuronCore::TraceHit& _actual, const std::wstring& _ray)
{
  Assert::AreEqual(_expected.voxel, _actual.voxel, _ray.c_str());
  if (_expected.voxel != NeuronCore::NO_VOXEL)
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
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    Assert::AreEqual(200, model.version);
    Assert::AreEqual(std::size_t{1}, model.instances.size());
    const NeuronCore::ModelInstance& instance = model.instances.front();
    AreEqualInt3(MODEL_SIZE, instance.size, L"size");
    AreEqualInt3(MODEL_ORIGIN, instance.origin, L"origin");
    Assert::AreEqual(0u, instance.firstRecord);
    Assert::AreEqual(VOXEL_COUNT, instance.recordCount);
    Assert::AreEqual(std::size_t{VOXEL_COUNT}, model.records.size());
  }

  TEST_METHOD(UsesSevenEntriesInTheMeasuredCounts)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    std::array<std::uint32_t, NeuronCore::PALETTE_ENTRY_COUNT> counts{};
    Int3 lower{255, 255, 255};
    Int3 upper{0, 0, 0};
    for (const std::uint32_t record : model.records)
    {
      const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(record);
      ++counts[voxel.color];
      lower = {std::min<std::int32_t>(lower.x, voxel.x), std::min<std::int32_t>(lower.y, voxel.y),
               std::min<std::int32_t>(lower.z, voxel.z)};
      upper = {std::max<std::int32_t>(upper.x, voxel.x), std::max<std::int32_t>(upper.y, voxel.y),
               std::max<std::int32_t>(upper.z, voxel.z)};
    }

    // By palette entry, 1-16.
    const std::array<std::uint32_t, NeuronCore::PALETTE_ENTRY_COUNT> expected{0,     59013, 0, 0, 0, 0, 0,   68956,
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
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    for (std::size_t i = 0; i < model.palette.size(); ++i)
    {
      const NeuronCore::PaletteEntry& entry = model.palette[i];
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

  // Design/SampleRenderer.md §11: the lighting reads the file's own settings, which the defaults repeat.
  TEST_METHOD(LightsAsItsSettingsSay)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const NeuronCore::RenderSettings settings = NeuronCore::ReadRenderSettings(model.renderObjects);
    const NeuronCore::RenderSettings defaults = NeuronCore::DefaultRenderSettings();
    Assert::AreEqual(defaults.sunElevationRadians, settings.sunElevationRadians, L"_angle 50 50");
    Assert::AreEqual(defaults.sunAzimuthRadians, settings.sunAzimuthRadians);
    Assert::AreEqual(0.7f, settings.sunIntensity, L"_inf _i");
    Assert::AreEqual(0.7f, settings.skyIntensity, L"_uni _i");
    Assert::AreEqual(1.0f, settings.sunColor.x, L"a white sun");
    Assert::AreEqual(1.0f, settings.skyColor.z, L"a white sky");
    Assert::AreEqual(0.0802198203f, settings.groundColor.y, 1.0e-7f, L"_ground 80 80 80, decoded");
    Assert::AreEqual(0.0f, settings.backgroundColor.x, L"_bg 0 0 0");
    Assert::AreEqual(1.0f, settings.exposure, L"_film _expo");
    Assert::IsTrue(settings.groundVisible, L"_setting _ground 1");
  }

  TEST_METHOD(KeepsTheRenderSettings)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    Assert::AreEqual(std::size_t{15}, model.renderObjects.size());

    const NeuronCore::VoxAttributes& sun = RenderObject(model, "_inf");
    ExpectAttribute(sun, "_i", "0.7");
    ExpectAttribute(sun, "_k", "255 255 255");
    ExpectAttribute(sun, "_angle", "50 50");
    ExpectAttribute(sun, "_area", "0.07");
    const NeuronCore::VoxAttributes& sky = RenderObject(model, "_uni");
    ExpectAttribute(sky, "_i", "0.7");
    ExpectAttribute(sky, "_k", "255 255 255");
    ExpectAttribute(RenderObject(model, "_lens"), "_fov", "45");
    const NeuronCore::VoxAttributes& film = RenderObject(model, "_film");
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
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const NeuronCore::VoxelGrid grid(model);

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
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const NeuronCore::VoxelGrid grid(model);
    const NeuronCore::ModelInstance& instance = model.instances.front();
    for (std::uint32_t i = 0; i < instance.recordCount; ++i)
    {
      const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(model.records[i]);
      const Int3 position = instance.origin + Int3{voxel.x, voxel.y, voxel.z};
      if (grid.VoxelAt(position) != i)
      {
        Assert::Fail(std::format(L"record {} is not at {} {} {}", i, position.x, position.y, position.z).c_str());
      }
    }
    Assert::AreEqual(NeuronCore::NO_VOXEL, grid.VoxelAt({0, 0, 300}), L"above the station");
  }

  TEST_METHOD(GridTracesAsBruteForceDoes)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const NeuronCore::VoxelGrid grid(model);
    const NeuronCore::ModelInstance& instance = model.instances.front();
    std::vector<NeuronCore::Box> boxes;
    boxes.reserve(model.records.size());
    for (const std::uint32_t record : model.records)
    {
      boxes.push_back(NeuronCore::VoxelBox(instance, record));
    }

    // Three views of the bounding sphere at the design's distance, in the file's 45-degree field of view, and the sun
    // straight overhead, each sampled on a lattice over the station's silhouette. The level view's centre row and column
    // and every ray of the sun have exactly-zero components.
    const Float3 center{0.5f, 0.5f, 127.5f};
    constexpr float DISTANCE = 520.3f;
    const std::array<Float3, 3> offsets{Float3{0.0f, -1.0f, 0.0f}, NeuronCore::Normalize({1.0f, -1.2f, 0.8f}),
                                        NeuronCore::Normalize({1.0f, 0.3f, -0.1f})};
    std::uint32_t hits = 0;
    std::uint32_t rays = 0;
    for (const Float3& offset : offsets)
    {
      const NeuronCore::PerspectiveView view =
        NeuronCore::MakePerspectiveView(center + offset * DISTANCE, center, {0.0f, 0.0f, 1.0f}, 0.785398163f, 0.1f, 161, 91);
      for (const std::uint32_t pixelY : {40u, 45u, 52u, 58u, 66u})
      {
        for (const std::uint32_t pixelX : {70u, 76u, 80u, 86u, 92u})
        {
          const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(view, pixelX, pixelY);
          const NeuronCore::TraceHit expected = NeuronCore::TraceBoxes<false>(boxes, ray, view.nearPlane);
          ExpectSameHit(expected, grid.Trace(ray, view.nearPlane), std::format(L"pixel {} {}", pixelX, pixelY));
          hits += expected.voxel != NeuronCore::NO_VOXEL ? 1u : 0u;
          ++rays;
        }
      }
    }

    const NeuronCore::OrthographicView sun =
      NeuronCore::MakeOrthographicView({0.5f, 0.5f, 300.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, 120.0f, 120.0f, 310.0f, 64, 64);
    for (const std::uint32_t pixelY : {14u, 26u, 34u, 44u, 52u})
    {
      for (const std::uint32_t pixelX : {12u, 24u, 32u, 40u, 52u})
      {
        const NeuronCore::Ray ray = NeuronCore::OrthographicRay(sun, pixelX, pixelY);
        const NeuronCore::TraceHit expected = NeuronCore::TraceBoxes<false>(boxes, ray, 0.0f);
        ExpectSameHit(expected, grid.Trace(ray, 0.0f), std::format(L"sun pixel {} {}", pixelX, pixelY));
        hits += expected.voxel != NeuronCore::NO_VOXEL ? 1u : 0u;
        ++rays;
      }
    }
    Logger::WriteMessage(std::format(L"{} of {} rays hit the station", hits, rays).c_str());
    Assert::IsTrue(2 * hits > rays, L"most rays hit, or the comparison says little");
  }

  // Design/SampleRenderer.md §12: time 0 is the intact station, every voxel where it was and unrotated, exactly.
  TEST_METHOD(ExplosionStartsIntact)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    const NeuronCore::VoxelPose intact{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    for (std::uint32_t voxel = 0; voxel < centers.size(); ++voxel)
    {
      NeuronCore::VoxelPose expected = intact;
      expected.center = centers[voxel];
      if (!SamePose(expected, NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, 0.0f)))
      {
        Assert::Fail(std::format(L"voxel {} moves at time 0", voxel).c_str());
      }
    }
  }

  // §14: no corner below the ground at any time, through every flight and in the last fall to rest.
  TEST_METHOD(ExplosionKeepsEveryCornerAboveTheGround)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    const std::vector<std::uint32_t> followed = FollowedVoxels(centers);
    float lowest = 1.0e9f;
    for (const std::uint32_t voxel : followed)
    {
      for (const float time : SampleTimes(NeuronCore::ExplosionRestTime(voxel, centers[voxel], parameters)))
      {
        const float point = LowestPoint(NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, time));
        lowest = std::min(lowest, point);
        if (point < -GROUND_TOLERANCE)
        {
          Assert::Fail(std::format(L"voxel {} reaches {} at {} s", voxel, point, time).c_str());
        }
      }
    }
    Logger::WriteMessage(std::format(L"{} voxels followed; the lowest point any reached is {}\n", followed.size(), lowest).c_str());
  }

  // §14: from its rest time on, a voxel lies flat on the ground, its center at z = 0.5 and its rotation one of the cube's
  // 24, and it stays there.
  TEST_METHOD(ExplosionComesToRestFlat)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    for (const std::uint32_t voxel : FollowedVoxels(centers))
    {
      const float rest = NeuronCore::ExplosionRestTime(voxel, centers[voxel], parameters);
      const NeuronCore::VoxelPose landed = NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, rest);
      const std::wstring what = std::format(L"voxel {} at rest", voxel);
      Assert::AreEqual(NeuronCore::VOXEL_REST_HEIGHT, landed.center.z, what.c_str());
      for (const Float3 axis : {landed.axisX, landed.axisY, landed.axisZ})
      {
        for (const float entry : {axis.x, axis.y, axis.z})
        {
          Assert::IsTrue(entry == 0.0f || entry == 1.0f || entry == -1.0f, what.c_str());
        }
      }
      const Float3 handed = NeuronCore::Cross(landed.axisX, landed.axisY);
      Assert::IsTrue(handed.x == landed.axisZ.x && handed.y == landed.axisZ.y && handed.z == landed.axisZ.z, what.c_str());
      for (const float later : {rest + 1.0f, 2.0f * rest + 60.0f})
      {
        Assert::IsTrue(SamePose(landed, NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, later)), what.c_str());
      }
    }
  }

  // §14: the center is continuous across every contact. Just before one, it is no farther from where the contact
  // leaves it than its speed carries it in the time left, and no voxel is faster than a launch at full speed that then
  // falls from the top of the station.
  TEST_METHOD(ExplosionIsContinuousAcrossContacts)
  {
    constexpr float STEP_SECONDS = 1.0e-3f;
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    const float launch = parameters.launchSpeed * (1.0f + parameters.speedJitter);
    const float liftSquared = 2.0f * parameters.gravity * (NeuronCore::VOXEL_BOUNDING_RADIUS + NeuronCore::EXPLOSION_LIFT_CLEARANCE);
    const float top = static_cast<float>(MODEL_ORIGIN.z + MODEL_SIZE.z);
    const float fastest = std::sqrt(launch * launch + liftSquared + 2.0f * parameters.gravity * top);
    for (const std::uint32_t voxel : FollowedVoxels(centers))
    {
      for (const float contact : NeuronCore::ExplosionContactTimes(voxel, centers[voxel], parameters))
      {
        const NeuronCore::VoxelPose at = NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, contact);
        const NeuronCore::VoxelPose before = NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, contact - STEP_SECONDS);
        const float moved = NeuronCore::Length(at.center - before.center);
        Assert::IsTrue(moved <= fastest * STEP_SECONDS + 1.0e-3f,
                       std::format(L"voxel {} jumps {} at its contact at {} s", voxel, moved, contact).c_str());
      }
    }
  }

  // §12: the closed-form envelope holds every box and every rest time, and the defaults keep it inside the shadow map's
  // square (§10), which is centered on the station's box.
  TEST_METHOD(ExplosionStaysInsideItsEnvelope)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    Float3 lower{1.0e9f, 1.0e9f, 1.0e9f};
    Float3 upper{-1.0e9f, -1.0e9f, -1.0e9f};
    for (const Float3 center : centers)
    {
      lower = {std::min(lower.x, center.x - 0.5f), std::min(lower.y, center.y - 0.5f), std::min(lower.z, center.z - 0.5f)};
      upper = {std::max(upper.x, center.x + 0.5f), std::max(upper.y, center.y + 0.5f), std::max(upper.z, center.z + 0.5f)};
    }
    const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, lower, upper);
    float latestRest = 0.0f;
    for (const std::uint32_t voxel : FollowedVoxels(centers))
    {
      const float rest = NeuronCore::ExplosionRestTime(voxel, centers[voxel], parameters);
      latestRest = std::max(latestRest, rest);
      Assert::IsTrue(rest <= envelope.restTimeSeconds, std::format(L"voxel {} rests at {} s", voxel, rest).c_str());
      for (const float time : SampleTimes(rest))
      {
        const NeuronCore::VoxelPose pose = NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, time);
        const float radius = NeuronCore::VOXEL_BOUNDING_RADIUS;
        const std::wstring where = std::format(L"voxel {} at {} s", voxel, time);
        Assert::IsTrue(pose.center.x - radius >= envelope.lower.x && pose.center.x + radius <= envelope.upper.x, where.c_str());
        Assert::IsTrue(pose.center.y - radius >= envelope.lower.y && pose.center.y + radius <= envelope.upper.y, where.c_str());
        Assert::IsTrue(pose.center.z + radius <= envelope.upper.z, where.c_str());
      }
    }

    const Float3 middle = (lower + upper) * 0.5f;
    const float reachX = std::max(middle.x - envelope.lower.x, envelope.upper.x - middle.x);
    const float reachY = std::max(middle.y - envelope.lower.y, envelope.upper.y - middle.y);
    Logger::WriteMessage(std::format(L"envelope {} x {} x {} voxels, {} and {} from the station's middle; rest by {} s, the latest "
                                     L"followed voxel at {} s\n",
                                     envelope.upper.x - envelope.lower.x, envelope.upper.y - envelope.lower.y,
                                     envelope.upper.z - envelope.lower.z, reachX, reachY, envelope.restTimeSeconds, latestRest)
                           .c_str());
    Assert::IsTrue(reachX <= NeuronCore::SHADOW_HALF_EXTENT && reachY <= NeuronCore::SHADOW_HALF_EXTENT,
                   L"the defaults keep the envelope inside the shadow map's square");
  }

  // §14: identical inputs give identical results.
  TEST_METHOD(ExplosionRepeatsItself)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    for (const std::uint32_t voxel : FollowedVoxels(centers))
    {
      for (const float time : {0.25f, 1.0f, 3.0f, 6.0f})
      {
        const NeuronCore::VoxelPose first = NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, time);
        Assert::IsTrue(SamePose(first, NeuronCore::ExplosionPose(voxel, centers[voxel], parameters, time)));
      }
    }
  }
};

} // namespace NeuronCoreTests
