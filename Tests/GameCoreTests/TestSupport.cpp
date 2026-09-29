#include "pch.h"

#include "TestSupport.h"

#include "Catalogue.h"

#include "VoxelRecord.h"

#include <cstdint>
#include <format>
#include <optional>
#include <source_location>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{
namespace
{

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

} // namespace

std::filesystem::path GameDataDirectory()
{
  const std::filesystem::path relative = std::filesystem::path("GameData") / "StationCore.nvf";
  std::optional<std::filesystem::path> found = FindAbove(std::filesystem::current_path(), relative);
  if (!found)
  {
    found = FindAbove(std::filesystem::path(std::source_location::current().file_name()).parent_path(), relative);
  }
  Assert::IsTrue(found.has_value(), L"GameData is not above the working directory or the test sources");
  return found.value_or(std::filesystem::path()).parent_path();
}

NeuronCore::NvfModel LoadModel(std::string_view _name)
{
  const std::string file = std::string(_name) + ".nvf";
  auto model = NeuronCore::LoadNvfModel(GameDataDirectory() / file);
  const std::string refusal = model ? std::string() : std::format("{}: {}", file, NeuronCore::NvfErrorName(model.error()));
  Assert::IsTrue(model.has_value(), Widen(refusal).c_str());
  return model ? std::move(*model) : NeuronCore::NvfModel{};
}

GameCore::Design LoadMvpDesign(std::string_view _name)
{
  const GameCore::DesignSpec* spec = GameCore::FindDesign(_name);
  Assert::IsTrue(spec != nullptr, std::format(L"the catalogue holds {}", Widen(_name)).c_str());
  auto design = GameCore::LoadDesign(*spec, GameDataDirectory());
  const std::string refusal =
    design ? std::string() : std::format("{}: {}", GameCore::DesignRefusalName(design.error().refusal), design.error().detail);
  Assert::IsTrue(design.has_value(), Widen(refusal).c_str());
  return design ? std::move(*design) : GameCore::Design{};
}

void AddPart(NeuronCore::NvfModel& _hull, std::string _path, NeuronCore::Int3 _translation, NeuronCore::Int3 _size,
             std::span<const NeuronCore::Int3> _cells)
{
  const auto firstVoxel = static_cast<std::uint32_t>(_hull.records.size());
  for (const NeuronCore::Int3 cell : _cells)
  {
    _hull.records.push_back(NeuronCore::PackVoxelRecord(
      {static_cast<std::uint8_t>(cell.x), static_cast<std::uint8_t>(cell.y), static_cast<std::uint8_t>(cell.z), 0}));
  }
  const NeuronCore::Float3 pivot{static_cast<float>(_size.x) / 2.0f, static_cast<float>(_size.y) / 2.0f,
                                 static_cast<float>(_size.z) / 2.0f};
  _hull.parts.push_back({std::move(_path), 0, _size, _translation, pivot, false, firstVoxel, static_cast<std::uint32_t>(_cells.size())});
}

std::string RefusalOf(const std::expected<GameCore::Design, GameCore::DesignError>& _result)
{
  return _result ? std::string("Accepted") : std::string(GameCore::DesignRefusalName(_result.error().refusal));
}

std::wstring Widen(std::string_view _text)
{
  return {_text.begin(), _text.end()};
}

} // namespace GameCoreTests
