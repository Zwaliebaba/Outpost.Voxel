#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace NeuronCoreTests
{

// _relative, a path from the repository's root, found above the working directory, which is the repository root under
// CI and the output directory under Test Explorer, or else above this source file. A missing file fails the test rather
// than skipping it.
[[nodiscard]] std::filesystem::path FindRepositoryFile(const std::filesystem::path& _relative);

// The bytes of the file FindRepositoryFile finds. A file that cannot be read fails the test.
[[nodiscard]] std::vector<std::uint8_t> ReadRepositoryFile(const std::filesystem::path& _relative);

} // namespace NeuronCoreTests
