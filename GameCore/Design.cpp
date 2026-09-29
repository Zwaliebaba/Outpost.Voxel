#include "pch.h"

#include "Design.h"

#include "Quaternion.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace GameCore
{
namespace
{

using NeuronCore::Int3;

// The steps to a cell's six face neighbors.
constexpr std::array<Int3, 6> FACE_STEPS{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};

// How far from the model's origin a mount's center may lie, in voxels: NVF's bound on a part's origin, doubled for the
// position within the part, which keeps every center exact in single precision and every cell inside int32.
constexpr float MAX_MOUNT_CELL = static_cast<float>(2 * NeuronCore::NVF_MAX_PART_ORIGIN);

[[nodiscard]] std::unexpected<DesignError> Refuse(DesignRefusal _refusal, std::string _detail)
{
  return std::unexpected(DesignError{_refusal, std::move(_detail)});
}

[[nodiscard]] std::string CellText(Int3 _cell)
{
  return std::format("({}, {}, {})", _cell.x, _cell.y, _cell.z);
}

[[nodiscard]] constexpr Int3 Scaled(Int3 _cell, std::int32_t _scale) noexcept
{
  return {_cell.x * _scale, _cell.y * _scale, _cell.z * _scale};
}

// An axis of a cube symmetry, whose entries are exactly 0 or ±1, as a step between cells.
[[nodiscard]] Int3 StepOf(NeuronCore::Float3 _axis) noexcept
{
  return {static_cast<std::int32_t>(_axis.x), static_cast<std::int32_t>(_axis.y), static_cast<std::int32_t>(_axis.z)};
}

// The type and size a mount's name spells as <type>.<size>.<label> (ADR-025), or nothing when it spells no mount.
struct MountName
{
  ModuleKind kind;
  MountSize size;
};

[[nodiscard]] std::optional<MountName> ParseMountName(std::string_view _name) noexcept
{
  if (!NeuronCore::IsNvfHardpointName(_name))
  {
    return std::nullopt;
  }
  const std::size_t first = _name.find('.');
  const std::size_t second = _name.find('.', first + 1);
  if (second == std::string_view::npos || _name.find('.', second + 1) != std::string_view::npos)
  {
    return std::nullopt;
  }
  const std::optional<ModuleKind> kind = ModuleKindOf(_name.substr(0, first));
  const std::optional<MountSize> size = MountSizeOf(_name.substr(first + 1, second - first - 1));
  if (!kind || !size)
  {
    return std::nullopt;
  }
  return MountName{*kind, *size};
}

// The cell whose center _position is, when it is one.
[[nodiscard]] std::optional<Int3> CenterCell(NeuronCore::Float3 _position) noexcept
{
  std::array<std::int32_t, 3> cell{};
  const std::array<float, 3> position{_position.x, _position.y, _position.z};
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    const float corner = position[axis] - 0.5f;
    if (!std::isfinite(corner) || std::abs(corner) > MAX_MOUNT_CELL || std::floor(corner) != corner)
    {
      return std::nullopt;
    }
    cell[axis] = static_cast<std::int32_t>(corner);
  }
  return Int3{cell[0], cell[1], cell[2]};
}

// A dense grid over a box of cells, holding one value per cell and 0 outside the box.
class CellGrid
{
public:
  CellGrid(Int3 _lower, Int3 _upper)
    : m_lower(_lower),
      m_size{_upper.x - _lower.x + 1, _upper.y - _lower.y + 1, _upper.z - _lower.z + 1},
      m_values(static_cast<std::size_t>(m_size.x) * static_cast<std::size_t>(m_size.y) * static_cast<std::size_t>(m_size.z), 0u)
  {
  }

  [[nodiscard]] bool Contains(Int3 _cell) const noexcept
  {
    const Int3 offset = _cell - m_lower;
    return offset.x >= 0 && offset.y >= 0 && offset.z >= 0 && offset.x < m_size.x && offset.y < m_size.y && offset.z < m_size.z;
  }

  [[nodiscard]] std::uint32_t At(Int3 _cell) const noexcept
  {
    return Contains(_cell) ? m_values[IndexOf(_cell)] : 0u;
  }

  void Set(Int3 _cell, std::uint32_t _value) noexcept
  {
    m_values[IndexOf(_cell)] = _value;
  }

private:
  [[nodiscard]] std::size_t IndexOf(Int3 _cell) const noexcept
  {
    const Int3 offset = _cell - m_lower;
    return static_cast<std::size_t>(offset.x) +
           static_cast<std::size_t>(m_size.x) *
             (static_cast<std::size_t>(offset.y) + static_cast<std::size_t>(m_size.y) * static_cast<std::size_t>(offset.z));
  }

  Int3 m_lower;
  Int3 m_size;
  std::vector<std::uint32_t> m_values;
};

// The hull's voxels in the model's space, each with the index of the part it came from.
struct FlatHull
{
  std::vector<HullVoxel> voxels;
  std::vector<std::uint32_t> parts;
  std::vector<Int3> partOrigins;
};

[[nodiscard]] std::expected<FlatHull, DesignError> Flatten(std::string_view _design, const NeuronCore::NvfModel& _hull)
{
  FlatHull flat;
  flat.partOrigins.reserve(_hull.parts.size());
  for (std::size_t index = 0; index < _hull.parts.size(); ++index)
  {
    const NeuronCore::NvfPart& part = _hull.parts[index];
    const bool root = part.parent == NeuronCore::NVF_NO_PARENT;
    if (root != (index == 0) || (!root && part.parent >= index))
    {
      return Refuse(DesignRefusal::HullUnreadable,
                    std::format("{}: {}", _design, NeuronCore::NvfErrorName(NeuronCore::NvfError::BadPartTree)));
    }
    if (std::size_t{part.firstVoxel} + part.voxelCount > _hull.records.size())
    {
      return Refuse(DesignRefusal::HullUnreadable,
                    std::format("{}: {}", _design, NeuronCore::NvfErrorName(NeuronCore::NvfError::BadVoxelRange)));
    }
    const Int3 origin = root ? part.translation : flat.partOrigins[part.parent] + part.translation;
    flat.partOrigins.push_back(origin);
    for (std::uint32_t voxel = 0; voxel < part.voxelCount; ++voxel)
    {
      const NeuronCore::VoxelRecord record = NeuronCore::UnpackVoxelRecord(_hull.records[part.firstVoxel + voxel]);
      flat.voxels.push_back({origin + Int3{record.x, record.y, record.z}, record.color});
      flat.parts.push_back(static_cast<std::uint32_t>(index));
    }
  }
  if (flat.voxels.empty())
  {
    return Refuse(DesignRefusal::NotOnePiece, std::format("{}: the hull has no voxels", _design));
  }
  return flat;
}

// The first class of _kind that holds a design spanning _lower to _upper with _voxels hull voxels.
[[nodiscard]] std::optional<SizeClass> ClassHolding(DesignKind _kind, Int3 _lower, Int3 _upper, std::size_t _voxels) noexcept
{
  const Int3 extent = _upper - _lower + Int3{1, 1, 1};
  for (std::size_t index = 0; index < SIZE_CLASS_COUNT; ++index)
  {
    const SizeClassSpec& spec = SizeClassOf(static_cast<SizeClass>(index));
    if (spec.kind == _kind && extent.x <= spec.boxVoxels.x && extent.y <= spec.boxVoxels.y && extent.z <= spec.boxVoxels.z &&
        _voxels <= spec.voxelBudget)
    {
      return static_cast<SizeClass>(index);
    }
  }
  return std::nullopt;
}

} // namespace

const char* DesignRefusalName(DesignRefusal _refusal) noexcept
{
  switch (_refusal)
  {
  case DesignRefusal::HullUnreadable:
    return "HullUnreadable";
  case DesignRefusal::BadMountName:
    return "BadMountName";
  case DesignRefusal::MountOffCenter:
    return "MountOffCenter";
  case DesignRefusal::MountOffAxis:
    return "MountOffAxis";
  case DesignRefusal::NoCommandMount:
    return "NoCommandMount";
  case DesignRefusal::ExtraCommandMount:
    return "ExtraCommandMount";
  case DesignRefusal::NoSizeClass:
    return "NoSizeClass";
  case DesignRefusal::PartsOverlap:
    return "PartsOverlap";
  case DesignRefusal::NotOnePiece:
    return "NotOnePiece";
  case DesignRefusal::MountHoldsHull:
    return "MountHoldsHull";
  case DesignRefusal::MountsOverlap:
    return "MountsOverlap";
  case DesignRefusal::MountDetached:
    return "MountDetached";
  case DesignRefusal::UnknownMount:
    return "UnknownMount";
  case DesignRefusal::MountFittedTwice:
    return "MountFittedTwice";
  case DesignRefusal::UnknownModule:
    return "UnknownModule";
  case DesignRefusal::WrongModule:
    return "WrongModule";
  case DesignRefusal::UnfittedMount:
    return "UnfittedMount";
  case DesignRefusal::PowerShort:
    return "PowerShort";
  }
  return "Unknown";
}

Int3 Facing(const Mount& _mount) noexcept
{
  return StepOf(_mount.turn.axisZ);
}

std::vector<Int3> MountCells(const Mount& _mount)
{
  const Int3 box = MountBox(_mount.size);
  const Int3 right = StepOf(_mount.turn.axisX);
  const Int3 up = StepOf(_mount.turn.axisY);
  const Int3 forward = StepOf(_mount.turn.axisZ);
  std::vector<Int3> cells;
  cells.reserve(static_cast<std::size_t>(box.x) * static_cast<std::size_t>(box.y) * static_cast<std::size_t>(box.z));
  for (std::int32_t x = -(box.x / 2); x <= box.x / 2; ++x)
  {
    for (std::int32_t y = -(box.y / 2); y <= box.y / 2; ++y)
    {
      for (std::int32_t z = -(box.z / 2); z <= box.z / 2; ++z)
      {
        cells.push_back(_mount.centerCell + Scaled(right, x) + Scaled(up, y) + Scaled(forward, z));
      }
    }
  }
  return cells;
}

std::expected<Design, DesignError> ValidateDesign(const DesignSpec& _spec, const NeuronCore::NvfModel& _hull)
{
  const std::string_view designName = _spec.name;
  auto flat = Flatten(designName, _hull);
  if (!flat)
  {
    return std::unexpected(std::move(flat.error()));
  }

  // Each hardpoint is a mount: named as ADR-025 spells one, centered on a voxel and turned by quarter turns.
  Design design{&_spec, SizeClass::Frigate, std::move(flat->voxels), {}};
  design.mounts.reserve(_hull.hardpoints.size());
  for (const NeuronCore::NvfHardpoint& hardpoint : _hull.hardpoints)
  {
    const std::optional<MountName> mountName = ParseMountName(hardpoint.name);
    if (!mountName)
    {
      return Refuse(DesignRefusal::BadMountName,
                    std::format("{}: {} is not <type>.<s|l>.<label> with a module's type", designName, hardpoint.name));
    }
    if (hardpoint.part >= flat->partOrigins.size())
    {
      return Refuse(DesignRefusal::HullUnreadable,
                    std::format("{}: {}", designName, NeuronCore::NvfErrorName(NeuronCore::NvfError::BadHardpointPart)));
    }
    const std::optional<Int3> center = CenterCell(hardpoint.position);
    if (!center)
    {
      return Refuse(DesignRefusal::MountOffCenter,
                    std::format("{}: {} stands at ({}, {}, {}), not at a voxel's center", designName, hardpoint.name, hardpoint.position.x,
                                hardpoint.position.y, hardpoint.position.z));
    }
    const NeuronCore::Rotation turn = NeuronCore::RotationOf(hardpoint.rotation);
    if (!NeuronCore::IsCubeSymmetry(turn))
    {
      return Refuse(DesignRefusal::MountOffAxis, std::format("{}: {} is not turned by quarter turns", designName, hardpoint.name));
    }
    design.mounts.push_back(
      {hardpoint.name, mountName->kind, mountName->size, flat->partOrigins[hardpoint.part] + *center, turn, false, nullptr});
  }

  const auto commands = std::ranges::count(design.mounts, ModuleKind::Command, &Mount::kind);
  if (commands == 0)
  {
    return Refuse(DesignRefusal::NoCommandMount, std::format("{}: no mount for a command module", designName));
  }
  if (commands > 1)
  {
    return Refuse(DesignRefusal::ExtraCommandMount, std::format("{}: {} mounts for a command module, not one", designName, commands));
  }

  // The class, from the box the hull and the mounts' boxes span, before anything is allocated over that box.
  std::vector<std::vector<Int3>> mountCells;
  mountCells.reserve(design.mounts.size());
  Int3 lower = design.voxels.front().cell;
  Int3 upper = lower;
  const auto widen = [&lower, &upper](Int3 _cell)
  {
    lower = {std::min(lower.x, _cell.x), std::min(lower.y, _cell.y), std::min(lower.z, _cell.z)};
    upper = {std::max(upper.x, _cell.x), std::max(upper.y, _cell.y), std::max(upper.z, _cell.z)};
  };
  for (const HullVoxel& voxel : design.voxels)
  {
    widen(voxel.cell);
  }
  for (const Mount& mount : design.mounts)
  {
    mountCells.push_back(MountCells(mount));
    std::ranges::for_each(mountCells.back(), widen);
  }
  const std::optional<SizeClass> sizeClass = ClassHolding(_spec.kind, lower, upper, design.voxels.size());
  if (!sizeClass)
  {
    const Int3 extent = upper - lower + Int3{1, 1, 1};
    return Refuse(DesignRefusal::NoSizeClass, std::format("{}: {} x {} x {} with {} voxels fits no class of its kind", designName, extent.x,
                                                          extent.y, extent.z, design.voxels.size()));
  }
  design.sizeClass = *sizeClass;

  // The hull's voxels: one to a cell, and one piece, face to face.
  CellGrid hull(lower, upper);
  for (std::size_t index = 0; index < design.voxels.size(); ++index)
  {
    const Int3 cell = design.voxels[index].cell;
    if (const std::uint32_t taken = hull.At(cell); taken != 0)
    {
      return Refuse(DesignRefusal::PartsOverlap,
                    std::format("{}: {} and {} both put a voxel at {}", designName, _hull.parts[flat->parts[taken - 1]].path,
                                _hull.parts[flat->parts[index]].path, CellText(cell)));
    }
    hull.Set(cell, static_cast<std::uint32_t>(index + 1));
  }
  std::vector<std::uint8_t> reached(design.voxels.size(), 0);
  std::vector<std::size_t> frontier{0};
  reached[0] = 1;
  std::size_t reachedCount = 1;
  while (!frontier.empty())
  {
    const Int3 cell = design.voxels[frontier.back()].cell;
    frontier.pop_back();
    for (const Int3 step : FACE_STEPS)
    {
      const std::uint32_t neighbor = hull.At(cell + step);
      if (neighbor != 0 && reached[neighbor - 1] == 0)
      {
        reached[neighbor - 1] = 1;
        ++reachedCount;
        frontier.push_back(neighbor - 1);
      }
    }
  }
  if (reachedCount != design.voxels.size())
  {
    return Refuse(DesignRefusal::NotOnePiece, std::format("{}: {} of its {} voxels are cut off from the rest", designName,
                                                          design.voxels.size() - reachedCount, design.voxels.size()));
  }

  // Each mount's box: clear of the hull and of the other boxes, and against the hull (G37). Its line, for a module that
  // works along one, runs from the box's front face along its facing to the edge of the grid (G38).
  CellGrid boxes(lower, upper);
  for (std::size_t index = 0; index < design.mounts.size(); ++index)
  {
    Mount& mount = design.mounts[index];
    bool touches = false;
    for (const Int3 cell : mountCells[index])
    {
      if (hull.At(cell) != 0)
      {
        return Refuse(DesignRefusal::MountHoldsHull,
                      std::format("{}: {}'s box holds hull voxel {}", designName, mount.name, CellText(cell)));
      }
      if (const std::uint32_t other = boxes.At(cell); other != 0)
      {
        return Refuse(DesignRefusal::MountsOverlap, std::format("{}: {}'s box overlaps {}'s at {}", designName, mount.name,
                                                                design.mounts[other - 1].name, CellText(cell)));
      }
      boxes.Set(cell, static_cast<std::uint32_t>(index + 1));
      touches = touches || std::ranges::any_of(FACE_STEPS, [&hull, cell](Int3 _step) { return hull.At(cell + _step) != 0; });
    }
    if (!touches)
    {
      return Refuse(DesignRefusal::MountDetached, std::format("{}: {}'s box shares no face with the hull", designName, mount.name));
    }
    if (WorksAlongLine(mount.kind))
    {
      const Int3 facing = Facing(mount);
      mount.lineClear = true;
      for (Int3 cell = mount.centerCell + Scaled(facing, MountBox(mount.size).z / 2 + 1); hull.Contains(cell); cell = cell + facing)
      {
        if (hull.At(cell) != 0)
        {
          mount.lineClear = false;
          break;
        }
      }
    }
  }

  // The fit: a module of the mount's kind and size at every mount, within the reactors' supply.
  for (const FitEntry& entry : _spec.fit)
  {
    const auto mount = std::ranges::find(design.mounts, entry.mount, &Mount::name);
    if (mount == design.mounts.end())
    {
      return Refuse(DesignRefusal::UnknownMount, std::format("{}: the fit names {}, which the hull lacks", designName, entry.mount));
    }
    if (mount->module != nullptr)
    {
      return Refuse(DesignRefusal::MountFittedTwice, std::format("{}: the fit names {} twice", designName, entry.mount));
    }
    const ModuleSpec* module = FindModule(entry.module);
    if (module == nullptr)
    {
      return Refuse(DesignRefusal::UnknownModule,
                    std::format("{}: the fit puts {}, which the catalogue lacks, at {}", designName, entry.module, entry.mount));
    }
    if (module->kind != mount->kind || module->size != mount->size)
    {
      return Refuse(DesignRefusal::WrongModule,
                    std::format("{}: {} is a {}.{} module, and {} a {}.{} mount", designName, module->name, HardpointTypeOf(module->kind),
                                MountSizeSegment(module->size), mount->name, HardpointTypeOf(mount->kind), MountSizeSegment(mount->size)));
    }
    mount->module = module;
  }
  float supply = 0.0f;
  float draw = 0.0f;
  for (const Mount& mount : design.mounts)
  {
    if (mount.module == nullptr)
    {
      return Refuse(DesignRefusal::UnfittedMount, std::format("{}: the fit leaves {} empty", designName, mount.name));
    }
    supply += mount.module->powerSupply;
    draw += mount.module->powerDraw;
  }
  if (draw > supply)
  {
    return Refuse(DesignRefusal::PowerShort, std::format("{}: its modules draw {} and its reactors supply {}", designName, draw, supply));
  }
  return design;
}

std::expected<Design, DesignError> LoadDesign(const DesignSpec& _spec, const std::filesystem::path& _gameData)
{
  const std::string file = std::string(_spec.hull) + ".nvf";
  const auto hull = NeuronCore::LoadNvfModel(_gameData / file);
  if (!hull)
  {
    return Refuse(DesignRefusal::HullUnreadable, std::format("{}: {}", file, NeuronCore::NvfErrorName(hull.error())));
  }
  return ValidateDesign(_spec, *hull);
}

} // namespace GameCore
