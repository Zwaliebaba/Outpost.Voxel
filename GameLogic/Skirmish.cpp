#include "pch.h"

#include "Skirmish.h"

#include "Sector.h"

#include "Catalogue.h"
#include "Design.h"
#include "DesignComposite.h"
#include "Orders.h"
#include "Profile.h"
#include "WelcomeNames.h"

#include "Composite.h"
#include "Hash.h"
#include "NvfImport.h"
#include "NvfModel.h"
#include "Quaternion.h"
#include "VoxModel.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
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

// Combat's draws (Design/ADR/ADR-035), each from PcgHash of its stream, the seed and what it is for: a weapon's aim from
// its target's silhouette, a tie between targets, and a jink.
constexpr std::uint32_t AIM_STREAM = 0xA1u;
constexpr std::uint32_t TARGET_STREAM = 0x7Au;
constexpr std::uint32_t JINK_STREAM = 0x31u;

// The skirmish's parameters in its command log: a version, then the seed, the tick rate and the flags.
constexpr std::uint8_t PARAMETERS_VERSION = 2;
constexpr std::size_t PARAMETERS_BYTES = 1 + 4 + 4 + 1;
constexpr std::uint8_t BATTLE_FLAG = 1;
constexpr std::uint8_t NO_JINKING_FLAG = 2;

// How a moving ship keeps apart from another whole ship (Design/ADR/ADR-033): within their two spheres and CLEARANCE_MARGIN,
// and further by the distance they close in this many seconds, it steers away from the other and, when the other lies
// ahead, to the side away from it, the right when dead ahead, the harder as they near, up to this gain against its aim.
constexpr float AVOID_SECONDS = 1.0f;
constexpr float AVOID_GAIN = 1.5f;

// An attack closes until every working weapon's muzzle lies within this share of its range of its target's nearest voxel
// (G47): the rest is its margin, so that a target that drifts off does not take it out of range at once (ADR-035).
constexpr float ATTACK_RANGE_SHARE = 0.9f;

// How often a ship closing on a target finds its path anew, as the target moves.
constexpr float REPATH_SECONDS = 0.5f;

// A ship under fire jinks (G58): it slides across its heading, keeping its weapons where they bear, at a side speed of up
// to JINK_SPEED either way, drawn with PcgHash every JINK_SECONDS. It gets there within the side thrust its vectored
// engines give, GameCore::VECTORED_THRUST_SHARE of its acceleration, so that its agility decides how far a shell's lead
// misses it. Bearing on a target, it slides back toward where it began to bear once it is JINK_LEASH off it.
constexpr float JINK_SECONDS = 1.0f;
constexpr float JINK_SPEED = 20.0f;
constexpr float JINK_LEASH = 30.0f;

// The fewest voxels a piece cut off an entity breaks away with as debris of its own (ADR-035): a smaller chip goes as the
// voxels a shot removes do, so that erosion does not strew the sector with entities of a voxel or two, each drawn with
// its whole composite and sent with its whole mask. Measured over the 20-point battle: of the pieces a minute's fight
// cut off, the median held 2 voxels.
constexpr std::uint32_t PIECE_MIN_VOXELS = 16;

// An entity's index in the ships when it is none.
constexpr std::size_t NO_SHIP = std::numeric_limits<std::size_t>::max();

// A side's number is a bit of a mask of the sides that see an entity.
static_assert(GameCore::SIDE_COUNT < 8);

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

// _composite's sphere, about its middle: half its box's diagonal, as the sector measures a model's (Design/ADR/ADR-017).
[[nodiscard]] float Sphere(std::span<const NeuronCore::VoxModel> _models, const NeuronCore::CompositeModel& _composite)
{
  const NeuronCore::VoxelBounds bounds = NeuronCore::CompositeBounds(_models, _composite).value_or(NeuronCore::VoxelBounds{});
  const NeuronCore::Int3 extent = bounds.upper - bounds.lower;
  return 0.5f * NeuronCore::Length({static_cast<float>(extent.x), static_cast<float>(extent.y), static_cast<float>(extent.z)});
}

// What a ship can do on the plane (ADR-033): its design's _profile's speed cap, acceleration and turn rate, ADR-017's bank
// for its class, and its composite's _sphere.
[[nodiscard]] ShipClass ShipClassOf(const GameCore::Profile& _profile, float _sphere) noexcept
{
  const bool capital = _profile.sizeClass == GameCore::SizeClass::Capital;
  return {_sphere,
          _profile.speedUnitsPerSecond,
          0.0f,
          _profile.speedUnitsPerSecond,
          _profile.accelerationUnitsPerSecondSquared,
          _profile.turnRateRadiansPerSecond,
          capital ? CAPITAL_SHIP_BANK_LIMIT : FRIGATE_BANK_LIMIT,
          capital ? CAPITAL_SHIP_BANK_RATE : FRIGATE_BANK_RATE};
}

[[nodiscard]] float PlaneDistance(Float3 _from, Float3 _to) noexcept
{
  const float dx = _to.x - _from.x;
  const float dz = _to.z - _from.z;
  return std::sqrt(dx * dx + dz * dz);
}

// _vector on the plane at unit length, or _fallback when it has next to none.
[[nodiscard]] Float3 PlaneDirection(Float3 _vector, Float3 _fallback) noexcept
{
  const Float3 flat{_vector.x, 0.0f, _vector.z};
  const float length = NeuronCore::Length(flat);
  return length > 1.0e-3f ? flat * (1.0f / length) : _fallback;
}

// A ship's velocity, and exactly zero at rest.
[[nodiscard]] Float3 Velocity(const ShipMotion& _motion) noexcept
{
  return _motion.speed > 0.0f ? _motion.forward * _motion.speed : Float3{0.0f, 0.0f, 0.0f};
}

// Whether a ship on the plane is level: its up the world's, exactly, as Brake leaves a halted ship.
[[nodiscard]] bool IsLevel(const ShipMotion& _motion) noexcept
{
  return _motion.up.x == 0.0f && _motion.up.y == 1.0f && _motion.up.z == 0.0f;
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

// A draw of combat's randomness: PcgHash of _stream and the seed, then of _a, _b and _c in turn (R21).
[[nodiscard]] std::uint32_t Draw(std::uint32_t _seed, std::uint32_t _stream, std::uint32_t _a, std::uint32_t _b, std::uint32_t _c) noexcept
{
  return NeuronCore::PcgHash(_c + NeuronCore::PcgHash(_b + NeuronCore::PcgHash(_a + NeuronCore::PcgHash(_stream + _seed * SEED_STEP))));
}

// A draw as a share in [0, 1), from its top 24 bits, exactly.
[[nodiscard]] float Share(std::uint32_t _draw) noexcept
{
  return static_cast<float>(_draw >> 8u) * (1.0f / 16777216.0f);
}

// The world ticks in _seconds, at least one.
[[nodiscard]] std::uint64_t TicksOf(float _seconds, std::uint32_t _tickRate) noexcept
{
  return std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::llround(static_cast<double>(_seconds) * _tickRate)));
}

// The part of the segment from _from to _to within _range of _center, as fractions of it, if any.
[[nodiscard]] std::optional<std::pair<float, float>> WithinSphere(Float3 _from, Float3 _to, Float3 _center, float _range) noexcept
{
  const Float3 d = _to - _from;
  const Float3 f = _from - _center;
  const float a = NeuronCore::Dot(d, d);
  const float b = 2.0f * NeuronCore::Dot(f, d);
  const float c = NeuronCore::Dot(f, f) - _range * _range;
  if (a <= 0.0f)
  {
    return c <= 0.0f ? std::optional(std::pair(0.0f, 1.0f)) : std::nullopt;
  }
  const float discriminant = b * b - 4.0f * a * c;
  if (discriminant < 0.0f)
  {
    return std::nullopt;
  }
  const float root = std::sqrt(discriminant);
  const float first = std::max((-b - root) / (2.0f * a), 0.0f);
  const float last = std::min((-b + root) / (2.0f * a), 1.0f);
  return first < last ? std::optional(std::pair(first, last)) : std::nullopt;
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
  bytes.push_back(static_cast<std::uint8_t>((_parameters.battle ? BATTLE_FLAG : 0u) | (_parameters.jinking ? 0u : NO_JINKING_FLAG)));
  return bytes;
}

std::optional<SkirmishParameters> DecodeSkirmishParameters(std::span<const std::uint8_t> _bytes) noexcept
{
  if (_bytes.size() != PARAMETERS_BYTES || _bytes[0] != PARAMETERS_VERSION || (_bytes[9] & ~(BATTLE_FLAG | NO_JINKING_FLAG)) != 0)
  {
    return std::nullopt;
  }
  return SkirmishParameters{ReadU32(_bytes, 1), ReadU32(_bytes, 5), (_bytes[9] & BATTLE_FLAG) != 0, (_bytes[9] & NO_JINKING_FLAG) == 0};
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
  return Make(_parameters, _gameData,
              _parameters.battle ? GameCore::MakeBattleLayout(_parameters.seed) : GameCore::MakeSkirmishLayout(_parameters.seed));
}

std::expected<std::unique_ptr<Skirmish>, SkirmishError>
Skirmish::Create(const SkirmishParameters& _parameters, const std::filesystem::path& _gameData, const GameCore::SkirmishLayout& _layout)
{
  return Make(_parameters, _gameData, _layout);
}

std::expected<std::unique_ptr<Skirmish>, SkirmishError>
Skirmish::Make(const SkirmishParameters& _parameters, const std::filesystem::path& _gameData, const GameCore::SkirmishLayout& _layout)
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
  for (const GameCore::LayoutUnit& unit : _layout.units)
  {
    if (std::ranges::find(designs, unit.design, [](const GameCore::Design& _design) { return _design.spec->name; }) == designs.end())
    {
      return std::unexpected(
        Refuse(SkirmishRefusal::DesignRefused, std::format("the layout names {}, no design of the catalogue", unit.design)));
    }
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

  // What combat knows of each composite (ADR-035): its voxels, which shots meet, and each one's toughness; and for a
  // design's, its combat profile and its sensor range. An asteroid's voxels stop shots and take nothing.
  for (std::size_t composite = 0; composite < skirmish->m_composites.size(); ++composite)
  {
    const NeuronCore::CompositeModel& model = skirmish->m_composites[composite];
    std::vector<NeuronCore::Int3> cells;
    std::vector<float> toughness;
    if (composite < designs.size())
    {
      GameCore::CombatProfile profile = GameCore::ComputeCombatProfile(designs[composite], models, model);
      cells.reserve(profile.voxels.size());
      for (std::size_t voxel = 0; voxel < profile.voxels.size(); ++voxel)
      {
        cells.push_back(profile.voxels[voxel].cell);
        toughness.push_back(GameCore::MaterialOf(profile.materials[voxel]).toughness);
      }
      skirmish->m_designed.push_back(true);
      skirmish->m_profiles.push_back(std::move(profile));
      skirmish->m_sensorRanges.push_back(GameCore::ComputeProfile(designs[composite]).sensorRangeUnits);
    }
    else
    {
      for (const NeuronCore::CompositeVoxel& voxel : NeuronCore::CompositeVoxels(models, model))
      {
        cells.push_back(voxel.cell);
      }
      toughness.assign(cells.size(), std::numeric_limits<float>::infinity());
      skirmish->m_designed.push_back(false);
      skirmish->m_profiles.emplace_back();
      skirmish->m_sensorRanges.push_back(0.0f);
    }
    skirmish->m_bodies.emplace_back(std::move(cells));
    skirmish->m_toughness.push_back(std::move(toughness));
  }

  // The entities, where the layout puts them: each side's core and ships, then the asteroids. Each stands at its anchor,
  // turned by one of the cube's rotations, spelled as the NVF importer's table spells it, so every voxel is exact. A
  // design's entity senses as far as its profile says, and an asteroid senses nothing.
  const auto place = [&skirmish, &models](std::uint16_t _composite, std::uint8_t _side, const GameCore::Anchor& _anchor)
  {
    const Float3 middle = Middle(models, skirmish->m_composites[_composite]);
    const NeuronCore::Quaternion rotation = NeuronCore::CubeRotationQuaternion(_anchor.turn).value_or(NeuronCore::Quaternion{});
    Entity entity{_composite,
                  _side,
                  GameCore::AnchoredPosition(_anchor, middle),
                  rotation,
                  skirmish->m_sensorRanges[_composite],
                  std::nullopt,
                  {},
                  {},
                  {},
                  0,
                  {},
                  false,
                  false,
                  false};
    const GameCore::CombatProfile& profile = skirmish->m_profiles[_composite];
    for (const GameCore::CombatComponent& component : profile.components)
    {
      entity.remaining.push_back(component.voxelCount);
    }
    entity.weapons.assign(profile.weapons.size(), WeaponState{0, 0, false, true, 0, 0});
    skirmish->m_entities.push_back(std::move(entity));
  };
  for (const GameCore::LayoutUnit& unit : _layout.units)
  {
    const auto design = std::ranges::find(designs, unit.design, [](const GameCore::Design& _design) { return _design.spec->name; });
    place(static_cast<std::uint16_t>(design - designs.begin()), unit.side, unit.anchor);
  }
  for (const GameCore::LayoutAsteroid& asteroid : _layout.asteroids)
  {
    place(static_cast<std::uint16_t>(firstAsteroid + asteroid.model), 0, asteroid.anchor);
  }
  skirmish->m_records.assign(skirmish->m_entities.size(), CombatRecord{});

  // Each ship design's limits on the plane and its clearances of the obstacles (ADR-033). The obstacles are the entities
  // of every other composite, the cores and the asteroids, which never move, by their spheres.
  std::vector<float> spheres;
  spheres.reserve(skirmish->m_composites.size());
  for (const NeuronCore::CompositeModel& composite : skirmish->m_composites)
  {
    spheres.push_back(Sphere(models, composite));
  }
  const auto flies = [&designs](std::size_t _composite)
  { return _composite < designs.size() && designs[_composite].spec->kind == GameCore::DesignKind::Ship; };
  std::vector<Obstacle> obstacles;
  for (const Entity& entity : skirmish->m_entities)
  {
    if (!flies(entity.composite))
    {
      obstacles.push_back({entity.position, spheres[entity.composite]});
    }
  }
  std::vector<std::size_t> shipDesignOf(skirmish->m_composites.size(), NO_SHIP);
  for (std::size_t composite = 0; composite < designs.size(); ++composite)
  {
    if (flies(composite))
    {
      const float sphere = spheres[composite];
      shipDesignOf[composite] = skirmish->m_designs.size();
      skirmish->m_designs.push_back({ShipClassOf(GameCore::ComputeProfile(designs[composite]), sphere), Clearances(obstacles, sphere)});
    }
  }

  // Each ship starts idle and at rest, heading the way its design's front faces.
  skirmish->m_shipOf.assign(skirmish->m_entities.size(), NO_SHIP);
  for (std::size_t index = 0; index < skirmish->m_entities.size(); ++index)
  {
    const Entity& entity = skirmish->m_entities[index];
    const std::size_t design = shipDesignOf[entity.composite];
    if (design != NO_SHIP)
    {
      const NeuronCore::Rotation turn = NeuronCore::RotationOf(entity.rotation);
      skirmish->m_shipOf[index] = skirmish->m_ships.size();
      skirmish->m_ships.push_back({index,
                                   design,
                                   {entity.position, turn.axisZ, turn.axisY, 0.0f, 0.0f},
                                   Stance::Idle,
                                   {},
                                   0,
                                   0.0f,
                                   skirmish->m_designs[design].limits,
                                   1.0f,
                                   0,
                                   {0.0f, 0.0f, 0.0f},
                                   false,
                                   std::nullopt,
                                   0,
                                   {0.0f, 0.0f, 0.0f}});
    }
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

Skirmish::CombatRecord Skirmish::Record(std::uint32_t _entity) const noexcept
{
  return _entity != 0 && _entity <= m_records.size() ? m_records[_entity - 1] : CombatRecord{};
}

void Skirmish::Advance(std::uint64_t _worldTick)
{
  // The combat tick (ADR-035): what the side sees and every entity as the tick begins decide the targets and the fire,
  // every shot of the tick is swept against the same, and only then is the damage applied; the flight comes last.
  const float seconds = 1.0f / static_cast<float>(m_parameters.tickRate);
  Expire(_worldTick);
  std::vector<Shot> beams;
  std::vector<std::pair<std::size_t, std::size_t>> beamWeapons;
  std::vector<Removal> removals;
  {
    const std::vector<SweptEntity> swept = Swept();
    Target(swept, SeenBySides());
    Fire(_worldTick, swept, beams, beamWeapons);
    removals = Resolve(swept, beams, beamWeapons, seconds);
  }
  Suffer(removals, _worldTick);
  Fly(_worldTick, seconds);
}

void Skirmish::Detonate(std::uint32_t _entity, std::uint64_t _worldTick)
{
  if (_entity == 0 || _entity > m_entities.size() || !Intact(m_entities[_entity - 1]))
  {
    return;
  }
  Lose(_entity - 1, _worldTick);
}

void Skirmish::Restore(std::uint32_t _entity)
{
  // Whole again, as the design has it: every voxel back, every module working. A piece has no whole to return to, and
  // expired debris has left the world.
  if (_entity == 0 || _entity > m_entities.size())
  {
    return;
  }
  Entity& entity = m_entities[_entity - 1];
  if (entity.piece || entity.expired)
  {
    return;
  }
  entity.detonation.reset();
  entity.gone.clear();
  entity.damage.clear();
  const GameCore::CombatProfile& profile = m_profiles[entity.composite];
  for (std::size_t component = 0; component < profile.components.size(); ++component)
  {
    entity.remaining[component] = profile.components[component].voxelCount;
  }
  for (WeaponState& weapon : entity.weapons)
  {
    weapon.working = true;
  }
  entity.sensorRangeUnits = m_sensorRanges[entity.composite];
  if (m_shipOf[_entity - 1] != NO_SHIP)
  {
    m_ships[m_shipOf[_entity - 1]].thrustShare = 1.0f;
  }
}

std::optional<NeuronServer::CommandRefusal> Skirmish::Refuses(const NeuronCore::Command& _command, std::uint8_t _side) const
{
  if (_command.kind == NeuronCore::CommandKind::Game)
  {
    const auto order = GameCore::DecodeOrder(_command.payload);
    if (!order)
    {
      return NeuronServer::CommandRefusal::MalformedCommand;
    }
    for (const std::uint32_t ship : order->ships)
    {
      if (ship > m_entities.size() || m_shipOf[ship - 1] == NO_SHIP)
      {
        return NeuronServer::CommandRefusal::NotOrderable;
      }
      if (_side == NeuronCore::OBSERVER_SIDE || m_entities[ship - 1].side != _side)
      {
        return NeuronServer::CommandRefusal::OtherSidesEntity;
      }
    }
    // An attack names an entity of another side that combat can fight: a design's, whole, and no piece of one (ADR-035).
    if (order->kind == GameCore::OrderKind::Attack &&
        (order->target > m_entities.size() || !Fightable(m_entities[order->target - 1]) || m_entities[order->target - 1].side == _side))
    {
      return NeuronServer::CommandRefusal::NotOrderable;
    }
    return std::nullopt;
  }
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

void Skirmish::ApplyGameCommand(std::span<const std::uint8_t> _payload, std::uint8_t /*_side*/)
{
  const auto order = GameCore::DecodeOrder(_payload);
  if (!order)
  {
    return;
  }
  // An order changes what its ships fight (G71): each finds its targets afresh, an attack's first.
  for (const std::uint32_t id : order->ships)
  {
    if (WholeShip(id))
    {
      m_entities[id - 1].target = 0;
    }
  }
  switch (order->kind)
  {
  case GameCore::OrderKind::Move:
    Move(order->ships, order->targetX, order->targetZ);
    break;
  case GameCore::OrderKind::Stop:
    Halt(order->ships, Stance::Idle);
    break;
  case GameCore::OrderKind::Hold:
    Halt(order->ships, Stance::Holding);
    break;
  case GameCore::OrderKind::Attack:
    for (const std::uint32_t id : order->ships)
    {
      if (const std::optional<std::size_t> ship = WholeShip(id))
      {
        EndMove(m_ships[*ship], Stance::Attacking);
        m_ships[*ship].attackTarget = order->target;
        m_ships[*ship].pathTick = 0;
        m_entities[id - 1].target = order->target;
      }
    }
    break;
  case GameCore::OrderKind::AttackMove:
    Move(order->ships, order->targetX, order->targetZ);
    for (const std::uint32_t id : order->ships)
    {
      if (const std::optional<std::size_t> ship = WholeShip(id); ship && m_ships[*ship].stance == Stance::Moving)
      {
        m_ships[*ship].stance = Stance::AttackMoving;
        m_ships[*ship].destination = m_ships[*ship].path.back();
      }
    }
    break;
  }
}

void Skirmish::Describe(NeuronCore::Snapshot& _snapshot, std::uint8_t _side) const
{
  // Worked out afresh from every entity against every intact entity of the side, so a detonation or a restore changes
  // what the side sees on the tick it lands. A detonation and a mask go with their entity. A side learns the orders of
  // its own ships, and the observer every side's; and the shells and beams its sensors cover, its own whole (G54).
  const bool observer = _side == NeuronCore::OBSERVER_SIDE;
  const std::size_t first = _snapshot.entities.size();
  GameCore::SnapshotPayload payload;
  for (std::size_t index = 0; index < m_entities.size(); ++index)
  {
    const Entity& entity = m_entities[index];
    if (entity.expired || !Sees(_side, entity))
    {
      continue;
    }
    const auto id = static_cast<std::uint32_t>(index + 1);
    const std::optional<std::size_t> ship = WholeShip(id);
    const Float3 velocity = ship ? VelocityOf(m_ships[*ship]) : Float3{0.0f, 0.0f, 0.0f};
    _snapshot.entities.push_back({id, entity.composite, entity.side, entity.position, entity.rotation, velocity});
    if (ship && (observer || entity.side == _side))
    {
      const Ship& whole = m_ships[*ship];
      switch (whole.stance)
      {
      case Stance::Moving:
        payload.orders.push_back({id, GameCore::ShipState::Moving, whole.path.back().x, whole.path.back().z});
        break;
      case Stance::Holding:
        payload.orders.push_back({id, GameCore::ShipState::Holding, 0.0f, 0.0f});
        break;
      case Stance::Attacking:
        payload.orders.push_back({id, GameCore::ShipState::Attacking, 0.0f, 0.0f, whole.attackTarget});
        break;
      case Stance::AttackMoving:
        payload.orders.push_back({id, GameCore::ShipState::AttackMoving, whole.destination.x, whole.destination.z});
        break;
      case Stance::Idle:
        break;
      }
    }
  }
  for (const Shell& shell : m_shells)
  {
    if (observer || shell.side == _side || Covers(_side, shell.position))
    {
      payload.shells.push_back({shell.position, shell.velocity, shell.side});
    }
  }
  for (const Beam& beam : m_beams)
  {
    if (observer || beam.side == _side)
    {
      payload.beams.push_back({beam.from, beam.to, beam.side});
      continue;
    }
    // The parts of the beam within the side's sensors, each once, in order along it.
    std::vector<std::pair<float, float>> parts;
    for (const Entity& sensor : m_entities)
    {
      if (sensor.side == _side && Intact(sensor) && sensor.sensorRangeUnits > 0.0f)
      {
        if (const auto part = WithinSphere(beam.from, beam.to, sensor.position, sensor.sensorRangeUnits))
        {
          parts.push_back(*part);
        }
      }
    }
    std::ranges::sort(parts);
    for (std::size_t part = 0; part < parts.size();)
    {
      float last = parts[part].second;
      std::size_t next = part + 1;
      while (next < parts.size() && parts[next].first <= last)
      {
        last = std::max(last, parts[next].second);
        ++next;
      }
      payload.beams.push_back({beam.from + (beam.to - beam.from) * parts[part].first, beam.from + (beam.to - beam.from) * last, beam.side});
      part = next;
    }
  }
  _snapshot.payload = GameCore::EncodeSnapshotPayload(payload);
  for (std::size_t described = first; described < _snapshot.entities.size(); ++described)
  {
    const std::uint32_t id = _snapshot.entities[described].id;
    const Entity& entity = m_entities[id - 1];
    if (entity.detonation)
    {
      _snapshot.detonations.push_back(*entity.detonation);
    }
    if (std::ranges::any_of(entity.gone, [](std::uint8_t _byte) { return _byte != 0; }))
    {
      _snapshot.masks.push_back({id, static_cast<std::uint32_t>(m_bodies[entity.composite].Cells().size()), entity.gone});
    }
  }
}

std::optional<std::size_t> Skirmish::WholeShip(std::uint32_t _id) const noexcept
{
  if (_id == 0 || _id > m_entities.size() || m_shipOf[_id - 1] == NO_SHIP || m_entities[_id - 1].detonation)
  {
    return std::nullopt;
  }
  return m_shipOf[_id - 1];
}

void Skirmish::Move(std::span<const std::uint32_t> _ships, float _targetX, float _targetZ)
{
  // The group is the whole ships named. It flies as one, at its slowest member's speed cap, acceleration and turn rate,
  // each member to the target plus its offset from the group's middle, or the nearest point it may go to.
  std::vector<std::size_t> group;
  float cap = std::numeric_limits<float>::max();
  float acceleration = std::numeric_limits<float>::max();
  float turnRate = std::numeric_limits<float>::max();
  Float3 middle{0.0f, 0.0f, 0.0f};
  for (const std::uint32_t id : _ships)
  {
    if (const std::optional<std::size_t> ship = WholeShip(id))
    {
      const ShipClass& limits = m_designs[m_ships[*ship].design].limits;
      cap = std::min(cap, limits.maxSpeed);
      acceleration = std::min(acceleration, limits.acceleration);
      turnRate = std::min(turnRate, limits.turnRate);
      middle = middle + m_ships[*ship].motion.position;
      group.push_back(*ship);
    }
  }
  if (group.empty())
  {
    return;
  }
  middle = middle * (1.0f / static_cast<float>(group.size()));

  // Each member's path, from where it is. A member's pace is the group's cap in proportion to its path's length against
  // the longest, so that the group arrives together.
  std::vector<std::vector<Float3>> paths;
  paths.reserve(group.size());
  float longest = 0.0f;
  for (const std::size_t ship : group)
  {
    const Float3 from = m_ships[ship].motion.position;
    const Clearances& clearances = m_designs[m_ships[ship].design].clearances;
    const Float3 destination = clearances.Reachable({_targetX + (from.x - middle.x), from.y, _targetZ + (from.z - middle.z)});
    std::vector<Float3> path = clearances.Path(from, destination);
    path.insert(path.begin(), from);
    if (path.size() > 1)
    {
      longest = std::max(longest, RemainingLength(from, path, 1));
    }
    paths.push_back(std::move(path));
  }
  for (std::size_t member = 0; member < group.size(); ++member)
  {
    Ship& ship = m_ships[group[member]];
    if (paths[member].size() < 2)
    {
      EndMove(ship, Stance::Idle);
      continue;
    }
    const float length = RemainingLength(paths[member].front(), paths[member], 1);
    EndMove(ship, Stance::Moving);
    ship.path = std::move(paths[member]);
    ship.next = 1;
    ship.pace = longest > 0.0f ? cap * (length / longest) : cap;
    ship.limits.acceleration = acceleration;
    ship.limits.turnRate = turnRate;
  }
}

void Skirmish::Halt(std::span<const std::uint32_t> _ships, Stance _stance)
{
  for (const std::uint32_t id : _ships)
  {
    if (const std::optional<std::size_t> ship = WholeShip(id))
    {
      EndMove(m_ships[*ship], _stance);
    }
  }
}

void Skirmish::EndMove(Ship& _ship, Stance _stance) const noexcept
{
  _ship.stance = _stance;
  _ship.path.clear();
  _ship.limits = m_designs[_ship.design].limits;
  _ship.attackTarget = 0;
  _ship.engaged = false;
  _ship.station.reset();
}

Float3 Skirmish::Avoidance(std::size_t _ship, Float3 _course) const noexcept
{
  const Ship& self = m_ships[_ship];
  const ShipMotion& motion = self.motion;
  const float radius = m_designs[self.design].limits.radius;
  const Float3 right{_course.z, 0.0f, -_course.x};
  const Float3 velocity = VelocityOf(self);
  Float3 steer{0.0f, 0.0f, 0.0f};
  for (std::size_t index = 0; index < m_ships.size(); ++index)
  {
    const Ship& other = m_ships[index];
    if (index == _ship || m_entities[other.entity].detonation)
    {
      continue;
    }
    const Float3 apart{other.motion.position.x - motion.position.x, 0.0f, other.motion.position.z - motion.position.z};
    const float distance = NeuronCore::Length(apart);
    if (distance < 1.0e-3f)
    {
      continue;
    }
    const float closing = -NeuronCore::Dot(VelocityOf(other) - velocity, apart) / distance;
    const float reach = radius + m_designs[other.design].limits.radius + CLEARANCE_MARGIN + std::max(closing, 0.0f) * AVOID_SECONDS;
    if (distance >= reach)
    {
      continue;
    }
    const float weight = 1.0f - distance / reach;
    steer = steer - apart * (weight / distance);
    if (NeuronCore::Dot(apart, _course) > 0.0f)
    {
      steer = steer + (NeuronCore::Dot(apart, right) > 0.0f ? -right : right) * weight;
    }
  }
  return steer * AVOID_GAIN;
}

bool Skirmish::DestinationTaken(std::size_t _ship) const noexcept
{
  // Another ship halted within their spacing of the destination takes it, and this one halts within the spacing and
  // CLEARANCE_MARGIN more of it.
  const Ship& self = m_ships[_ship];
  const float radius = m_designs[self.design].limits.radius;
  const float remaining = RemainingLength(self.motion.position, self.path, self.next);
  for (std::size_t index = 0; index < m_ships.size(); ++index)
  {
    const Ship& other = m_ships[index];
    if (index == _ship || m_entities[other.entity].detonation || other.stance == Stance::Moving || other.stance == Stance::AttackMoving)
    {
      continue;
    }
    const float spacing = radius + m_designs[other.design].limits.radius + CLEARANCE_MARGIN;
    if (PlaneDistance(other.motion.position, self.path.back()) <= spacing && remaining <= spacing + CLEARANCE_MARGIN)
    {
      return true;
    }
  }
  return false;
}

bool Skirmish::Sees(std::uint8_t _side, const Entity& _entity) const noexcept
{
  if (_side == NeuronCore::OBSERVER_SIDE || _entity.side == _side)
  {
    return true;
  }
  return Covers(_side, _entity.position);
}

bool Skirmish::Covers(std::uint8_t _side, Float3 _point) const noexcept
{
  return std::ranges::any_of(m_entities,
                             [_side, _point](const Entity& _sensor)
                             {
                               return _sensor.side == _side && Intact(_sensor) && _sensor.sensorRangeUnits > 0.0f &&
                                      WithinRange(_sensor.position, _point, _sensor.sensorRangeUnits);
                             });
}

bool Skirmish::Intact(const Entity& _entity) noexcept
{
  return !_entity.detonation && !_entity.expired;
}

bool Skirmish::Fightable(const Entity& _entity) const noexcept
{
  return Intact(_entity) && !_entity.piece && _entity.side != 0 && m_designed[_entity.composite];
}

void Skirmish::Expire(std::uint64_t _worldTick)
{
  const std::uint64_t lifetime = TicksOf(DEBRIS_SECONDS, m_parameters.tickRate);
  for (Entity& entity : m_entities)
  {
    if (entity.detonation && !entity.expired && _worldTick >= entity.detonation->worldTick + lifetime)
    {
      entity.expired = true;
    }
  }
}

std::vector<SweptEntity> Skirmish::Swept() const
{
  // Every intact entity as the tick begins: debris is passed (G39).
  std::vector<SweptEntity> swept;
  for (std::size_t index = 0; index < m_entities.size(); ++index)
  {
    const Entity& entity = m_entities[index];
    if (!Intact(entity))
    {
      continue;
    }
    const VoxelBody& body = m_bodies[entity.composite];
    swept.push_back({static_cast<std::uint32_t>(index + 1), entity.side, m_designed[entity.composite], &body,
                     PlaceOf(body, entity.position, NeuronCore::RotationOf(entity.rotation)), EntityVelocity(index), entity.gone,
                     entity.damage, m_toughness[entity.composite]});
  }
  return swept;
}

std::vector<std::uint8_t> Skirmish::SeenBySides() const
{
  std::vector<std::uint8_t> seen(m_entities.size(), 0);
  for (std::size_t index = 0; index < m_entities.size(); ++index)
  {
    for (std::size_t side = 1; side <= m_sides.size(); ++side)
    {
      if (!m_entities[index].expired && Sees(static_cast<std::uint8_t>(side), m_entities[index]))
      {
        seen[index] = static_cast<std::uint8_t>(seen[index] | (1u << side));
      }
    }
  }
  return seen;
}

Float3 Skirmish::Muzzle(std::size_t _entity, std::size_t _weapon) const noexcept
{
  const Entity& entity = m_entities[_entity];
  const VoxelBody& body = m_bodies[entity.composite];
  return NeuronCore::TransformPoint(PlaceOf(body, entity.position, NeuronCore::RotationOf(entity.rotation)),
                                    m_profiles[entity.composite].weapons[_weapon].muzzle);
}

Float3 Skirmish::Facing(std::size_t _entity, std::size_t _weapon) const noexcept
{
  const Entity& entity = m_entities[_entity];
  const NeuronCore::Int3 facing = m_profiles[entity.composite].weapons[_weapon].facing;
  return NeuronCore::RotateVector(NeuronCore::RotationOf(entity.rotation),
                                  {static_cast<float>(facing.x), static_cast<float>(facing.y), static_cast<float>(facing.z)});
}

bool Skirmish::WithinReach(Float3 _point, std::size_t _target, float _rangeUnits) const
{
  // Every voxel's center lies within the sphere, so the sphere settles most cases, and the nearest voxel the rest.
  const Entity& target = m_entities[_target];
  const VoxelBody& body = m_bodies[target.composite];
  const float distance = NeuronCore::Length(target.position - _point);
  if (distance - body.Radius() > _rangeUnits)
  {
    return false;
  }
  if (distance + body.Radius() <= _rangeUnits)
  {
    return true;
  }
  const Float3 local = NeuronCore::InverseTransformPoint(PlaceOf(body, target.position, NeuronCore::RotationOf(target.rotation)), _point);
  return body.NearestVoxel(local, target.gone, _rangeUnits).has_value();
}

bool Skirmish::Bears(std::size_t _entity, std::size_t _weapon, std::size_t _target) const
{
  const Entity& entity = m_entities[_entity];
  if (!entity.weapons[_weapon].working)
  {
    return false;
  }
  const Float3 muzzle = Muzzle(_entity, _weapon);
  return NeuronCore::Dot(m_entities[_target].position - muzzle, Facing(_entity, _weapon)) > 0.0f &&
         WithinReach(muzzle, _target, m_profiles[entity.composite].weapons[_weapon].spec->rangeUnits);
}

Float3 Skirmish::VoxelPosition(std::size_t _entity, std::uint32_t _voxel) const noexcept
{
  const Entity& entity = m_entities[_entity];
  const VoxelBody& body = m_bodies[entity.composite];
  const NeuronCore::Int3 cell = body.Cells()[_voxel];
  return NeuronCore::TransformPoint(
    PlaceOf(body, entity.position, NeuronCore::RotationOf(entity.rotation)),
    {static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f, static_cast<float>(cell.z) + 0.5f});
}

Float3 Skirmish::EntityVelocity(std::size_t _entity) const noexcept
{
  const std::size_t ship = m_shipOf[_entity];
  return ship != NO_SHIP && Intact(m_entities[_entity]) ? VelocityOf(m_ships[ship]) : Float3{0.0f, 0.0f, 0.0f};
}

void Skirmish::Target(std::span<const SweptEntity> _swept, std::span<const std::uint8_t> _seen)
{
  for (Entity& entity : m_entities)
  {
    entity.underFire = false;
  }
  for (std::size_t index = 0; index < m_entities.size(); ++index)
  {
    Entity& entity = m_entities[index];
    if (!Intact(entity) || entity.weapons.empty())
    {
      continue;
    }
    const GameCore::CombatProfile& profile = m_profiles[entity.composite];
    const auto id = static_cast<std::uint32_t>(index + 1);
    const std::uint8_t side = entity.side;
    // A target is an entity of another side that combat fights and the side sees; a weapon reaches it within its range.
    const auto valid = [this, side, _seen](std::uint32_t _target)
    {
      return _target != 0 && _target <= m_entities.size() && Fightable(m_entities[_target - 1]) && m_entities[_target - 1].side != side &&
             ((_seen[_target - 1] >> side) & 1u) != 0u;
    };
    const auto reached = [this, &entity, &profile, index](std::size_t _target)
    {
      for (std::size_t weapon = 0; weapon < entity.weapons.size(); ++weapon)
      {
        if (entity.weapons[weapon].working && WithinReach(Muzzle(index, weapon), _target, profile.weapons[weapon].spec->rangeUnits))
        {
          return true;
        }
      }
      return false;
    };
    Ship* ship = m_shipOf[index] != NO_SHIP ? &m_ships[m_shipOf[index]] : nullptr;

    // The entity's target (G71): an attack's, while the side sees it; or another, kept while it stays in reach, and else
    // the nearest in reach, one whose line is clear of the side's own hulls first, and a tie by PcgHash.
    if (ship != nullptr && ship->stance == Stance::Attacking)
    {
      if (valid(ship->attackTarget))
      {
        entity.target = ship->attackTarget;
      }
      else
      {
        EndMove(*ship, Stance::Idle);
        entity.target = 0;
      }
    }
    if (ship == nullptr || ship->stance != Stance::Attacking)
    {
      if (!valid(entity.target) || !reached(entity.target - 1))
      {
        entity.target = 0;
        bool bestClear = false;
        float bestDistance = std::numeric_limits<float>::max();
        std::uint32_t bestDraw = 0;
        for (std::size_t candidate = 0; candidate < m_entities.size(); ++candidate)
        {
          const auto candidateId = static_cast<std::uint32_t>(candidate + 1);
          if (!valid(candidateId) || !reached(candidate))
          {
            continue;
          }
          bool clear = false;
          for (std::size_t weapon = 0; weapon < entity.weapons.size() && !clear; ++weapon)
          {
            clear = entity.weapons[weapon].working && !LineBlocked(Muzzle(index, weapon), m_entities[candidate].position, side, _swept);
          }
          const float distance = NeuronCore::Length(m_entities[candidate].position - entity.position);
          const std::uint32_t draw = Draw(m_parameters.seed, TARGET_STREAM, id, candidateId, 0);
          if (entity.target == 0 || (clear && !bestClear) ||
              (clear == bestClear && (distance < bestDistance || (distance == bestDistance && draw < bestDraw))))
          {
            entity.target = candidateId;
            bestClear = clear;
            bestDistance = distance;
            bestDraw = draw;
          }
        }
      }
      if (ship != nullptr && ship->stance == Stance::AttackMoving)
      {
        // An attack-move fights what it finds on its way, and takes its way anew once that is done.
        const bool engaged = entity.target != 0;
        if (engaged != ship->engaged)
        {
          ship->path.clear();
          ship->pathTick = 0;
          ship->station.reset();
        }
        ship->engaged = engaged;
      }
    }

    // Each weapon fires at the entity's target when it bears on it, and else at a target of its own, kept while it bears
    // and else the nearest that does.
    for (std::size_t weapon = 0; weapon < entity.weapons.size(); ++weapon)
    {
      WeaponState& state = entity.weapons[weapon];
      std::uint32_t chosen = 0;
      if (!state.working)
      {
        chosen = 0;
      }
      else if (entity.target != 0 && Bears(index, weapon, entity.target - 1))
      {
        chosen = entity.target;
      }
      else if (valid(state.target) && Bears(index, weapon, state.target - 1))
      {
        chosen = state.target;
      }
      else
      {
        float bestDistance = std::numeric_limits<float>::max();
        std::uint32_t bestDraw = 0;
        for (std::size_t candidate = 0; candidate < m_entities.size(); ++candidate)
        {
          const auto candidateId = static_cast<std::uint32_t>(candidate + 1);
          if (!valid(candidateId) || !Bears(index, weapon, candidate))
          {
            continue;
          }
          const float distance = NeuronCore::Length(m_entities[candidate].position - entity.position);
          const std::uint32_t draw = Draw(m_parameters.seed, TARGET_STREAM, id, candidateId, static_cast<std::uint32_t>(weapon + 1));
          if (chosen == 0 || distance < bestDistance || (distance == bestDistance && draw < bestDraw))
          {
            chosen = candidateId;
            bestDistance = distance;
            bestDraw = draw;
          }
        }
      }
      if (chosen != state.target)
      {
        state.target = chosen;
        state.aimed = false;
      }
      if (chosen != 0)
      {
        m_entities[chosen - 1].underFire = true;
      }
    }
  }
}

void Skirmish::Fire(std::uint64_t _worldTick, std::span<const SweptEntity> _swept, std::vector<Shot>& _beams,
                    std::vector<std::pair<std::size_t, std::size_t>>& _beamWeapons)
{
  const auto tickRate = static_cast<float>(m_parameters.tickRate);
  for (std::size_t index = 0; index < m_entities.size(); ++index)
  {
    Entity& entity = m_entities[index];
    if (!Intact(entity) || entity.weapons.empty())
    {
      continue;
    }
    const GameCore::CombatProfile& profile = m_profiles[entity.composite];
    const auto id = static_cast<std::uint32_t>(index + 1);
    for (std::size_t weapon = 0; weapon < entity.weapons.size(); ++weapon)
    {
      WeaponState& state = entity.weapons[weapon];
      const GameCore::WeaponSpec& spec = *profile.weapons[weapon].spec;
      const bool shell = spec.shot == GameCore::ShotKind::Shell;
      if (!state.working || state.target == 0 || (shell && _worldTick < state.readyTick))
      {
        continue;
      }
      const std::size_t target = state.target - 1;
      const Float3 muzzle = Muzzle(index, weapon);

      // The aim (G57): a voxel drawn from the target's silhouette as the weapon sees it, anew for every shell, and for a
      // beam anew only for a new target or once its line meets nothing of the target.
      if (!state.aimed)
      {
        const Entity& aimed = m_entities[target];
        const Float3 look = NeuronCore::UnrotateVector(NeuronCore::RotationOf(aimed.rotation), aimed.position - muzzle);
        const std::vector<std::uint32_t>& silhouette = m_profiles[aimed.composite].silhouettes[GameCore::SilhouetteDirection(look)];
        state.aim =
          silhouette.empty()
            ? 0
            : silhouette[Draw(m_parameters.seed, AIM_STREAM, id, static_cast<std::uint32_t>(weapon), state.shots) % silhouette.size()];
        state.aimed = true;
      }
      const Float3 point = VoxelPosition(target, state.aim);
      const Float3 facing = Facing(index, weapon);
      if (shell)
      {
        // A shell leads (G58), within the weapon's arc, along a line its own side's hulls leave clear (G71).
        const std::optional<Lead> lead = LeadShot(muzzle, point, EntityVelocity(target), spec.shellSpeedUnitsPerSecond);
        if (!lead || NeuronCore::Dot(lead->direction, facing) <= 0.0f ||
            LineBlocked(muzzle, muzzle + lead->direction * (spec.shellSpeedUnitsPerSecond * lead->seconds), entity.side, _swept))
        {
          continue;
        }
        m_shells.push_back(
          {muzzle, lead->direction * spec.shellSpeedUnitsPerSecond, entity.side, id, state.target, spec.damage, spec.reachUnits,
           static_cast<std::uint32_t>(TicksOf(spec.rangeUnits / spec.shellSpeedUnitsPerSecond, m_parameters.tickRate)), false});
        state.readyTick = _worldTick + TicksOf(spec.intervalSeconds, m_parameters.tickRate);
        state.aimed = false;
        ++state.shots;
        ++m_records[index].shellsFired;
        ++m_records[target].shellsAt;
      }
      else
      {
        // A beam hits at once, along the line to its aim and on to its range.
        const float length = NeuronCore::Length(point - muzzle);
        if (!(length > 0.0f))
        {
          continue;
        }
        const Float3 direction = (point - muzzle) * (1.0f / length);
        if (NeuronCore::Dot(direction, facing) <= 0.0f || LineBlocked(muzzle, point, entity.side, _swept))
        {
          continue;
        }
        _beams.push_back(
          {muzzle, direction * spec.rangeUnits, 0.0f, spec.damage / tickRate, spec.reachUnits, id, entity.side, state.target});
        _beamWeapons.emplace_back(index, weapon);
        ++state.shots;
        ++m_records[index].beamTicks;
      }
    }
  }
}

std::vector<Skirmish::Removal> Skirmish::Resolve(std::span<const SweptEntity> _swept, std::span<const Shot> _beams,
                                                 std::span<const std::pair<std::size_t, std::size_t>> _beamWeapons, float _seconds)
{
  // Every shot swept against the entities as the tick began (G72): the beams, then the shells in the order they were
  // fired. What each spends is kept apart until all are swept.
  std::vector<SpentDamage> spent;
  m_beams.clear();
  for (std::size_t beam = 0; beam < _beams.size(); ++beam)
  {
    const Shot& shot = _beams[beam];
    const ShotOutcome outcome = SweepShot(shot, _swept, spent);
    if (!outcome.damagedTarget)
    {
      m_entities[_beamWeapons[beam].first].weapons[_beamWeapons[beam].second].aimed = false;
    }
    m_beams.push_back({shot.origin, shot.origin + shot.displacement * static_cast<float>(outcome.stopFraction), shot.side});
  }
  for (Shell& shell : m_shells)
  {
    const Shot shot{shell.position, shell.velocity * _seconds, _seconds, shell.damage, shell.reachUnits, shell.shooter, shell.side,
                    shell.target};
    const ShotOutcome outcome = SweepShot(shot, _swept, spent);
    if (outcome.damagedTarget && !shell.hit)
    {
      shell.hit = true;
      ++m_records[shell.target - 1].shellsHit;
    }
    if (outcome.stopped)
    {
      shell.ticksLeft = 0;
    }
    else
    {
      shell.position = shell.position + shell.velocity * _seconds;
      --shell.ticksLeft;
    }
  }
  std::erase_if(m_shells, [](const Shell& _shell) { return _shell.ticksLeft == 0; });

  // The damage summed, in the order it was spent, and a voxel gone once it has taken its toughness.
  std::vector<Removal> removals;
  for (const SpentDamage& spend : spent)
  {
    const std::size_t index = _swept[spend.entity].id - 1;
    Entity& entity = m_entities[index];
    const std::vector<float>& toughness = m_toughness[entity.composite];
    if (entity.damage.empty())
    {
      entity.damage.assign(toughness.size(), 0.0f);
    }
    entity.damage[spend.voxel] += spend.damage;
    if ((spend.destroyed || entity.damage[spend.voxel] >= toughness[spend.voxel]) && !IsGone(entity.gone, spend.voxel))
    {
      if (entity.gone.empty())
      {
        entity.gone.assign((toughness.size() + 7) / 8, 0);
      }
      entity.gone[spend.voxel / 8] = static_cast<std::uint8_t>(entity.gone[spend.voxel / 8] | (1u << (spend.voxel % 8)));
      if (m_designed[entity.composite])
      {
        --entity.remaining[m_profiles[entity.composite].voxels[spend.voxel].component];
      }
      ++m_records[index].voxelsLost;
      const auto removal = std::ranges::find(removals, index, &Removal::entity);
      if (removal == removals.end())
      {
        removals.push_back({index, {spend.voxel}});
      }
      else
      {
        removal->voxels.push_back(spend.voxel);
      }
    }
  }
  std::ranges::sort(removals, {}, &Removal::entity);
  return removals;
}

void Skirmish::Suffer(std::span<const Removal> _removals, std::uint64_t _worldTick)
{
  // Each entity that lost voxels, once (G72): a failed command module or reactor loses it; else what is cut off from its
  // command module breaks away as debris of its own, and its other modules work or fail by what remains of them.
  struct Piece
  {
    std::size_t parent;
    std::vector<std::uint8_t> gone;
  };
  std::vector<Piece> pieces;
  for (const Removal& removal : _removals)
  {
    const std::size_t index = removal.entity;
    Entity& entity = m_entities[index];
    if (!Intact(entity) || !m_designed[entity.composite])
    {
      continue;
    }
    const GameCore::CombatProfile& profile = m_profiles[entity.composite];
    const auto failed = [&profile, &entity](std::size_t _component)
    {
      const GameCore::CombatComponent& component = profile.components[_component];
      return component.voxelCount > 0 &&
             static_cast<float>(entity.remaining[_component]) < GameCore::FAIL_SHARE * static_cast<float>(component.voxelCount);
    };
    bool command = false;
    bool reactor = false;
    for (std::size_t component = 0; component < profile.components.size(); ++component)
    {
      command = command || (profile.components[component].module == GameCore::ModuleKind::Command && failed(component));
      reactor = reactor || (profile.components[component].module == GameCore::ModuleKind::Reactor && failed(component));
    }
    if (command || reactor)
    {
      m_losses.push_back({static_cast<std::uint32_t>(index + 1), _worldTick, reactor ? LossKind::Reactor : LossKind::Command});
      Lose(index, _worldTick);
      continue;
    }

    // What still connects to the command module, face to face, and what is cut off: searched through the whole only when
    // what remains beside the removed voxels does not connect up near them.
    const VoxelBody& body = m_bodies[entity.composite];
    if (!CutsNothing(body, entity.gone, removal.voxels))
    {
      const std::size_t voxels = body.Cells().size();
      std::vector<std::uint8_t> connected(voxels, 0);
      std::vector<std::uint32_t> open;
      for (const GameCore::CombatComponent& component : profile.components)
      {
        if (component.module == GameCore::ModuleKind::Command)
        {
          for (std::uint32_t voxel = component.firstVoxel; voxel < component.firstVoxel + component.voxelCount; ++voxel)
          {
            if (!IsGone(entity.gone, voxel))
            {
              connected[voxel] = 1;
              open.push_back(voxel);
            }
          }
        }
      }
      while (!open.empty())
      {
        const std::uint32_t voxel = open.back();
        open.pop_back();
        for (const std::uint32_t neighbor : body.FaceNeighbors(voxel))
        {
          if (neighbor != NeuronCore::NO_VOXEL && connected[neighbor] == 0 && !IsGone(entity.gone, neighbor))
          {
            connected[neighbor] = 1;
            open.push_back(neighbor);
          }
        }
      }
      Piece piece{index, std::vector<std::uint8_t>((voxels + 7) / 8, 0xFF)};
      std::uint32_t cut = 0;
      for (std::uint32_t voxel = 0; voxel < voxels; ++voxel)
      {
        if (!IsGone(entity.gone, voxel) && connected[voxel] == 0)
        {
          entity.gone[voxel / 8] = static_cast<std::uint8_t>(entity.gone[voxel / 8] | (1u << (voxel % 8)));
          piece.gone[voxel / 8] = static_cast<std::uint8_t>(piece.gone[voxel / 8] & ~(1u << (voxel % 8)));
          --entity.remaining[profile.voxels[voxel].component];
          ++m_records[index].voxelsLost;
          ++cut;
        }
      }
      if (cut >= PIECE_MIN_VOXELS)
      {
        if (voxels % 8 != 0)
        {
          piece.gone.back() = static_cast<std::uint8_t>(piece.gone.back() & ((1u << (voxels % 8)) - 1u));
        }
        pieces.push_back(std::move(piece));
      }
    }

    // The other modules: a failed weapon holds its fire, and failed engines take their thrust with them. A sensor senses
    // while its ship lives: a ship blinded with no marker to say so would read as a fault, and the markers come after
    // the MVP (Design/MvpPlan.md §2.2 on G49).
    for (std::size_t weapon = 0; weapon < entity.weapons.size(); ++weapon)
    {
      entity.weapons[weapon].working = !failed(profile.weapons[weapon].component);
    }
    std::uint32_t engines = 0;
    std::uint32_t workingEngines = 0;
    for (std::size_t component = 0; component < profile.components.size(); ++component)
    {
      if (profile.components[component].module == GameCore::ModuleKind::Engine)
      {
        ++engines;
        workingEngines += failed(component) ? 0u : 1u;
      }
    }
    if (m_shipOf[index] != NO_SHIP && engines > 0)
    {
      m_ships[m_shipOf[index]].thrustShare = static_cast<float>(workingEngines) / static_cast<float>(engines);
    }
  }

  // Each piece cut off becomes debris of its own at once: the parent's composite, side and place, masked to the piece.
  for (Piece& piece : pieces)
  {
    const Entity& parent = m_entities[piece.parent];
    const auto id = static_cast<std::uint32_t>(m_entities.size() + 1);
    Entity debris{parent.composite,
                  parent.side,
                  parent.position,
                  parent.rotation,
                  0.0f,
                  NeuronCore::DetonationEvent{id, NextDetonationSeed(), _worldTick, EntityVelocity(piece.parent)},
                  std::move(piece.gone),
                  {},
                  {},
                  0,
                  {},
                  false,
                  true,
                  false};
    m_entities.push_back(std::move(debris));
    m_shipOf.push_back(NO_SHIP);
    m_records.push_back({});
  }
}

void Skirmish::Lose(std::size_t _entity, std::uint64_t _worldTick)
{
  // The entity freezes where it is, and its velocity joins its debris's launch, as the sector's does. A ship forgets its
  // order.
  const Float3 velocity = EntityVelocity(_entity);
  if (m_shipOf[_entity] != NO_SHIP)
  {
    EndMove(m_ships[m_shipOf[_entity]], Stance::Idle);
  }
  Entity& entity = m_entities[_entity];
  entity.target = 0;
  for (WeaponState& weapon : entity.weapons)
  {
    weapon.target = 0;
    weapon.aimed = false;
  }
  entity.detonation = NeuronCore::DetonationEvent{static_cast<std::uint32_t>(_entity + 1), NextDetonationSeed(), _worldTick, velocity};
}

std::uint32_t Skirmish::NextDetonationSeed() noexcept
{
  const std::uint32_t seed = NeuronCore::PcgHash(m_detonations * 256u + DETONATION_STREAM + m_parameters.seed * SEED_STEP);
  ++m_detonations;
  return seed;
}

ShipClass Skirmish::FlyingLimits(const Ship& _ship) noexcept
{
  ShipClass limits = _ship.limits;
  limits.acceleration *= _ship.thrustShare;
  limits.turnRate *= _ship.thrustShare;
  return limits;
}

float Skirmish::JinkSpeed(const Ship& _ship, std::uint64_t _worldTick) const noexcept
{
  const Entity& entity = m_entities[_ship.entity];
  if (!m_parameters.jinking || !entity.underFire)
  {
    return 0.0f;
  }
  const auto period = static_cast<std::uint32_t>(_worldTick / TicksOf(JINK_SECONDS, m_parameters.tickRate));
  const auto id = static_cast<std::uint32_t>(_ship.entity + 1);
  const float speed = (2.0f * Share(Draw(m_parameters.seed, JINK_STREAM, id, period, 0)) - 1.0f) * JINK_SPEED;
  if (_ship.station)
  {
    const Float3 right{_ship.motion.forward.z, 0.0f, -_ship.motion.forward.x};
    const float off = NeuronCore::Dot(_ship.motion.position - *_ship.station, right);
    if (std::abs(off) > JINK_LEASH)
    {
      return off > 0.0f ? -std::abs(speed) : std::abs(speed);
    }
  }
  return speed;
}

Float3 Skirmish::VelocityOf(const Ship& _ship) noexcept
{
  return Velocity(_ship.motion) + _ship.slide;
}

void Skirmish::Slide(Ship& _ship, std::uint64_t _worldTick, float _seconds) const noexcept
{
  const Float3 right = PlaneDirection({_ship.motion.forward.z, 0.0f, -_ship.motion.forward.x}, {1.0f, 0.0f, 0.0f});
  const Float3 change = right * JinkSpeed(_ship, _worldTick) - _ship.slide;
  const float length = NeuronCore::Length(change);
  const float most = FlyingLimits(_ship).acceleration * GameCore::VECTORED_THRUST_SHARE * _seconds;
  _ship.slide = _ship.slide + (length > most ? change * (most / length) : change);
  _ship.motion.position = _ship.motion.position + _ship.slide * _seconds;
}

bool Skirmish::InAttackRange(const Ship& _ship, std::size_t _target) const
{
  // A ship with no working weapon closes no further.
  const Entity& entity = m_entities[_ship.entity];
  const GameCore::CombatProfile& profile = m_profiles[entity.composite];
  for (std::size_t weapon = 0; weapon < entity.weapons.size(); ++weapon)
  {
    if (entity.weapons[weapon].working &&
        !WithinReach(Muzzle(_ship.entity, weapon), _target, ATTACK_RANGE_SHARE * profile.weapons[weapon].spec->rangeUnits))
    {
      return false;
    }
  }
  return true;
}

void Skirmish::Bear(Ship& _ship, std::size_t _target, float _seconds)
{
  // It turns to hold its target at its design's bearing (G33), where it began to bear, which its jink slides about.
  if (!_ship.station)
  {
    _ship.station = _ship.motion.position;
  }
  const Float3 toward = PlaneDirection(m_entities[_target].position - _ship.motion.position, _ship.motion.forward);
  Steer(_ship.motion, TurnOnPlane(toward, -m_profiles[m_entities[_ship.entity].composite].bearingRadians), 0.0f, FlyingLimits(_ship),
        _seconds);
}

void Skirmish::Close(Ship& _ship, std::size_t _target, Float3 _avoid, std::uint64_t _worldTick, float _seconds)
{
  // Toward the target by the clearances, the path found anew every REPATH_SECONDS as the target moves.
  _ship.station.reset();
  const Clearances& clearances = m_designs[_ship.design].clearances;
  if (_ship.path.size() < 2 || _worldTick >= _ship.pathTick + TicksOf(REPATH_SECONDS, m_parameters.tickRate))
  {
    std::vector<Float3> path = clearances.Path(_ship.motion.position, clearances.Reachable(m_entities[_target].position));
    path.insert(path.begin(), _ship.motion.position);
    _ship.path = std::move(path);
    _ship.next = 1;
    _ship.pathTick = _worldTick;
  }
  const ShipClass limits = FlyingLimits(_ship);
  if (_ship.path.size() < 2)
  {
    Brake(_ship.motion, limits, _seconds);
    return;
  }
  _ship.next = FlyPath(_ship.motion, _ship.path, _ship.next, limits.maxSpeed, _avoid, limits, _seconds);
}

void Skirmish::Fly(std::uint64_t _worldTick, float _seconds)
{
  // Each flying ship's avoidance, from where every ship stands as the flight begins; then each whole ship's tick, kept out
  // of every keep-out (ADR-033). A ship at rest and level with nothing to do is left exactly as it stands.
  std::vector<Float3> avoidance(m_ships.size(), Float3{0.0f, 0.0f, 0.0f});
  for (std::size_t index = 0; index < m_ships.size(); ++index)
  {
    const Ship& ship = m_ships[index];
    if (!Intact(m_entities[ship.entity]))
    {
      continue;
    }
    if (ship.path.size() > 1 && ship.next < ship.path.size())
    {
      avoidance[index] = Avoidance(index, PlaneDirection(ship.path[ship.next] - ship.motion.position, ship.motion.forward));
    }
    else if (ship.motion.speed > 0.0f)
    {
      avoidance[index] = Avoidance(index, ship.motion.forward);
    }
  }
  for (std::size_t index = 0; index < m_ships.size(); ++index)
  {
    Ship& ship = m_ships[index];
    Entity& entity = m_entities[ship.entity];
    if (!Intact(entity))
    {
      continue;
    }
    const ShipDesign& design = m_designs[ship.design];
    const ShipClass limits = FlyingLimits(ship);
    const auto fight = [this, &ship, &avoidance, index, _worldTick, _seconds](std::size_t _target)
    {
      if (InAttackRange(ship, _target))
      {
        ship.path.clear();
        Bear(ship, _target, _seconds);
      }
      else
      {
        Close(ship, _target, avoidance[index], _worldTick, _seconds);
      }
    };
    switch (ship.stance)
    {
    case Stance::Moving:
    case Stance::AttackMoving:
      if (ship.stance == Stance::AttackMoving && ship.engaged && entity.target != 0)
      {
        fight(entity.target - 1);
        break;
      }
      if (ship.path.size() < 2 && ship.stance == Stance::Moving)
      {
        EndMove(ship, Stance::Idle);
        Brake(ship.motion, limits, _seconds);
        break;
      }
      if (ship.path.size() < 2)
      {
        // An attack-move takes its way anew once it is done fighting.
        std::vector<Float3> path = design.clearances.Path(
          ship.motion.position, design.clearances.Reachable({ship.destination.x, ship.motion.position.y, ship.destination.z}));
        path.insert(path.begin(), ship.motion.position);
        if (path.size() < 2)
        {
          EndMove(ship, Stance::Idle);
          Brake(ship.motion, limits, _seconds);
          break;
        }
        ship.path = std::move(path);
        ship.next = 1;
        ship.pace = limits.maxSpeed;
      }
      ship.next = FlyPath(ship.motion, ship.path, ship.next, ship.pace, avoidance[index], limits, _seconds);
      if (RemainingLength(ship.motion.position, ship.path, ship.next) <= ARRIVAL_RADIUS || DestinationTaken(index))
      {
        EndMove(ship, Stance::Idle);
      }
      break;
    case Stance::Attacking:
      fight(ship.attackTarget - 1);
      break;
    case Stance::Idle:
    case Stance::Holding:
    {
      // It bears on its target where it stands; without one it brakes, and under fire it jinks about where it stands.
      const bool jinking = m_parameters.jinking && entity.underFire;
      if (entity.target != 0)
      {
        Bear(ship, entity.target - 1, _seconds);
      }
      else if (jinking || ship.motion.speed > 0.0f || !IsLevel(ship.motion) || ship.slide.x != 0.0f || ship.slide.z != 0.0f)
      {
        if (jinking && !ship.station)
        {
          ship.station = ship.motion.position;
        }
        Brake(ship.motion, limits, _seconds);
      }
      else
      {
        ship.station.reset();
        continue;
      }
      break;
    }
    }
    Slide(ship, _worldTick, _seconds);
    ship.motion.position = design.clearances.KeptOut(ship.motion.position);
    entity.position = ship.motion.position;
    entity.rotation = NeuronCore::QuaternionOf(ShipRotation(ship.motion));
  }
}

} // namespace GameLogic
