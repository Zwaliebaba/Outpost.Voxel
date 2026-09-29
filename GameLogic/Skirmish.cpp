#include "pch.h"

#include "Skirmish.h"

#include "Sector.h"

#include "Catalogue.h"
#include "Design.h"
#include "DesignComposite.h"
#include "Profile.h"
#include "SkirmishLayout.h"
#include "WelcomeNames.h"

#include "Composite.h"
#include "Hash.h"
#include "NvfImport.h"
#include "NvfModel.h"
#include "VoxModel.h"

#include <algorithm>
#include <format>
#include <utility>

namespace GameLogic
{
namespace
{

using NeuronCore::Float3;

// A detonation's seed (Design/Archive/SpaceScene.md §5.5): PcgHash of the skirmish's count of detonations, offset by its
// seed as the layout's draws are (R21).
constexpr std::uint32_t SEED_STEP = 0x9E3779B9u;
constexpr std::uint32_t DETONATION_STREAM = 0xD7u;

// The skirmish's parameters in its command log: a version, then the seed and the tick rate.
constexpr std::uint8_t PARAMETERS_VERSION = 1;
constexpr std::size_t PARAMETERS_BYTES = 1 + 4 + 4;

[[nodiscard]] SkirmishError Refuse(SkirmishRefusal _refusal, std::string _detail)
{
  return {_refusal, std::move(_detail)};
}

// The middle of _composite's box, in its space: where an entity of it stands (Design/ADR/ADR-029).
[[nodiscard]] Float3 Middle(std::span<const NeuronCore::VoxModel> _models, const NeuronCore::CompositeModel& _composite)
{
  const NeuronCore::VoxelBounds bounds = NeuronCore::CompositeBounds(_models, _composite).value_or(NeuronCore::VoxelBounds{});
  return {0.5f * static_cast<float>(bounds.lower.x + bounds.upper.x), 0.5f * static_cast<float>(bounds.lower.y + bounds.upper.y),
          0.5f * static_cast<float>(bounds.lower.z + bounds.upper.z)};
}

// Whether _to lies within _rangeUnits of _from. Squared, in double precision: the skirmish's positions are whole or half
// voxels, so every term is exact and every build compares alike (R21).
[[nodiscard]] bool WithinRange(Float3 _from, Float3 _to, float _rangeUnits) noexcept
{
  const double dx = static_cast<double>(_to.x) - static_cast<double>(_from.x);
  const double dy = static_cast<double>(_to.y) - static_cast<double>(_from.y);
  const double dz = static_cast<double>(_to.z) - static_cast<double>(_from.z);
  const double range = _rangeUnits;
  return dx * dx + dy * dy + dz * dz <= range * range;
}

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> _bytes, std::size_t _offset) noexcept
{
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i)
  {
    value |= std::uint32_t{_bytes[_offset + i]} << (8u * i);
  }
  return value;
}

} // namespace

std::vector<std::uint8_t> EncodeSkirmishParameters(const SkirmishParameters& _parameters)
{
  std::vector<std::uint8_t> bytes{PARAMETERS_VERSION};
  for (const std::uint32_t value : {_parameters.seed, _parameters.tickRate})
  {
    for (std::uint32_t shift = 0; shift < 32u; shift += 8u)
    {
      bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
  }
  return bytes;
}

std::optional<SkirmishParameters> DecodeSkirmishParameters(std::span<const std::uint8_t> _bytes) noexcept
{
  if (_bytes.size() != PARAMETERS_BYTES || _bytes[0] != PARAMETERS_VERSION)
  {
    return std::nullopt;
  }
  return SkirmishParameters{ReadU32(_bytes, 1), ReadU32(_bytes, 5)};
}

const char* SkirmishRefusalName(SkirmishRefusal _refusal) noexcept
{
  switch (_refusal)
  {
  case SkirmishRefusal::BadParameter:
    return "BadParameter";
  case SkirmishRefusal::ModelNotLoaded:
    return "ModelNotLoaded";
  case SkirmishRefusal::DesignRefused:
    return "DesignRefused";
  }
  return "Unknown";
}

std::expected<std::unique_ptr<Skirmish>, SkirmishError> Skirmish::Create(const SkirmishParameters& _parameters,
                                                                         const std::filesystem::path& _gameData)
{
  if (_parameters.tickRate == 0)
  {
    return std::unexpected(Refuse(SkirmishRefusal::BadParameter, "a tick rate of 0"));
  }

  // Every design of the catalogue, validated as the server validates any design that enters a match.
  std::vector<GameCore::Design> designs;
  for (const GameCore::DesignSpec& spec : GameCore::Designs())
  {
    auto design = GameCore::LoadDesign(spec, _gameData);
    if (!design)
    {
      const bool unread = design.error().refusal == GameCore::DesignRefusal::HullUnreadable;
      return std::unexpected(
        Refuse(unread ? SkirmishRefusal::ModelNotLoaded : SkirmishRefusal::DesignRefused,
               unread ? design.error().detail
                      : std::format("{}: {}: {}", spec.name, GameCore::DesignRefusalName(design.error().refusal), design.error().detail)));
    }
    designs.push_back(std::move(*design));
  }

  // The models, in the order the composites first name them: each design's hull and modules, then the asteroids. Each
  // is read from <name>.nvf, hashed for the manifest and flattened, as the sector reads its own (Design/ADR/ADR-028).
  std::vector<std::string> names;
  const auto name = [&names](std::string_view _name)
  {
    if (std::ranges::find(names, _name) == names.end())
    {
      names.emplace_back(_name);
    }
  };
  for (const GameCore::Design& design : designs)
  {
    name(design.spec->hull);
    for (const GameCore::Mount& mount : design.mounts)
    {
      name(mount.module->name);
    }
  }
  for (const std::string_view asteroid : GameCore::ASTEROID_MODELS)
  {
    name(asteroid);
  }
  auto skirmish = std::unique_ptr<Skirmish>(new Skirmish());
  skirmish->m_parameters = _parameters;
  skirmish->m_settings = SpaceSettings(_parameters.seed);
  std::vector<NeuronCore::VoxModel> models;
  models.reserve(names.size());
  for (const std::string& model : names)
  {
    const std::string file = model + ".nvf";
    const auto bytes = NeuronCore::ReadNvfFile(_gameData / file);
    if (!bytes)
    {
      return std::unexpected(Refuse(SkirmishRefusal::ModelNotLoaded, std::format("{}: {}", file, NeuronCore::NvfErrorName(bytes.error()))));
    }
    const auto parsed = NeuronCore::ParseNvfModel(*bytes);
    if (!parsed)
    {
      return std::unexpected(
        Refuse(SkirmishRefusal::ModelNotLoaded, std::format("{}: {}", file, NeuronCore::NvfErrorName(parsed.error()))));
    }
    models.push_back(NeuronCore::FlattenNvfModel(*parsed));
    skirmish->m_manifest.push_back({model, NeuronCore::Fnv1aHash64(*bytes)});
  }

  // The composites: each design's, in the catalogue's order, then each asteroid alone; and their names and the sides',
  // which the welcome's payload carries for the client's figures.
  const GameCore::NamedModels named{names, models};
  GameCore::WelcomeNames welcomeNames;
  for (const GameCore::Design& design : designs)
  {
    auto composite = GameCore::DesignComposite(design, named);
    if (!composite)
    {
      return std::unexpected(Refuse(SkirmishRefusal::DesignRefused, composite.error()));
    }
    skirmish->m_composites.push_back(std::move(*composite));
    welcomeNames.composites.emplace_back(design.spec->name);
  }
  const auto firstAsteroid = static_cast<std::uint16_t>(skirmish->m_composites.size());
  for (const std::string_view asteroid : GameCore::ASTEROID_MODELS)
  {
    const auto model = GameCore::ModelIndex(named, asteroid);
    skirmish->m_composites.push_back({{{model.value_or(std::uint16_t{0}), {0, 0, 0}, {0.0f, 0.0f, 0.0f, 1.0f}}}});
    welcomeNames.composites.emplace_back(asteroid);
  }
  for (const GameCore::SideSpec& side : GameCore::SKIRMISH_SIDES)
  {
    skirmish->m_sides.push_back(side.color);
    welcomeNames.sides.emplace_back(side.name);
  }
  skirmish->m_welcomePayload = GameCore::EncodeWelcomeNames(welcomeNames);

  // The entities, where the seed lays them out: each side's core and ships, then the asteroids. Each stands at its anchor,
  // turned by one of the cube's rotations, spelled as the NVF importer's table spells it, so every voxel is exact. A
  // design's entity senses as far as its profile says, and an asteroid senses nothing.
  const GameCore::SkirmishLayout layout = GameCore::MakeSkirmishLayout(_parameters.seed);
  const auto place =
    [&skirmish, &models](std::uint16_t _composite, std::uint8_t _side, const GameCore::Anchor& _anchor, float _sensorRangeUnits)
  {
    const Float3 middle = Middle(models, skirmish->m_composites[_composite]);
    const NeuronCore::Quaternion rotation = NeuronCore::CubeRotationQuaternion(_anchor.turn).value_or(NeuronCore::Quaternion{});
    skirmish->m_entities.push_back(
      {_composite, _side, GameCore::AnchoredPosition(_anchor, middle), rotation, _sensorRangeUnits, std::nullopt});
  };
  for (const GameCore::LayoutUnit& unit : layout.units)
  {
    const auto design = std::ranges::find(designs, unit.design, [](const GameCore::Design& _design) { return _design.spec->name; });
    place(static_cast<std::uint16_t>(design - designs.begin()), unit.side, unit.anchor, GameCore::ComputeProfile(*design).sensorRangeUnits);
  }
  for (const GameCore::LayoutAsteroid& asteroid : layout.asteroids)
  {
    place(static_cast<std::uint16_t>(firstAsteroid + asteroid.model), 0, asteroid.anchor, 0.0f);
  }
  return skirmish;
}

std::uint32_t Skirmish::TickRate() const noexcept
{
  return m_parameters.tickRate;
}

const NeuronCore::WorldSettings& Skirmish::Settings() const noexcept
{
  return m_settings;
}

std::span<const NeuronCore::ManifestEntry> Skirmish::Manifest() const noexcept
{
  return m_manifest;
}

std::span<const NeuronCore::CompositeModel> Skirmish::Composites() const noexcept
{
  return m_composites;
}

std::span<const NeuronCore::SideColor> Skirmish::Sides() const noexcept
{
  return m_sides;
}

std::span<const std::uint8_t> Skirmish::WelcomePayload() const noexcept
{
  return m_welcomePayload;
}

void Skirmish::Advance(std::uint64_t /*_worldTick*/)
{
  // Nothing moves until phase 4 gives the ships their orders, and debris lasts until it is restored.
}

void Skirmish::Detonate(std::uint32_t _entity, std::uint64_t _worldTick)
{
  if (_entity == 0 || _entity > m_entities.size() || m_entities[_entity - 1].detonation)
  {
    return;
  }
  const std::uint32_t seed = NeuronCore::PcgHash(m_detonations * 256u + DETONATION_STREAM + m_parameters.seed * SEED_STEP);
  m_entities[_entity - 1].detonation = NeuronCore::DetonationEvent{_entity, seed, _worldTick, {0.0f, 0.0f, 0.0f}};
  ++m_detonations;
}

void Skirmish::Restore(std::uint32_t _entity)
{
  if (_entity != 0 && _entity <= m_entities.size())
  {
    m_entities[_entity - 1].detonation.reset();
  }
}

std::optional<NeuronServer::CommandRefusal> Skirmish::Refuses(const NeuronCore::Command& _command, std::uint8_t _side) const
{
  const bool namesEntity = _command.kind == NeuronCore::CommandKind::Detonate || _command.kind == NeuronCore::CommandKind::Restore;
  if (!namesEntity || _side == NeuronCore::OBSERVER_SIDE || _command.entity == 0 || _command.entity > m_entities.size())
  {
    return std::nullopt;
  }
  const std::uint8_t owner = m_entities[_command.entity - 1].side;
  if (owner != 0 && owner != _side)
  {
    return NeuronServer::CommandRefusal::OtherSidesEntity;
  }
  return std::nullopt;
}

void Skirmish::Describe(NeuronCore::Snapshot& _snapshot, std::uint8_t _side) const
{
  // Worked out afresh from every entity against every intact entity of the side, so a detonation or a restore changes
  // what the side sees on the tick it lands. A detonation goes with its entity.
  const std::size_t first = _snapshot.entities.size();
  for (std::size_t index = 0; index < m_entities.size(); ++index)
  {
    const Entity& entity = m_entities[index];
    if (Sees(_side, entity))
    {
      _snapshot.entities.push_back(
        {static_cast<std::uint32_t>(index + 1), entity.composite, entity.side, entity.position, entity.rotation, {0.0f, 0.0f, 0.0f}});
    }
  }
  for (std::size_t described = first; described < _snapshot.entities.size(); ++described)
  {
    const Entity& entity = m_entities[_snapshot.entities[described].id - 1];
    if (entity.detonation)
    {
      _snapshot.detonations.push_back(*entity.detonation);
    }
  }
}

bool Skirmish::Sees(std::uint8_t _side, const Entity& _entity) const noexcept
{
  if (_side == NeuronCore::OBSERVER_SIDE || _entity.side == _side)
  {
    return true;
  }
  return std::ranges::any_of(m_entities,
                             [_side, &_entity](const Entity& _sensor)
                             {
                               return _sensor.side == _side && !_sensor.detonation && _sensor.sensorRangeUnits > 0.0f &&
                                      WithinRange(_sensor.position, _entity.position, _sensor.sensorRangeUnits);
                             });
}

} // namespace GameLogic
