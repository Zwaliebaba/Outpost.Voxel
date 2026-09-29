#include "pch.h"

#include "Box.h"
#include "Explosion.h"
#include "Fragmentation.h"
#include "Lighting.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "PinnedStation.h"
#include "Placement.h"
#include "RigidTransform.h"
#include "SceneTracer.h"
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
#include <functional>
#include <optional>
#include <source_location>
#include <span>
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

// In the engine's axes: MagicaVoxel's 207 x 228 x 255 with y and z swapped, and its translation 0 0 127, which the
// engine reads as (0, 127, 0), minus floor(size / 2).
constexpr Int3 MODEL_SIZE{207, 255, 228};
constexpr Int3 MODEL_ORIGIN{-103, 0, -114};
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

// The explosion tests follow every EXPLOSION_STRIDE-th voxel through this many samples of its flight, from the
// detonation to twice the envelope's stop time (Design/Archive/SpaceScene.md §5.5).
constexpr std::uint32_t EXPLOSION_STRIDE = 101;
constexpr std::uint32_t FLIGHT_SAMPLES = 128;

// How long after the detonation, in units of 1 / drag, a voxel has made all of its motion that a float can hold:
// e^-32 is far below the half of 2^-24 at which 1 - e^-32 rounds to 1.
constexpr float MOTION_DONE = 32.0f;

// Rounding allowed on a position, relative to its distance from the origin: a float holds a coordinate 1,000 voxels
// out to 6 × 10^-5, and the pose adds a few such roundings.
constexpr float POSITION_ROUNDING = 1.0e-6f;

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

// The box around every voxel while the station is intact, which the envelope is bounded from.
[[nodiscard]] std::pair<Float3, Float3> CenterBox(const std::vector<Float3>& _centers) noexcept
{
  Float3 lower{1.0e9f, 1.0e9f, 1.0e9f};
  Float3 upper{-1.0e9f, -1.0e9f, -1.0e9f};
  for (const Float3 center : _centers)
  {
    lower = {std::min(lower.x, center.x - 0.5f), std::min(lower.y, center.y - 0.5f), std::min(lower.z, center.z - 0.5f)};
    upper = {std::max(upper.x, center.x + 0.5f), std::max(upper.y, center.y + 0.5f), std::max(upper.z, center.z + 0.5f)};
  }
  return {lower, upper};
}

[[nodiscard]] std::vector<std::uint32_t> FollowedVoxels(const std::vector<Float3>& _centers)
{
  std::vector<std::uint32_t> voxels;
  for (std::uint32_t voxel = 0; voxel < _centers.size(); voxel += EXPLOSION_STRIDE)
  {
    voxels.push_back(voxel);
  }
  return voxels;
}

// The times a followed voxel is sampled at: from the detonation to twice the stop time.
[[nodiscard]] std::vector<float> SampleTimes(float _stopSeconds)
{
  std::vector<float> times;
  for (std::uint32_t sample = 0; sample <= FLIGHT_SAMPLES; ++sample)
  {
    times.push_back(2.0f * _stopSeconds * static_cast<float>(sample) / static_cast<float>(FLIGHT_SAMPLES));
  }
  return times;
}

// The station broken as the client breaks it (Design/ADR/ADR-024), in the model's space where RestCenters lie: each voxel's
// fragment, and each fragment with its pivot moved out of its part's space.
struct StationFragments
{
  std::vector<std::uint32_t> fragmentOf;
  std::vector<NeuronCore::Fragment> fragments;
  float radius;
};

[[nodiscard]] StationFragments BreakStation(const NeuronCore::VoxModel& _model)
{
  const NeuronCore::ModelFragments broken = NeuronCore::FragmentModel(_model, NeuronCore::DefaultFragmentationParameters(_model));
  Assert::AreEqual(std::size_t{1}, _model.instances.size(), L"the station is one part");
  const Int3 origin = _model.instances.front().origin;
  StationFragments station{broken.fragmentOf, broken.fragments, broken.partRadius.front()};
  for (NeuronCore::Fragment& fragment : station.fragments)
  {
    fragment.pivot = fragment.pivot + Float3{static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z)};
  }
  return station;
}

// Voxel _voxel of the station, whose center is _center, as the detonation poses it.
[[nodiscard]] NeuronCore::VoxelPose StationPose(const StationFragments& _station, std::uint32_t _voxel, Float3 _center,
                                                const NeuronCore::ExplosionParameters& _parameters, float _timeSeconds) noexcept
{
  const std::uint32_t fragment = _station.fragmentOf[_voxel];
  return NeuronCore::ExplosionPose(fragment, _station.fragments[fragment], _center, _parameters, _timeSeconds);
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

// The pins are in MagicaVoxel's axes (PinnedStation.h), and the engine's swap y and z (Design/Archive/NeuronVoxelFormat.md §12).
[[nodiscard]] constexpr Float3 FromPinnedAxes(Float3 _vector) noexcept
{
  return {_vector.x, _vector.z, _vector.y};
}

// The pins of Design/Archive/NeuronVoxelFormat.md §12.4 (PinnedStation.h). A pixel may show another voxel than its pin, or
// none, only where its ray passes within PIN_EDGE_EPSILON of an edge, the sliver of the splat tests (SampleRenderer §14):
// where it meets the grown box of the voxel it shows, if any, and misses the shrunk box of the voxel it was pinned to,
// if any. Two more checks make that rule mean what it says (Design/ADR/ADR-011): the ray must also meet the grown box of
// the voxel it was pinned to, and where it was pinned to none, miss the shrunk box of the voxel it shows. The tracer
// found the voxel a pixel shows along the ray, but not its pin, and without them a pin the ray passes nowhere near would
// count as grazed: a mirrored image would pass as differences on edges.
constexpr float PIN_EDGE_EPSILON = 1.0f / 256.0f;

// Pixels allowed to differ on an edge per image, which §12.4 sets from the first run after the axis move. That run
// found none in any of the four images, in CI's MSVC Debug build and under GCC 13.3 at -O2 alike, on 2026-09-28
// (Design/ADR/ADR-011). The bound is not zero for the reason the splat tests give: the pins were taken by another
// compiler, and one that contracts or rounds differently can move a ray that lands within rounding of an edge to its
// other side. Four is headroom.
constexpr std::uint32_t PIN_EDGE_LIMIT = 4;

// The pinned lighting agrees with NeuronCore to rounding, and to nothing looser. MSVC fuses multiply-adds that GCC,
// which took the pins, does not (AGENTS.md §3).
constexpr float PIN_COLOR_TOLERANCE = 1.0e-5f;

// Whether _ray meets the unit box around _center, grown or shrunk by _change, at or beyond _minDistance.
[[nodiscard]] bool MeetsCell(const NeuronCore::Ray& _ray, Float3 _center, float _change, float _minDistance) noexcept
{
  const float radius = 0.5f + _change;
  const NeuronCore::Box box = NeuronCore::MakeAxisAlignedBox(_center, {radius, radius, radius});
  float distance = 0.0f;
  Float3 normal{};
  return NeuronCore::IntersectBox<false, false>(box, _ray.origin, _ray.direction, NeuronCore::InverseDirection(_ray), distance, normal) &&
         distance >= _minDistance;
}

// The rule above over one image, _widthPixels by _heightPixels, traced through _grid with the rays _rayAt gives.
void ExpectPinnedImage(const wchar_t* _name, std::span<const PinnedPixel> _pinned, const NeuronCore::VoxelGrid& _grid,
                       const std::vector<Float3>& _centers, const std::function<NeuronCore::Ray(std::uint32_t, std::uint32_t)>& _rayAt,
                       float _minDistance, std::uint32_t _widthPixels, std::uint32_t _heightPixels)
{
  std::vector<std::uint32_t> pinned(static_cast<std::size_t>(_widthPixels) * _heightPixels, NeuronCore::NO_VOXEL);
  for (const PinnedPixel& pixel : _pinned)
  {
    pinned[pixel.pixel] = pixel.voxel;
  }
  std::uint32_t shown = 0;
  std::uint32_t edges = 0;
  std::vector<std::wstring> failures;
  for (std::uint32_t y = 0; y < _heightPixels; ++y)
  {
    for (std::uint32_t x = 0; x < _widthPixels; ++x)
    {
      const NeuronCore::Ray ray = _rayAt(x, y);
      const std::uint32_t traced = _grid.Trace(ray, _minDistance).voxel;
      const std::uint32_t expected = pinned[static_cast<std::size_t>(y) * _widthPixels + x];
      shown += traced != NeuronCore::NO_VOXEL ? 1u : 0u;
      if (traced == expected)
      {
        continue;
      }
      const bool nearTraced = traced == NeuronCore::NO_VOXEL || MeetsCell(ray, _centers[traced], PIN_EDGE_EPSILON, _minDistance);
      const bool nearPinned = expected == NeuronCore::NO_VOXEL || MeetsCell(ray, _centers[expected], PIN_EDGE_EPSILON, _minDistance);
      const std::uint32_t grazed = expected != NeuronCore::NO_VOXEL ? expected : traced;
      if (nearTraced && nearPinned && !MeetsCell(ray, _centers[grazed], -PIN_EDGE_EPSILON, _minDistance))
      {
        ++edges;
        continue;
      }
      failures.push_back(std::format(L"({}, {}): voxel {}, pinned to {}", x, y, traced, expected));
    }
  }
  Logger::WriteMessage(std::format(L"{}: {} pixels pinned to a voxel, {} show one, {} differ on an edge, {} elsewhere\n", _name,
                                   _pinned.size(), shown, edges, failures.size())
                         .c_str());
  for (std::size_t i = 0; i < std::min<std::size_t>(failures.size(), 10); ++i)
  {
    Logger::WriteMessage((failures[i] + L"\n").c_str());
  }
  Assert::IsTrue(failures.empty(), std::format(L"{}: {} pixels differ from their pin away from any edge", _name, failures.size()).c_str());
  Assert::IsTrue(edges <= PIN_EDGE_LIMIT,
                 std::format(L"{}: {} pixels differ on an edge, more than {}", _name, edges, PIN_EDGE_LIMIT).c_str());
}

// The station's box, as the grid holds it.
[[nodiscard]] std::pair<Float3, Float3> GridBox(const NeuronCore::VoxelGrid& _grid) noexcept
{
  const Int3 origin = _grid.Origin();
  const Int3 size = _grid.Size();
  const Float3 lower{static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z)};
  return {lower, lower + Float3{static_cast<float>(size.x), static_cast<float>(size.y), static_cast<float>(size.z)}};
}

// The sun's view of the station's box that the pins were taken with.
[[nodiscard]] NeuronCore::OrthographicView PinnedSunView(const NeuronCore::VoxelGrid& _grid)
{
  const auto [lower, upper] = GridBox(_grid);
  return NeuronCore::MakeShadowView(StationLighting(1.0f).toSun, FromPinnedAxes(PINNED_TARGET), PINNED_SUN_HALF_EXTENT, lower, upper,
                                    PINNED_SUN_PIXELS);
}

void ExpectColor(Float3 _expected, Float3 _actual, const std::wstring& _what)
{
  Assert::AreEqual(_expected.x, _actual.x, PIN_COLOR_TOLERANCE, _what.c_str());
  Assert::AreEqual(_expected.y, _actual.y, PIN_COLOR_TOLERANCE, _what.c_str());
  Assert::AreEqual(_expected.z, _actual.z, PIN_COLOR_TOLERANCE, _what.c_str());
}

} // namespace

// The figures of Design/Archive/SampleRenderer.md §3, measured once by a throwaway script and pinned here in C++.
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
    AreEqualInt3({1, 0, 1}, lower, L"lowest occupied position");
    AreEqualInt3({205, 254, 227}, upper, L"highest occupied position");

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

  TEST_METHOD(KeepsItsRenderObjects)
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

    // The occupied cells, in world space: the lowest layer sits exactly on y = 0 (Design/Archive/NeuronVoxelFormat.md §12.4).
    AreEqualInt3({-102, 0, -113}, grid.Origin(), L"grid origin");
    AreEqualInt3({205, 255, 227}, grid.Size(), L"grid size");

    // The default view frames this box's bounding sphere: radius about 199 around (0.5, 127.5, 0.5).
    const Int3 size = grid.Size();
    const double radius = 0.5 * std::hypot(static_cast<double>(size.x), static_cast<double>(size.y), static_cast<double>(size.z));
    Assert::AreEqual(199.1, radius, 0.05, L"bounding sphere radius");
    Assert::AreEqual(0.5, grid.Origin().x + 0.5 * size.x, 0.0, L"centre x");
    Assert::AreEqual(127.5, grid.Origin().y + 0.5 * size.y, 0.0, L"centre y");
    Assert::AreEqual(0.5, grid.Origin().z + 0.5 * size.z, 0.0, L"centre z");
  }

  TEST_METHOD(IsBoundedByItsOccupiedCells)
  {
    // Design/Archive/SpaceScene.md §4's occupied box, 205 × 227 × 255 in MagicaVoxel's axes, where the grid finds it: an entity
    // of this model stands at its middle (§5.1).
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const NeuronCore::VoxelGrid grid(model);
    const std::optional<NeuronCore::VoxelBounds> bounds = NeuronCore::OccupiedBounds(model);
    Assert::IsTrue(bounds.has_value(), L"the station has bounds");
    AreEqualInt3(grid.Origin(), bounds.value_or(NeuronCore::VoxelBounds{}).lower, L"lower");
    AreEqualInt3(grid.Origin() + grid.Size(), bounds.value_or(NeuronCore::VoxelBounds{}).upper, L"upper");
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
    const Float3 center{0.5f, 127.5f, 0.5f};
    constexpr float DISTANCE = 520.3f;
    const std::array<Float3, 3> offsets{Float3{0.0f, 0.0f, -1.0f}, NeuronCore::Normalize({1.0f, 0.8f, -1.2f}),
                                        NeuronCore::Normalize({1.0f, -0.1f, 0.3f})};
    std::uint32_t hits = 0;
    std::uint32_t rays = 0;
    for (const Float3& offset : offsets)
    {
      const NeuronCore::PerspectiveView view =
        NeuronCore::MakePerspectiveView(center + offset * DISTANCE, center, {0.0f, 1.0f, 0.0f}, 0.785398163f, 0.1f, 161, 91);
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
      NeuronCore::MakeOrthographicView({0.5f, 300.0f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 120.0f, 120.0f, 310.0f, 64, 64);
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

  // Design/Archive/SpaceScene.md §5.5: time 0 is the intact station, every voxel where it was and unrotated, exactly.
  TEST_METHOD(ExplosionStartsIntact)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const StationFragments station = BreakStation(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    const NeuronCore::VoxelPose intact{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    for (std::uint32_t voxel = 0; voxel < centers.size(); ++voxel)
    {
      NeuronCore::VoxelPose expected = intact;
      expected.center = centers[voxel];
      if (!SamePose(expected, StationPose(station, voxel, centers[voxel], parameters, 0.0f)))
      {
        Assert::Fail(std::format(L"voxel {} moves at time 0", voxel).c_str());
      }
    }
  }

  // §5.5: from the envelope's stop time on, no corner of any voxel is farther than EXPLOSION_STOP_DISTANCE from where it
  // ends; and where it ends, once its fragment's motion is done to the float's last bit, it is still (ADR-024).
  TEST_METHOD(ExplosionDriftsToAStop)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const StationFragments station = BreakStation(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    const auto [lower, upper] = CenterBox(centers);
    const float stop = NeuronCore::BoundExplosion(parameters, lower, upper, station.radius).stopSeconds;
    // Every fragment has started by the stop time, and has the least drag or more.
    const float done = stop + MOTION_DONE / parameters.minDrag;
    float mostLeft = 0.0f;
    for (std::uint32_t voxel = 0; voxel < centers.size(); ++voxel)
    {
      const NeuronCore::VoxelPose end = StationPose(station, voxel, centers[voxel], parameters, done);
      if (!SamePose(end, StationPose(station, voxel, centers[voxel], parameters, 2.0f * done)))
      {
        Assert::Fail(std::format(L"voxel {} does not end still", voxel).c_str());
      }
      for (const float time : {stop, 1.5f * stop})
      {
        const float left = CornerDistance(StationPose(station, voxel, centers[voxel], parameters, time), end);
        mostLeft = std::max(mostLeft, left);
        if (left > NeuronCore::EXPLOSION_STOP_DISTANCE + POSITION_ROUNDING * NeuronCore::Length(end.center))
        {
          Assert::Fail(std::format(L"voxel {} has {} to go at {} s, after the stop at {} s", voxel, left, time, stop).c_str());
        }
      }
    }
    Logger::WriteMessage(std::format(L"stop at {} s; the most any corner had left to go from then on was {}\n", stop, mostLeft).c_str());
  }

  // §5.5: the drag only slows a fragment, so no voxel moves faster than the fastest launch, the launch speed at the blast
  // origin with all of its variation, plus the fastest turn at the farthest a voxel lies from its pivot.
  TEST_METHOD(ExplosionIsNoFasterThanItsLaunch)
  {
    constexpr float STEP_SECONDS = 1.0e-3f;
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const StationFragments station = BreakStation(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    const float fastest =
      parameters.launchSpeed * std::exp(3.0f * parameters.speedSpread) + parameters.maxSpinRadians * parameters.drag * station.radius;
    const auto [lower, upper] = CenterBox(centers);
    const float stop = NeuronCore::BoundExplosion(parameters, lower, upper, station.radius).stopSeconds;
    float fastestSeen = 0.0f;
    for (const std::uint32_t voxel : FollowedVoxels(centers))
    {
      for (const float time : SampleTimes(stop))
      {
        const NeuronCore::VoxelPose before = StationPose(station, voxel, centers[voxel], parameters, time);
        const NeuronCore::VoxelPose after = StationPose(station, voxel, centers[voxel], parameters, time + STEP_SECONDS);
        const float moved = NeuronCore::Length(after.center - before.center);
        fastestSeen = std::max(fastestSeen, moved / STEP_SECONDS);
        Assert::IsTrue(moved <= fastest * STEP_SECONDS * 1.001f + 4.0f * POSITION_ROUNDING * NeuronCore::Length(after.center),
                       std::format(L"voxel {} moves {} in {} s at {} s", voxel, moved, STEP_SECONDS, time).c_str());
      }
    }
    Logger::WriteMessage(
      std::format(L"the fastest followed voxel moves at {} voxels a second, of {} at most\n", fastestSeen, fastest).c_str());
  }

  // §5.5: the closed-form envelope holds every box at every time, and the defaults keep it inside the shadow map's
  // square (Design/Archive/SampleRenderer.md §10), which is centered on the station's box.
  TEST_METHOD(ExplosionStaysInsideItsEnvelope)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const StationFragments station = BreakStation(model);
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    const auto [lower, upper] = CenterBox(centers);
    const NeuronCore::ExplosionEnvelope envelope = NeuronCore::BoundExplosion(parameters, lower, upper, station.radius);
    float farthest = 0.0f;
    for (const std::uint32_t voxel : FollowedVoxels(centers))
    {
      for (const float time : SampleTimes(envelope.stopSeconds))
      {
        const NeuronCore::VoxelPose pose = StationPose(station, voxel, centers[voxel], parameters, time);
        const float reached = NeuronCore::Length(pose.center - envelope.center) + NeuronCore::VOXEL_BOUNDING_RADIUS;
        farthest = std::max(farthest, reached);
        Assert::IsTrue(reached <= envelope.radius * (1.0f + POSITION_ROUNDING),
                       std::format(L"voxel {} at {} s reaches {} of {}", voxel, time, reached, envelope.radius).c_str());
      }
    }

    const Float3 middle = (lower + upper) * 0.5f;
    const float fit = NeuronCore::Length(envelope.center - middle) + envelope.radius;
    Logger::WriteMessage(
      std::format(L"envelope {} voxels about the blast origin, {} from the station's middle; the followed voxels reach {}; "
                  L"stop by {} s; {} of the shadow map's {} from its middle\n",
                  envelope.radius, NeuronCore::Length(envelope.center - middle), farthest, envelope.stopSeconds, fit,
                  NeuronCore::SHADOW_HALF_EXTENT)
        .c_str());
    Assert::IsTrue(fit <= NeuronCore::SHADOW_HALF_EXTENT, L"the defaults keep the envelope inside the shadow map's square");
  }

  // Identical inputs give identical results (D3).
  TEST_METHOD(ExplosionRepeatsItself)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const std::vector<Float3> centers = RestCenters(model);
    const StationFragments station = BreakStation(model);
    Assert::IsTrue(station.fragmentOf == BreakStation(model).fragmentOf, L"the station breaks the same way twice");
    const NeuronCore::ExplosionParameters parameters = NeuronCore::DefaultExplosionParameters(NeuronCore::VoxelCentroid(model));
    for (const std::uint32_t voxel : FollowedVoxels(centers))
    {
      for (const float time : {0.25f, 1.0f, 3.0f, 6.0f})
      {
        const NeuronCore::VoxelPose first = StationPose(station, voxel, centers[voxel], parameters, time);
        Assert::IsTrue(SamePose(first, StationPose(station, voxel, centers[voxel], parameters, time)));
      }
    }
  }
  // Design/Archive/NeuronVoxelFormat.md §12.4: the voxel each pixel shows, from the view splat tests' cameras and from the
  // station's sun, is the one pinned before the axis move, but for rays within rounding of an edge.
  TEST_METHOD(TracesThePinnedVoxels)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const NeuronCore::VoxelGrid grid(model);
    const std::vector<Float3> centers = RestCenters(model);
    for (const PinnedCamera& camera : PINNED_CAMERAS)
    {
      const NeuronCore::PerspectiveView view =
        NeuronCore::MakePerspectiveView(FromPinnedAxes(camera.eye), FromPinnedAxes(PINNED_TARGET), FromPinnedAxes(PINNED_UP),
                                        PINNED_FOV_Y_RADIANS, PINNED_NEAR_PLANE, PINNED_WIDTH_PIXELS, PINNED_HEIGHT_PIXELS);
      ExpectPinnedImage(
        camera.name, camera.pixels, grid, centers, [&view](std::uint32_t _x, std::uint32_t _y)
        { return NeuronCore::PerspectiveRay(view, _x, _y); }, view.nearPlane, PINNED_WIDTH_PIXELS, PINNED_HEIGHT_PIXELS);
    }
    const NeuronCore::OrthographicView sun = PinnedSunView(grid);
    ExpectPinnedImage(
      L"from the sun", PINNED_FROM_THE_SUN, grid, centers, [&sun](std::uint32_t _x, std::uint32_t _y)
      { return NeuronCore::OrthographicRay(sun, _x, _y); }, 0.0f, PINNED_SUN_PIXELS, PINNED_SUN_PIXELS);
  }

  // Design/Archive/SpaceScene.md §16, S-M2: the station as one placement, placed as the application places it, traces as its
  // grid does in every pixel of the pinned views: the same voxel, whose id is its record, at the same distance, with the
  // same normal. So the pins hold through the placement too.
  TEST_METHOD(TracesThroughOnePlacementAsItsGridDoes)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const NeuronCore::VoxelGrid grid(model);
    const Int3 origin = model.instances.front().origin;
    std::array<NeuronCore::Placement, 1> placements{NeuronCore::PlacePart(
      model, 0, 0, 0,
      {NeuronCore::IDENTITY_ROTATION, {static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z)}})};
    Assert::IsTrue(NeuronCore::AssignVoxelIds(placements));
    Assert::IsTrue(NeuronCore::IsAlignedPlacement(placements[0]), L"the station draws aligned");
    const NeuronCore::SceneTracer tracer({&model, 1}, placements);

    std::uint32_t hits = 0;
    for (const PinnedCamera& camera : PINNED_CAMERAS)
    {
      const NeuronCore::PerspectiveView view =
        NeuronCore::MakePerspectiveView(FromPinnedAxes(camera.eye), FromPinnedAxes(PINNED_TARGET), FromPinnedAxes(PINNED_UP),
                                        PINNED_FOV_Y_RADIANS, PINNED_NEAR_PLANE, PINNED_WIDTH_PIXELS, PINNED_HEIGHT_PIXELS);
      for (std::uint32_t y = 0; y < PINNED_HEIGHT_PIXELS; ++y)
      {
        for (std::uint32_t x = 0; x < PINNED_WIDTH_PIXELS; ++x)
        {
          const NeuronCore::Ray ray = NeuronCore::PerspectiveRay(view, x, y);
          const NeuronCore::TraceHit expected = grid.Trace(ray, view.nearPlane);
          ExpectSameHit(expected, tracer.Trace(ray, view.nearPlane), std::format(L"{}, pixel {} {}", camera.name, x, y));
          hits += expected.voxel != NeuronCore::NO_VOXEL ? 1u : 0u;
        }
      }
    }
    const NeuronCore::OrthographicView sun = PinnedSunView(grid);
    for (std::uint32_t y = 0; y < PINNED_SUN_PIXELS; ++y)
    {
      for (std::uint32_t x = 0; x < PINNED_SUN_PIXELS; ++x)
      {
        const NeuronCore::Ray ray = NeuronCore::OrthographicRay(sun, x, y);
        const NeuronCore::TraceHit expected = grid.Trace(ray, 0.0f);
        ExpectSameHit(expected, tracer.Trace(ray, 0.0f), std::format(L"from the sun, pixel {} {}", x, y));
        hits += expected.voxel != NeuronCore::NO_VOXEL ? 1u : 0u;
      }
    }
    Logger::WriteMessage(std::format(L"{} pixels show the station, through its grid and through its placement alike\n", hits).c_str());
  }

  // §12.4's pin for the lighting: a white surface under the station's sun and sky for fixed normals, and the level
  // camera's centre column above the horizon, where no voxel is: the background. The column below the horizon was the
  // ground's, and its pins retired with the ground (Design/ADR/ADR-013).
  TEST_METHOD(LightsAsPinned)
  {
    const NeuronCore::VoxModel model = LoadMilitaryStation();
    const NeuronCore::VoxelGrid grid(model);
    const NeuronCore::LightingParameters lighting = StationLighting(1.0f);
    for (const PinnedShade& shade : PINNED_SHADES)
    {
      ExpectColor(shade.color, NeuronCore::ShadeSurface({1.0f, 1.0f, 1.0f}, 0.0f, FromPinnedAxes(shade.normal), 1.0f, lighting),
                  std::format(L"normal ({}, {}, {})", shade.normal.x, shade.normal.y, shade.normal.z));
    }

    const NeuronCore::PerspectiveView level =
      NeuronCore::MakePerspectiveView(FromPinnedAxes(PINNED_CAMERAS[1].eye), FromPinnedAxes(PINNED_TARGET), FromPinnedAxes(PINNED_UP),
                                      PINNED_FOV_Y_RADIANS, PINNED_NEAR_PLANE, PINNED_WIDTH_PIXELS, PINNED_HEIGHT_PIXELS);
    const std::array<float, 1> unshadowed{NeuronCore::ORTHOGRAPHIC_FAR_DEPTH};
    const NeuronCore::ShadowMapImage map{1, 1, unshadowed};
    const NeuronCore::OrthographicView sun = PinnedSunView(grid);
    for (std::uint32_t y = 0; y < PINNED_SKY.size(); ++y)
    {
      // No voxel, so no normal, depth, albedo or emission.
      ExpectColor(PINNED_SKY[y],
                  NeuronCore::LightPixel(level, PINNED_WIDTH_PIXELS / 2, y, NeuronCore::NO_VOXEL, {0.0f, 0.0f, 0.0f}, 0.0f,
                                         {0.0f, 0.0f, 0.0f}, 0.0f, map, sun, lighting),
                  std::format(L"row {}", y));
    }
  }
};

} // namespace NeuronCoreTests
