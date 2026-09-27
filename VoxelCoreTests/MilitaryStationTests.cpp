#include "pch.h"

#include "VoxFile.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VoxelCoreTests
{
namespace
{

using VoxelCore::Int3;

constexpr Int3 MODEL_SIZE{207, 228, 255};
constexpr Int3 MODEL_ORIGIN{-103, -114, 0}; // the translation 0 0 127, minus floor(size / 2)
constexpr std::uint32_t VOXEL_COUNT = 225048;

// Where the tests look for the repository's copy of the asset: above the working directory, which is the repository
// root under CI and the output directory under Test Explorer, then above this source file. A missing asset fails the
// test rather than skipping it.
[[nodiscard]] std::filesystem::path FindMilitaryStation()
{
  const std::filesystem::path relative = std::filesystem::path("GameData") / "MilitaryStation.vox";
  const std::array<std::filesystem::path, 2> starts{std::filesystem::current_path(),
                                                    std::filesystem::path(std::source_location::current().file_name()).parent_path()};
  for (const std::filesystem::path& start : starts)
  {
    for (std::filesystem::path directory = start; !directory.empty(); directory = directory.parent_path())
    {
      if (std::filesystem::exists(directory / relative))
      {
        return directory / relative;
      }
      if (directory == directory.parent_path())
      {
        break;
      }
    }
  }
  Assert::Fail(L"GameData/MilitaryStation.vox is not above the working directory or the test sources");
  return {};
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
  for (const VoxelCore::VoxAttributes& attributes : _model.renderObjects)
  {
    const auto type = attributes.find("_type");
    if (type != attributes.end() && type->second == _type)
    {
      return attributes;
    }
  }
  Assert::Fail(std::format(L"no rOBJ of type {}", std::wstring(_type.begin(), _type.end())).c_str());
  return _model.renderObjects.front();
}

void ExpectAttribute(const VoxelCore::VoxAttributes& _attributes, std::string_view _key, std::string_view _value)
{
  const auto found = _attributes.find(_key);
  const std::wstring what = std::format(L"{} = {}", std::wstring(_key.begin(), _key.end()), std::wstring(_value.begin(), _value.end()));
  Assert::IsTrue(found != _attributes.end(), what.c_str());
  Assert::AreEqual(std::string(_value), found->second, what.c_str());
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
};

} // namespace VoxelCoreTests
