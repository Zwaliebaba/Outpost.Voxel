#include "pch.h"

#include "CommandLine.h"

#include "NvfImport.h"
#include "NvfModel.h"
#include "VoxModel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

// What the process tells its caller (Design/NeuronVoxelFormat.md §6.3): CI's --check fails on anything but 0.
constexpr int UP_TO_DATE = 0;
constexpr int STALE = 1;
constexpr int REFUSED = 2;

// _path as UTF-8, whatever the console's code page.
[[nodiscard]] std::string Utf8(const std::filesystem::path& _path)
{
  const std::u8string text = _path.u8string();
  return {text.begin(), text.end()};
}

// What an artist does about a refusal (§5, §6.2).
[[nodiscard]] std::string_view Remedy(NeuronCore::NvfImportRefusal _refusal) noexcept
{
  switch (_refusal)
  {
  case NeuronCore::NvfImportRefusal::MalformedName:
    return "name the node as a part path such as hull/turret, a marker such as hull@weapon.main, or a pivot such as hull@pivot";
  case NeuronCore::NvfImportRefusal::UnnamedPart:
    return "only a file's one part may be unnamed; name this model";
  case NeuronCore::NvfImportRefusal::NoPart:
    return "the file has no visible part";
  case NeuronCore::NvfImportRefusal::SecondRoot:
    return "one part is the root, the only path of one segment; put this part under it";
  case NeuronCore::NvfImportRefusal::MissingPart:
    return "the part this names does not exist";
  case NeuronCore::NvfImportRefusal::DuplicateName:
    return "the name is taken by another node";
  case NeuronCore::NvfImportRefusal::RotatedPart:
    return "a part is never turned; turn its voxels instead";
  case NeuronCore::NvfImportRefusal::EvenMarker:
    return "a marker is odd in every dimension, so that its centre voxel is its middle";
  case NeuronCore::NvfImportRefusal::TooManyParts:
    return "an .nvf holds at most 1,024 parts";
  case NeuronCore::NvfImportRefusal::TooManyHardpoints:
    return "an .nvf holds at most 4,096 hardpoints";
  case NeuronCore::NvfImportRefusal::UnknownChunks:
    return "the .nvf holds chunks this version does not know, which a rewrite would lose; use a newer NvfImport, or --replace";
  case NeuronCore::NvfImportRefusal::OrphanHardpoint:
    return "a hardpoint authored in Blender is on a part that is gone; delete it in Blender, or restore the part";
  case NeuronCore::NvfImportRefusal::NameClash:
    return "this hardpoint was refined in Blender, which owns it now; delete the marker in MagicaVoxel";
  }
  return "";
}

[[nodiscard]] int Dump(const NvfImport::Options& _options)
{
  const auto model = NeuronCore::LoadNvfModel(_options.input);
  if (!model)
  {
    std::cerr << Utf8(_options.input) << ": " << NeuronCore::NvfErrorName(model.error()) << '\n';
    return REFUSED;
  }
  std::cout << NeuronCore::DumpNvfModel(*model);
  return UP_TO_DATE;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadBytes(const std::filesystem::path& _path)
{
  std::ifstream file(_path, std::ios::binary);
  if (!file)
  {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  return file.bad() ? std::nullopt : std::optional(std::move(bytes));
}

[[nodiscard]] int Import(const NvfImport::Options& _options)
{
  const std::string input = Utf8(_options.input);
  const std::string output = Utf8(_options.output);
  const auto vox = NeuronCore::LoadVoxModel(_options.input);
  if (!vox)
  {
    std::cerr << input << ": " << NeuronCore::VoxErrorName(vox.error()) << '\n';
    return REFUSED;
  }

  // The .nvf there now, whose Blender-authored hardpoints and pivots the import keeps, unless --replace discards them.
  std::optional<NeuronCore::NvfModel> previous;
  if (_options.mode != NvfImport::Mode::Replace && std::filesystem::exists(_options.output))
  {
    auto loaded = NeuronCore::LoadNvfModel(_options.output);
    if (!loaded)
    {
      std::cerr << output << ": " << NeuronCore::NvfErrorName(loaded.error())
                << ": it cannot be read to merge with; mend it, or use --replace\n";
      return REFUSED;
    }
    previous = std::move(*loaded);
  }

  const auto model = NeuronCore::ImportVoxModel(*vox, previous ? &*previous : nullptr);
  if (!model)
  {
    for (const NeuronCore::NvfImportError& error : model.error())
    {
      std::cerr << input << ": " << (error.node.empty() ? std::string() : error.node + ": ")
                << NeuronCore::NvfImportRefusalName(error.refusal) << ": " << Remedy(error.refusal) << '\n';
    }
    return REFUSED;
  }

  if (_options.mode == NvfImport::Mode::Check)
  {
    const auto bytes = NeuronCore::SerializeNvfModel(*model);
    if (!bytes)
    {
      std::cerr << input << ": " << NeuronCore::NvfErrorName(bytes.error()) << '\n';
      return REFUSED;
    }
    if (ReadBytes(_options.output) != *bytes)
    {
      std::cout << output << " is stale against " << input << "; run NvfImport " << input << ' ' << output << '\n';
      return STALE;
    }
    std::cout << output << " is up to date with " << input << '\n';
    return UP_TO_DATE;
  }

  if (const auto saved = NeuronCore::SaveNvfModel(*model, _options.output); !saved)
  {
    std::cerr << output << ": " << NeuronCore::NvfErrorName(saved.error()) << '\n';
    return REFUSED;
  }
  std::cout << output << ": " << model->parts.size() << " parts, " << model->records.size() << " voxels, " << model->hardpoints.size()
            << " hardpoints\n";
  return UP_TO_DATE;
}

} // namespace

// NvfImport: a .vox into an .nvf, or an .nvf as text (Design/NeuronVoxelFormat.md §6). wmain, the console entry point's
// wide form, because a path may hold what the ANSI code page cannot.
int wmain(int _argumentCount, wchar_t** _arguments)
{
  std::vector<std::wstring> arguments;
  arguments.reserve(static_cast<std::size_t>(std::max(_argumentCount, 1) - 1));
  for (int i = 1; i < _argumentCount; ++i)
  {
    arguments.emplace_back(_arguments[i]);
  }
  const auto options = NvfImport::ParseCommandLine(arguments);
  if (!options)
  {
    std::cerr << options.error() << "\n\n" << NvfImport::USAGE;
    return REFUSED;
  }
  try
  {
    return options->mode == NvfImport::Mode::Dump ? Dump(*options) : Import(*options);
  }
  catch (const std::exception& error)
  {
    std::cerr << "NvfImport: " << error.what() << '\n';
    return REFUSED;
  }
}
