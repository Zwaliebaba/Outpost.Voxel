#include "pch.h"

#include "Skirmish.h"

#include "Sector.h"

#include "Catalogue.h"
#include "Design.h"
#include "DesignComposite.h"
#include "Orders.h"
#include "Profile.h"
#include "SkirmishLayout.h"
#include "WelcomeNames.h"

#include "Composite.h"
#include "Hash.h"
#include "NvfImport.h"
#include "NvfModel.h"
#include "Quaternion.h"
#include "VoxModel.h"

#include <algorithm>
#include <format>
#include <limits>
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

// How a moving ship keeps apart from another whole ship (Design/ADR/ADR-033): within their two spheres and CLEARANCE_MARGIN,
// and further by the distance they close in this many seconds, it steers away from the other and, when the other lies
// ahead, to the side away from it, the right when dead ahead, the harder as they near, up to this gain against its aim.
constexpr float AVOID_SECONDS = 1.0f;
constexpr float AVOID_GAIN = 1.5f;

// An entity's index in the ships when it is none.
constexpr std::size_t NO_SHIP = std::numeric_limits<std::size_t>::max();

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
                                   skirmish->m_designs[design].limits});
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

void Skirmish::Advance(std::uint64_t /*_worldTick*/)
{
  // Each moving ship's avoidance, from where every ship stands as the tick begins; then each whole ship's tick, kept out
  // of every keep-out (ADR-033). A ship at rest and level is left exactly as it stands. Debris lasts until it is restored.
  const float seconds = 1.0f / static_cast<float>(m_parameters.tickRate);
  std::vector<Float3> avoidance(m_ships.size(), Float3{0.0f, 0.0f, 0.0f});
  for (std::size_t index = 0; index < m_ships.size(); ++index)
  {
    if (m_ships[index].stance == Stance::Moving && !m_entities[m_ships[index].entity].detonation)
    {
      avoidance[index] = Avoidance(index);
    }
  }
  for (std::size_t index = 0; index < m_ships.size(); ++index)
  {
    Ship& ship = m_ships[index];
    Entity& entity = m_entities[ship.entity];
    if (entity.detonation)
    {
      continue;
    }
    const ShipDesign& design = m_designs[ship.design];
    if (ship.stance == Stance::Moving)
    {
      ship.next = FlyPath(ship.motion, ship.path, ship.next, ship.pace, avoidance[index], ship.limits, seconds);
      if (RemainingLength(ship.motion.position, ship.path, ship.next) <= ARRIVAL_RADIUS || DestinationTaken(index))
      {
        EndMove(ship, Stance::Idle);
      }
    }
    else if (ship.motion.speed > 0.0f || !IsLevel(ship.motion))
    {
      Brake(ship.motion, design.limits, seconds);
    }
    else
    {
      continue;
    }
    ship.motion.position = design.clearances.KeptOut(ship.motion.position);
    entity.position = ship.motion.position;
    entity.rotation = NeuronCore::QuaternionOf(ShipRotation(ship.motion));
  }
}

void Skirmish::Detonate(std::uint32_t _entity, std::uint64_t _worldTick)
{
  if (_entity == 0 || _entity > m_entities.size() || m_entities[_entity - 1].detonation)
  {
    return;
  }

  // The entity freezes where it is, and its velocity joins its debris's launch, as the sector's does. A ship forgets its
  // order.
  Float3 velocity{0.0f, 0.0f, 0.0f};
  if (const std::optional<std::size_t> ship = WholeShip(_entity))
  {
    velocity = Velocity(m_ships[*ship].motion);
    EndMove(m_ships[*ship], Stance::Idle);
  }
  const std::uint32_t seed = NeuronCore::PcgHash(m_detonations * 256u + DETONATION_STREAM + m_parameters.seed * SEED_STEP);
  m_entities[_entity - 1].detonation = NeuronCore::DetonationEvent{_entity, seed, _worldTick, velocity};
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
  }
}

void Skirmish::Describe(NeuronCore::Snapshot& _snapshot, std::uint8_t _side) const
{
  // Worked out afresh from every entity against every intact entity of the side, so a detonation or a restore changes
  // what the side sees on the tick it lands. A detonation goes with its entity. A side learns the orders of its own ships
  // that move or hold, and the observer every side's.
  const std::size_t first = _snapshot.entities.size();
  std::vector<GameCore::ShipOrderState> states;
  for (std::size_t index = 0; index < m_entities.size(); ++index)
  {
    const Entity& entity = m_entities[index];
    if (!Sees(_side, entity))
    {
      continue;
    }
    const auto id = static_cast<std::uint32_t>(index + 1);
    const std::optional<std::size_t> ship = WholeShip(id);
    const Float3 velocity = ship ? Velocity(m_ships[*ship].motion) : Float3{0.0f, 0.0f, 0.0f};
    _snapshot.entities.push_back({id, entity.composite, entity.side, entity.position, entity.rotation, velocity});
    if (ship && (_side == NeuronCore::OBSERVER_SIDE || entity.side == _side))
    {
      const Ship& whole = m_ships[*ship];
      if (whole.stance == Stance::Moving)
      {
        states.push_back({id, GameCore::ShipState::Moving, whole.path.back().x, whole.path.back().z});
      }
      else if (whole.stance == Stance::Holding)
      {
        states.push_back({id, GameCore::ShipState::Holding, 0.0f, 0.0f});
      }
    }
  }
  _snapshot.payload = GameCore::EncodeOrderStates(states);
  for (std::size_t described = first; described < _snapshot.entities.size(); ++described)
  {
    const Entity& entity = m_entities[_snapshot.entities[described].id - 1];
    if (entity.detonation)
    {
      _snapshot.detonations.push_back(*entity.detonation);
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
    ship.stance = Stance::Moving;
    ship.path = std::move(paths[member]);
    ship.next = 1;
    ship.pace = longest > 0.0f ? cap * (length / longest) : cap;
    ship.limits = m_designs[ship.design].limits;
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
}

Float3 Skirmish::Avoidance(std::size_t _ship) const noexcept
{
  // Ahead and to either side are the ship's course's, toward the corner it steers for, which a ship turning where it
  // stands keeps.
  const Ship& self = m_ships[_ship];
  const ShipMotion& motion = self.motion;
  const float radius = m_designs[self.design].limits.radius;
  const Float3 toward = self.path[self.next] - motion.position;
  const float towardLength = std::sqrt(toward.x * toward.x + toward.z * toward.z);
  const Float3 course = towardLength > 1.0e-3f ? Float3{toward.x / towardLength, 0.0f, toward.z / towardLength} : motion.forward;
  const Float3 right{course.z, 0.0f, -course.x};
  const Float3 velocity = Velocity(motion);
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
    const float closing = -NeuronCore::Dot(Velocity(other.motion) - velocity, apart) / distance;
    const float reach = radius + m_designs[other.design].limits.radius + CLEARANCE_MARGIN + std::max(closing, 0.0f) * AVOID_SECONDS;
    if (distance >= reach)
    {
      continue;
    }
    const float weight = 1.0f - distance / reach;
    steer = steer - apart * (weight / distance);
    if (NeuronCore::Dot(apart, course) > 0.0f)
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
    if (index == _ship || m_entities[other.entity].detonation || other.stance == Stance::Moving)
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
  return std::ranges::any_of(m_entities,
                             [_side, &_entity](const Entity& _sensor)
                             {
                               return _sensor.side == _side && !_sensor.detonation && _sensor.sensorRangeUnits > 0.0f &&
                                      WithinRange(_sensor.position, _entity.position, _sensor.sensorRangeUnits);
                             });
}

} // namespace GameLogic
