#include "pch.h"

#include "RepositoryFile.h"

#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <source_location>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

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

} // namespace

std::filesystem::path FindRepositoryFile(const std::filesystem::path& _relative)
{
  std::optional<std::filesystem::path> found = FindAbove(std::filesystem::current_path(), _relative);
  if (!found)
  {
    found = FindAbove(std::filesystem::path(std::source_location::current().file_name()).parent_path(), _relative);
  }
  Assert::IsTrue(found.has_value(),
                 std::format(L"{} is not above the working directory or the test sources", _relative.generic_wstring()).c_str());
  return found.value_or(std::filesystem::path());
}

std::vector<std::uint8_t> ReadRepositoryFile(const std::filesystem::path& _relative)
{
  const std::filesystem::path path = FindRepositoryFile(_relative);
  std::ifstream file(path, std::ios::binary);
  std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  Assert::IsTrue(file.good() || file.eof(), std::format(L"{} could not be read", path.generic_wstring()).c_str());
  return bytes;
}

} // namespace NeuronCoreTests
