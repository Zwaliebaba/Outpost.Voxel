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
  const std::filesystem::path relative = std::filesystem::path("GameData") / "MilitaryStation.vox";
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
  if (!sector)
  {
    const std::string refusal = std::format("{}: {}", GameLogic::SectorRefusalName(sector.error().refusal), sector.error().detail);
    Assert::Fail(std::wstring(refusal.begin(), refusal.end()).c_str());
    return nullptr;
  }
  return std::move(*sector);
}

NeuronCore::Snapshot Describe(const GameLogic::Sector& _sector)
{
  NeuronCore::Snapshot snapshot{};
  _sector.Describe(snapshot);
  return snapshot;
}

std::vector<NeuronCore::Float3> StationCenters(const NeuronCore::Snapshot& _snapshot)
{
  std::vector<NeuronCore::Float3> centers;
  for (const NeuronCore::EntityState& entity : _snapshot.entities)
  {
    if (entity.modelIndex == GameLogic::STATION_MODEL)
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
  return entity == _snapshot.entities.end() ? std::uint16_t{0} : entity->modelIndex;
}

} // namespace GameLogicTests
