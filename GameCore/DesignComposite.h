#pragma once

#include "Design.h"

#include "Message.h"
#include "VoxModel.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace GameCore
{

// The models a welcome names, by their names: what a design's composite takes its models' indices and shapes from.
struct NamedModels
{
  std::span<const std::string> names;           // each model's name in GameData, in the manifest's order
  std::span<const NeuronCore::VoxModel> models; // flattened, as NeuronCore::FlattenNvfModel gives them
};

// The composite that draws _design (Design/ADR/ADR-029, Design/ADR/ADR-030): its hull where it is, then at each mount, in
// its hull's order, the module the fit puts there, turned by the mount's turn and moved by whole voxels so that the
// module's center cell fills the mount's center cell. A module is one part, odd on every axis, in its mount's frame with
// +Z the way it faces (Design/ADR/ADR-027). The error names a model _models lacks, or a module that is not one such part.
[[nodiscard]] std::expected<NeuronCore::CompositeModel, std::string> DesignComposite(const Design& _design, const NamedModels& _models);

// The index of the model named _name in _models, if it names one.
[[nodiscard]] std::expected<std::uint16_t, std::string> ModelIndex(const NamedModels& _models, std::string_view _name);

} // namespace GameCore
