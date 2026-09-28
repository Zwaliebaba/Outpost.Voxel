#pragma once

#include "Sector.h"

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

// The sector as it is now: its entities, in the order of their ids, and its detonations.
[[nodiscard]] NeuronCore::Snapshot Describe(const GameLogic::Sector& _sector);

// The centers of _snapshot's stations.
[[nodiscard]] std::vector<NeuronCore::Float3> StationCenters(const NeuronCore::Snapshot& _snapshot);

// The model of entity _id in _snapshot, which holds it.
[[nodiscard]] std::uint16_t ModelOf(const NeuronCore::Snapshot& _snapshot, std::uint32_t _id);

} // namespace GameLogicTests
