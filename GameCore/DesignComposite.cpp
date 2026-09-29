#include "pch.h"

#include "DesignComposite.h"

#include "NvfImport.h"

#include <algorithm>
#include <format>
#include <optional>

namespace GameCore
{
namespace
{

using NeuronCore::Int3;

constexpr NeuronCore::Quaternion IDENTITY{0.0f, 0.0f, 0.0f, 1.0f};

// _vector, whole, turned by _rotation, one of the cube's: exact, as every product is by 0 or ±1.
[[nodiscard]] Int3 Turned(const NeuronCore::Rotation& _rotation, Int3 _vector) noexcept
{
  const NeuronCore::Float3 turned =
    NeuronCore::RotateVector(_rotation, {static_cast<float>(_vector.x), static_cast<float>(_vector.y), static_cast<float>(_vector.z)});
  return {static_cast<std::int32_t>(turned.x), static_cast<std::int32_t>(turned.y), static_cast<std::int32_t>(turned.z)};
}

[[nodiscard]] bool IsOdd(std::int32_t _extent) noexcept
{
  return _extent % 2 != 0;
}

} // namespace

std::expected<std::uint16_t, std::string> ModelIndex(const NamedModels& _models, std::string_view _name)
{
  const auto found = std::ranges::find(_models.names, _name);
  if (found == _models.names.end())
  {
    return std::unexpected(std::format("no model is named {}", _name));
  }
  return static_cast<std::uint16_t>(found - _models.names.begin());
}

std::expected<NeuronCore::CompositeModel, std::string> DesignComposite(const Design& _design, const NamedModels& _models)
{
  const auto hull = ModelIndex(_models, _design.spec->hull);
  if (!hull)
  {
    return std::unexpected(std::format("{}: {}", _design.spec->name, hull.error()));
  }
  NeuronCore::CompositeModel composite{{{*hull, {0, 0, 0}, IDENTITY}}};
  for (const Mount& mount : _design.mounts)
  {
    const auto model = ModelIndex(_models, mount.module->name);
    if (!model)
    {
      return std::unexpected(std::format("{}: {}", _design.spec->name, model.error()));
    }
    const NeuronCore::VoxModel& module = _models.models[*model];
    if (module.instances.size() != 1 || !IsOdd(module.instances.front().size.x) || !IsOdd(module.instances.front().size.y) ||
        !IsOdd(module.instances.front().size.z))
    {
      return std::unexpected(
        std::format("{}: the module {} at {} is not one part, odd on every axis", _design.spec->name, mount.module->name, mount.name));
    }
    const std::optional<NeuronCore::Quaternion> turn = NeuronCore::CubeRotationQuaternion(mount.turn);
    if (!turn)
    {
      return std::unexpected(std::format("{}: {} is not turned by quarter turns", _design.spec->name, mount.name));
    }
    // The module's center cell m lands on the mount's c: t + R(m + ½) = c + ½. In doubled whole voxels, 2t is 2c + 1 less
    // R(2m + 1), whose every component is odd, so t is whole.
    const NeuronCore::ModelInstance& part = module.instances.front();
    const Int3 center{part.origin.x + part.size.x / 2, part.origin.y + part.size.y / 2, part.origin.z + part.size.z / 2};
    const Int3 doubled = Turned(mount.turn, {2 * center.x + 1, 2 * center.y + 1, 2 * center.z + 1});
    const Int3 translation{(2 * mount.centerCell.x + 1 - doubled.x) / 2, (2 * mount.centerCell.y + 1 - doubled.y) / 2,
                           (2 * mount.centerCell.z + 1 - doubled.z) / 2};
    composite.components.push_back({*model, translation, *turn});
  }
  return composite;
}

} // namespace GameCore
