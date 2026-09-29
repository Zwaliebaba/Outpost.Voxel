#pragma once

#include "Design.h"

#include "Float3.h"
#include "NvfModel.h"

#include <array>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace GameCoreTests
{

// The repository's GameData folder: above the working directory, which is the repository root under CI and the output
// directory under Test Explorer, or else above this source file. A missing folder fails the test rather than skipping
// it.
[[nodiscard]] std::filesystem::path GameDataDirectory();

// The model _name in GameData, as NVF's reader returns it. A refusal fails the test.
[[nodiscard]] NeuronCore::NvfModel LoadModel(std::string_view _name);

// The MVP's design _name, loaded from GameData and validated. A refusal fails the test.
[[nodiscard]] GameCore::Design LoadMvpDesign(std::string_view _name);

// Adds to _hull a part of its root, at _translation, _size across and holding _cells in its own space, each in palette
// entry 1.
void AddPart(NeuronCore::NvfModel& _hull, std::string _path, NeuronCore::Int3 _translation, NeuronCore::Int3 _size,
             std::span<const NeuronCore::Int3> _cells);

// The tail of AddPart's cells that reaches back from the gunship's hull behind its port engine and turns up across that
// engine's line (G38), at TAIL_TRANSLATION, TAIL_SIZE across.
inline constexpr std::array<NeuronCore::Int3, 9> TAIL_CELLS{
  {{0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 0, 3}, {0, 0, 4}, {0, 0, 5}, {0, 0, 6}, {0, 1, 0}, {0, 2, 0}}};
inline constexpr NeuronCore::Int3 TAIL_TRANSLATION{4, 0, -7};
inline constexpr NeuronCore::Int3 TAIL_SIZE{1, 3, 7};

// The name of the refusal _result carries, or "Accepted".
[[nodiscard]] std::string RefusalOf(const std::expected<GameCore::Design, GameCore::DesignError>& _result);

[[nodiscard]] std::wstring Widen(std::string_view _text);

} // namespace GameCoreTests
