#include "pch.h"

#include "NvfImport.h"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace NeuronCore
{
namespace
{

// √½, rounded to float: the components of a quarter turn's quaternion.
constexpr float HALF_SQRT2 = 0.70710677f;

// One of the cube's 24 rotations: the images of +X, +Y and +Z, the matrix's columns, and its quaternion. The table was
// generated from the matrices in exact arithmetic, and a test holds RotationOf to giving each matrix back exactly.
struct CubeRotation
{
  std::array<Int3, 3> columns;
  Quaternion quaternion;
};

constexpr std::array<CubeRotation, 24> CUBE_ROTATIONS{{
  {{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {0.0f, 0.0f, 0.0f, 1.0f}},
  {{{{1, 0, 0}, {0, -1, 0}, {0, 0, -1}}}, {1.0f, 0.0f, 0.0f, 0.0f}},
  {{{{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}}}, {0.0f, 1.0f, 0.0f, 0.0f}},
  {{{{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}}}, {0.0f, 0.0f, 1.0f, 0.0f}},
  {{{{1, 0, 0}, {0, 0, 1}, {0, -1, 0}}}, {HALF_SQRT2, 0.0f, 0.0f, HALF_SQRT2}},
  {{{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}}, {-HALF_SQRT2, 0.0f, 0.0f, HALF_SQRT2}},
  {{{{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}}, {0.0f, HALF_SQRT2, HALF_SQRT2, 0.0f}},
  {{{{-1, 0, 0}, {0, 0, -1}, {0, -1, 0}}}, {0.0f, HALF_SQRT2, -HALF_SQRT2, 0.0f}},
  {{{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}}, {HALF_SQRT2, HALF_SQRT2, 0.0f, 0.0f}},
  {{{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}}, {0.0f, 0.0f, HALF_SQRT2, HALF_SQRT2}},
  {{{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}}, {0.0f, 0.0f, -HALF_SQRT2, HALF_SQRT2}},
  {{{{0, -1, 0}, {-1, 0, 0}, {0, 0, -1}}}, {HALF_SQRT2, -HALF_SQRT2, 0.0f, 0.0f}},
  {{{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}}, {0.5f, 0.5f, 0.5f, 0.5f}},
  {{{{0, 1, 0}, {0, 0, -1}, {-1, 0, 0}}}, {-0.5f, -0.5f, 0.5f, 0.5f}},
  {{{{0, -1, 0}, {0, 0, 1}, {-1, 0, 0}}}, {0.5f, -0.5f, -0.5f, 0.5f}},
  {{{{0, -1, 0}, {0, 0, -1}, {1, 0, 0}}}, {-0.5f, 0.5f, -0.5f, 0.5f}},
  {{{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}}, {-0.5f, -0.5f, -0.5f, 0.5f}},
  {{{{0, 0, 1}, {-1, 0, 0}, {0, -1, 0}}}, {0.5f, -0.5f, 0.5f, 0.5f}},
  {{{{0, 0, -1}, {1, 0, 0}, {0, -1, 0}}}, {0.5f, 0.5f, -0.5f, 0.5f}},
  {{{{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}}, {-0.5f, 0.5f, 0.5f, 0.5f}},
  {{{{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}}}, {0.0f, -HALF_SQRT2, 0.0f, HALF_SQRT2}},
  {{{{0, 0, 1}, {0, -1, 0}, {1, 0, 0}}}, {HALF_SQRT2, 0.0f, HALF_SQRT2, 0.0f}},
  {{{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}}, {0.0f, HALF_SQRT2, 0.0f, HALF_SQRT2}},
  {{{{0, 0, -1}, {0, -1, 0}, {-1, 0, 0}}}, {HALF_SQRT2, 0.0f, -HALF_SQRT2, 0.0f}},
}};

// What a marker's name after the @ says it sets: a part's pivot (§5).
constexpr std::string_view PIVOT_MARKER = "pivot";

// The name of a file's only part when its model is unnamed (§5).
constexpr std::string_view DEFAULT_PART_PATH = "main";

// What a model of the .vox is, by its name (§5).
enum class NodeKind : std::uint8_t
{
  Part,
  Hardpoint,
  Pivot
};

struct Node
{
  std::size_t instance; // in VoxModel::instances
  NodeKind kind;
  std::string part;      // the part's path, or for a marker the path of the part it is on; empty for an unnamed part
  std::string hardpoint; // a hardpoint marker's name
};

[[nodiscard]] bool IsOdd(Int3 _size) noexcept
{
  return _size.x % 2 != 0 && _size.y % 2 != 0 && _size.z % 2 != 0;
}

// The path of _path's parent: all but its last segment, or nothing for a root.
[[nodiscard]] std::string_view ParentPath(std::string_view _path) noexcept
{
  const std::size_t slash = _path.rfind('/');
  return slash == std::string_view::npos ? std::string_view() : _path.substr(0, slash);
}

// The centre of a marker's centre voxel, in the space of a part whose origin is _partOrigin (§6.2). The centre voxel,
// floor(size / 2), stays in its cell however the marker is turned, because the marker is odd in every dimension.
[[nodiscard]] Float3 MarkerCenter(const ModelInstance& _marker, Int3 _partOrigin) noexcept
{
  const Int3 cell = _marker.origin + Int3{_marker.size.x / 2, _marker.size.y / 2, _marker.size.z / 2} - _partOrigin;
  return {static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f, static_cast<float>(cell.z) + 0.5f};
}

} // namespace

const char* NvfImportRefusalName(NvfImportRefusal _refusal) noexcept
{
  switch (_refusal)
  {
  case NvfImportRefusal::MalformedName:
    return "MalformedName";
  case NvfImportRefusal::UnnamedPart:
    return "UnnamedPart";
  case NvfImportRefusal::NoPart:
    return "NoPart";
  case NvfImportRefusal::SecondRoot:
    return "SecondRoot";
  case NvfImportRefusal::MissingPart:
    return "MissingPart";
  case NvfImportRefusal::DuplicateName:
    return "DuplicateName";
  case NvfImportRefusal::RotatedPart:
    return "RotatedPart";
  case NvfImportRefusal::EvenMarker:
    return "EvenMarker";
  case NvfImportRefusal::TooManyParts:
    return "TooManyParts";
  case NvfImportRefusal::TooManyHardpoints:
    return "TooManyHardpoints";
  case NvfImportRefusal::UnknownChunks:
    return "UnknownChunks";
  case NvfImportRefusal::OrphanHardpoint:
    return "OrphanHardpoint";
  case NvfImportRefusal::NameClash:
    return "NameClash";
  }
  return "Unknown";
}

std::string DumpNvfModel(const NvfModel& _model)
{
  std::string text;
  const auto line = [&text]<typename... Arguments>(std::format_string<Arguments...> _format, Arguments&&... _arguments)
  {
    text += std::format(_format, std::forward<Arguments>(_arguments)...);
    text += '\n';
  };
  line("palette");
  for (std::size_t i = 0; i < _model.palette.size(); ++i)
  {
    const PaletteEntry& entry = _model.palette[i];
    std::string material = entry.emissive ? " emissive" : "";
    if (entry.emit != 0.0f || entry.flux != 0.0f)
    {
      material += std::format(" emit {} flux {}", entry.emit, entry.flux);
    }
    line("  {:2}: {:02x}{:02x}{:02x}{:02x}{}", i + 1, entry.red, entry.green, entry.blue, entry.alpha, material);
  }
  line("parts: {}, voxels: {}", _model.parts.size(), _model.records.size());
  for (const NvfPart& part : _model.parts)
  {
    const std::string parent = part.parent == NVF_NO_PARENT ? std::string("the model") : _model.parts[part.parent].path;
    line("  {}: size {} x {} x {}, at ({}, {}, {}) in {}, pivot ({}, {}, {}){}, voxels {} to {}", part.path, part.size.x, part.size.y,
         part.size.z, part.translation.x, part.translation.y, part.translation.z, parent, part.pivot.x, part.pivot.y, part.pivot.z,
         part.pivotAuthored ? " authored" : "", part.firstVoxel, part.firstVoxel + part.voxelCount - 1);
  }
  line("hardpoints: {}", _model.hardpoints.size());
  for (const NvfHardpoint& hardpoint : _model.hardpoints)
  {
    line("  {}: {} on {}, at ({}, {}, {}), rotation ({}, {}, {}, {}){}", hardpoint.name, HardpointType(hardpoint),
         _model.parts[hardpoint.part].path, hardpoint.position.x, hardpoint.position.y, hardpoint.position.z, hardpoint.rotation.x,
         hardpoint.rotation.y, hardpoint.rotation.z, hardpoint.rotation.w, hardpoint.fromVox ? ", from the .vox" : "");
  }
  for (const std::string& id : _model.unknownChunks)
  {
    line("unknown chunk: {}", id);
  }
  return text;
}

std::optional<Quaternion> CubeRotationQuaternion(const Rotation& _rotation) noexcept
{
  const auto isColumn = [](Float3 _axis, Int3 _column)
  {
    return _axis.x == static_cast<float>(_column.x) && _axis.y == static_cast<float>(_column.y) && _axis.z == static_cast<float>(_column.z);
  };
  for (const CubeRotation& rotation : CUBE_ROTATIONS)
  {
    if (isColumn(_rotation.axisX, rotation.columns[0]) && isColumn(_rotation.axisY, rotation.columns[1]) &&
        isColumn(_rotation.axisZ, rotation.columns[2]))
    {
      return rotation.quaternion;
    }
  }
  return std::nullopt;
}

std::expected<NvfModel, std::vector<NvfImportError>> ImportVoxModel(const VoxModel& _vox, const NvfModel* _previous)
{
  std::vector<NvfImportError> refusals;
  const auto refuse = [&refusals](NvfImportRefusal _refusal, std::string _node) { refusals.push_back({_refusal, std::move(_node)}); };

  // 1. What each model's name makes it (§5): a part, a hardpoint marker or a pivot marker.
  std::vector<Node> nodes;
  for (std::size_t i = 0; i < _vox.instances.size(); ++i)
  {
    const std::string& name = _vox.instances[i].name;
    const std::size_t at = name.find('@');
    if (name.empty())
    {
      nodes.push_back({i, NodeKind::Part, {}, {}});
    }
    else if (at == std::string::npos)
    {
      if (IsNvfPartPath(name))
      {
        nodes.push_back({i, NodeKind::Part, name, {}});
      }
      else
      {
        refuse(NvfImportRefusal::MalformedName, name);
      }
    }
    else
    {
      const std::string_view part = std::string_view(name).substr(0, at);
      const std::string_view marker = std::string_view(name).substr(at + 1);
      const bool pivot = marker == PIVOT_MARKER;
      if (IsNvfPartPath(part) && (pivot || IsNvfHardpointName(marker)))
      {
        nodes.push_back({i, pivot ? NodeKind::Pivot : NodeKind::Hardpoint, std::string(part), pivot ? std::string() : std::string(marker)});
      }
      else
      {
        refuse(NvfImportRefusal::MalformedName, name);
      }
    }
  }

  // An unnamed model is the part `main` when it is the file's only part, and refused beside any other.
  const auto partCount = static_cast<std::size_t>(std::ranges::count(nodes, NodeKind::Part, &Node::kind));
  for (Node& node : nodes)
  {
    if (node.kind == NodeKind::Part && node.part.empty())
    {
      if (partCount == 1)
      {
        node.part = DEFAULT_PART_PATH;
      }
      else
      {
        refuse(NvfImportRefusal::UnnamedPart, std::format("(unnamed model {})", node.instance + 1));
      }
    }
  }

  // 2. The parts: one of each path, never turned, and a tree whose one root is the only path of one segment. The map
  // orders them by path, which puts every parent before its children, since a parent's path begins its children's.
  std::map<std::string, std::size_t, std::less<>> parts; // path to node
  std::vector<std::string_view> roots;
  for (std::size_t n = 0; n < nodes.size(); ++n)
  {
    const Node& node = nodes[n];
    if (node.kind != NodeKind::Part || node.part.empty())
    {
      continue;
    }
    if (!parts.emplace(node.part, n).second)
    {
      refuse(NvfImportRefusal::DuplicateName, node.part);
      continue;
    }
    if (!IsIdentityRotation(_vox.instances[node.instance].rotation))
    {
      refuse(NvfImportRefusal::RotatedPart, node.part);
    }
    if (ParentPath(node.part).empty())
    {
      roots.push_back(node.part);
    }
  }
  if (parts.empty() && refusals.empty())
  {
    refuse(NvfImportRefusal::NoPart, {});
  }
  for (std::size_t r = 1; r < roots.size(); ++r)
  {
    refuse(NvfImportRefusal::SecondRoot, std::string(roots[r]));
  }
  for (const Node& node : nodes)
  {
    const std::string_view parent = ParentPath(node.part);
    if (node.kind == NodeKind::Part && !parent.empty() && !parts.contains(parent))
    {
      refuse(NvfImportRefusal::MissingPart, node.part);
    }
  }
  if (parts.size() > NVF_MAX_PARTS)
  {
    refuse(NvfImportRefusal::TooManyParts, {});
  }

  // 3. The markers: each on a part, odd in every dimension, at most one pivot for a part, and one of each hardpoint.
  std::map<std::string, std::string, std::less<>> markerNames; // hardpoint name to the marker's node name
  std::set<std::string, std::less<>> pivotParts;
  for (const Node& node : nodes)
  {
    if (node.kind == NodeKind::Part)
    {
      continue;
    }
    const ModelInstance& instance = _vox.instances[node.instance];
    if (!parts.contains(node.part))
    {
      refuse(NvfImportRefusal::MissingPart, instance.name);
    }
    if (!IsOdd(instance.size))
    {
      refuse(NvfImportRefusal::EvenMarker, instance.name);
    }
    const bool first =
      node.kind == NodeKind::Pivot ? pivotParts.insert(node.part).second : markerNames.emplace(node.hardpoint, instance.name).second;
    if (!first)
    {
      refuse(NvfImportRefusal::DuplicateName, instance.name);
    }
  }
  if (!refusals.empty())
  {
    return std::unexpected(std::move(refusals));
  }

  // 4. The model: parts in path order, each keeping its records in the .vox's order, placed in its parent's space.
  NvfModel model{};
  model.palette = _vox.palette;
  std::map<std::string, std::uint32_t, std::less<>> partIndices;
  std::vector<Int3> origins; // each part's origin in model space
  std::vector<bool> pivotMarked;
  for (const auto& [path, n] : parts)
  {
    const ModelInstance& instance = _vox.instances[nodes[n].instance];
    const std::string_view parentPath = ParentPath(path);
    const std::uint32_t parent = parentPath.empty() ? NVF_NO_PARENT : partIndices.find(parentPath)->second;
    const Int3 translation = parent == NVF_NO_PARENT ? instance.origin : instance.origin - origins[parent];
    const auto firstVoxel = static_cast<std::uint32_t>(model.records.size());
    const auto records = std::span(_vox.records).subspan(instance.firstRecord, instance.recordCount);
    model.records.insert(model.records.end(), records.begin(), records.end());
    const Float3 center{0.5f * static_cast<float>(instance.size.x), 0.5f * static_cast<float>(instance.size.y),
                        0.5f * static_cast<float>(instance.size.z)};
    partIndices.emplace(path, static_cast<std::uint32_t>(model.parts.size()));
    origins.push_back(instance.origin);
    pivotMarked.push_back(false);
    model.parts.push_back({.path = path,
                           .parent = parent,
                           .size = instance.size,
                           .translation = translation,
                           .pivot = center,
                           .pivotAuthored = false,
                           .firstVoxel = firstVoxel,
                           .voxelCount = instance.recordCount});
  }

  // Markers set pivots, and become hardpoints that carry FromVox, in the order of the .vox.
  for (const Node& node : nodes)
  {
    if (node.kind == NodeKind::Part)
    {
      continue;
    }
    const ModelInstance& instance = _vox.instances[node.instance];
    const std::uint32_t part = partIndices.find(node.part)->second;
    const Float3 position = MarkerCenter(instance, origins[part]);
    if (node.kind == NodeKind::Pivot)
    {
      model.parts[part].pivot = position; // a pivot has no orientation, so a turned marker's turn is not read
      pivotMarked[part] = true;
      continue;
    }
    const std::optional<Quaternion> rotation = CubeRotationQuaternion(instance.rotation);
    if (!rotation)
    {
      throw std::logic_error("The .vox reader gave a rotation that is not one of the cube's: " + instance.name);
    }
    model.hardpoints.push_back({.name = node.hardpoint, .part = part, .position = position, .rotation = *rotation, .fromVox = true});
  }

  // 5. The merge (§6.2): what Blender authored survives a voxel edit, and what came from markers is replaced by the
  // markers there are now.
  if (_previous != nullptr)
  {
    if (!_previous->unknownChunks.empty())
    {
      std::string ids;
      for (const std::string& id : _previous->unknownChunks)
      {
        ids += (ids.empty() ? "" : ", ") + id;
      }
      refuse(NvfImportRefusal::UnknownChunks, ids);
      return std::unexpected(std::move(refusals));
    }
    for (const NvfHardpoint& hardpoint : _previous->hardpoints)
    {
      if (hardpoint.fromVox)
      {
        continue;
      }
      // An orphan is named as a marker on its old part would be, so that the artist sees which part went.
      const std::string path = hardpoint.part < _previous->parts.size() ? _previous->parts[hardpoint.part].path : std::string();
      const auto part = partIndices.find(path);
      if (part == partIndices.end())
      {
        refuse(NvfImportRefusal::OrphanHardpoint, path + "@" + hardpoint.name);
        continue;
      }
      // A clash names the marker, which is what the artist deletes now that Blender owns the hardpoint.
      if (const auto marker = markerNames.find(hardpoint.name); marker != markerNames.end())
      {
        refuse(NvfImportRefusal::NameClash, marker->second);
        continue;
      }
      model.hardpoints.push_back(
        {.name = hardpoint.name, .part = part->second, .position = hardpoint.position, .rotation = hardpoint.rotation, .fromVox = false});
    }
    for (const NvfPart& previous : _previous->parts)
    {
      const auto part = partIndices.find(previous.path);
      if (previous.pivotAuthored && part != partIndices.end() && !pivotMarked[part->second])
      {
        model.parts[part->second].pivot = previous.pivot;
        model.parts[part->second].pivotAuthored = true;
      }
    }
  }
  if (model.hardpoints.size() > NVF_MAX_HARDPOINTS)
  {
    refuse(NvfImportRefusal::TooManyHardpoints, {});
  }
  if (!refusals.empty())
  {
    return std::unexpected(std::move(refusals));
  }
  return model;
}

} // namespace NeuronCore
