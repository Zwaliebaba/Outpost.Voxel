#include "pch.h"

#include "TestSupport.h"

#include <algorithm>
#include <format>
#include <optional>
#include <source_location>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
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
  const std::filesystem::path relative = std::filesystem::path("GameData") / "MilitaryStation.nvf";
  std::optional<std::filesystem::path> found = FindAbove(std::filesystem::current_path(), relative);
  if (!found)
  {
    found = FindAbove(std::filesystem::path(std::source_location::current().file_name()).parent_path(), relative);
  }
  Assert::IsTrue(found.has_value(), L"GameData is not above the working directory or the test sources");
  return found.value_or(std::filesystem::path()).parent_path();
}

std::unique_ptr<GameLogic::Sector> MakeSector(const GameLogic::SectorParameters& _parameters)
{
  auto sector = GameLogic::Sector::Create(_parameters, GameDataDirectory());
  const std::string refusal =
    sector ? std::string() : std::format("{}: {}", GameLogic::SectorRefusalName(sector.error().refusal), sector.error().detail);
  Assert::IsTrue(sector.has_value(), std::wstring(refusal.begin(), refusal.end()).c_str());
  return sector ? std::move(*sector) : nullptr;
}

NeuronCore::Snapshot Describe(const GameLogic::Sector& _sector)
{
  NeuronCore::Snapshot snapshot{};
  _sector.Describe(snapshot, NeuronCore::OBSERVER_SIDE);
  return snapshot;
}

std::vector<NeuronCore::Float3> StationCenters(const NeuronCore::Snapshot& _snapshot)
{
  std::vector<NeuronCore::Float3> centers;
  for (const NeuronCore::EntityState& entity : _snapshot.entities)
  {
    if (entity.composite == GameLogic::STATION_MODEL)
    {
      centers.push_back(entity.position);
    }
  }
  return centers;
}

std::uint16_t ModelOf(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id)
{
  const auto entity = std::ranges::find(_snapshot.entities, _id, &NeuronCore::EntityState::id);
  Assert::IsTrue(entity != _snapshot.entities.end(), std::format(L"entity {} is in the snapshot", _id).c_str());
  return entity == _snapshot.entities.end() ? std::uint16_t{0} : entity->composite;
}

std::unique_ptr<GameLogic::Skirmish> MakeSkirmish(std::uint32_t _seed)
{
  auto skirmish = GameLogic::Skirmish::Create({.seed = _seed}, GameDataDirectory());
  const std::string refusal =
    skirmish ? std::string() : std::format("{}: {}", GameLogic::SkirmishRefusalName(skirmish.error().refusal), skirmish.error().detail);
  Assert::IsTrue(skirmish.has_value(), std::wstring(refusal.begin(), refusal.end()).c_str());
  return skirmish ? std::move(*skirmish) : nullptr;
}

std::unique_ptr<GameLogic::Skirmish> MakeStagedSkirmish(const GameLogic::SkirmishParameters& _parameters,
                                                        const GameCore::SkirmishLayout& _layout)
{
  auto skirmish = GameLogic::Skirmish::Create(_parameters, GameDataDirectory(), _layout);
  const std::string refusal =
    skirmish ? std::string() : std::format("{}: {}", GameLogic::SkirmishRefusalName(skirmish.error().refusal), skirmish.error().detail);
  Assert::IsTrue(skirmish.has_value(), std::wstring(refusal.begin(), refusal.end()).c_str());
  return skirmish ? std::move(*skirmish) : nullptr;
}

NeuronCore::Snapshot DescribeSkirmish(const GameLogic::Skirmish& _skirmish, std::uint8_t _side)
{
  NeuronCore::Snapshot snapshot{};
  _skirmish.Describe(snapshot, _side);
  return snapshot;
}

} // namespace GameLogicTests
