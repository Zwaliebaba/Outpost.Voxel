#pragma once

#include "Sector.h"
#include "Skirmish.h"

#include "SkirmishLayout.h"

#include "Message.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace GameLogicTests
{

// The repository's GameData folder: above the working directory, which is the repository root under CI and the output
// directory under Test Explorer, or else above this source file. A missing folder fails the test rather than skipping
// it.
[[nodiscard]] std::filesystem::path GameDataDirectory();

// The sector _parameters describe, from the repository's models. A refusal fails the test.
[[nodiscard]] std::unique_ptr<GameLogic::Sector> MakeSector(const GameLogic::SectorParameters& _parameters);

// The sector as it is now, as an observer sees it: its entities, in the order of their ids, and its detonations.
[[nodiscard]] NeuronCore::Snapshot Describe(const GameLogic::Sector& _sector);

// The centers of _snapshot's stations.
[[nodiscard]] std::vector<NeuronCore::Float3> StationCenters(const NeuronCore::Snapshot& _snapshot);

// The model of entity _id in _snapshot, which holds it.
[[nodiscard]] std::uint16_t ModelOf(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id);

// The skirmish of _seed, from the repository's models. A refusal fails the test.
[[nodiscard]] std::unique_ptr<GameLogic::Skirmish> MakeSkirmish(std::uint32_t _seed);

// The skirmish _parameters describe, staged as _layout has it (Design/ADR/ADR-035). A refusal fails the test.
[[nodiscard]] std::unique_ptr<GameLogic::Skirmish> MakeStagedSkirmish(const GameLogic::SkirmishParameters& _parameters,
                                                                      const GameCore::SkirmishLayout& _layout);

// The skirmish as side _side sees it, or all of it.
[[nodiscard]] NeuronCore::Snapshot DescribeSkirmish(const GameLogic::Skirmish& _skirmish, std::uint8_t _side = NeuronCore::OBSERVER_SIDE);

} // namespace GameLogicTests
