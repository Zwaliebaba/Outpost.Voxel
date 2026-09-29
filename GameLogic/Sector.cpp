#include "pch.h"

#include "Sector.h"

#include "Composite.h"
#include "Hash.h"
#include "Lighting.h"
#include "NvfModel.h"
#include "Quaternion.h"
#include "VoxModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <optional>
#include <utility>

namespace GameLogic
{
namespace
{

using NeuronCore::Float3;

constexpr float RADIANS_PER_DEGREE = std::numbers::pi_v<float> / 180.0f;
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
constexpr Float3 WORLD_UP{0.0f, 1.0f, 0.0f};

// The models the world places, in the manifest's order. Each is read from <name>.nvf (Design/Archive/SpaceScene.md §6.2,
// Design/ADR/ADR-028).
constexpr std::array<const char*, MODEL_COUNT> MODEL_NAMES{"MilitaryStation", "CapitalShip", "Frigate"};

// §5.2: an orbit's radius is 1.3 to 2 keep-out radii, in a plane tilted up to 45 degrees from the horizontal, flown one
// or two laps. It is capped to stay this far clear of the next station's keep-out, and a transit stays this far clear of
// every station's.
constexpr float MIN_ORBIT = 1.3f;
constexpr float MAX_ORBIT = 2.0f;
constexpr float MAX_ORBIT_TILT = 45.0f * RADIANS_PER_DEGREE;
constexpr float ORBIT_CLEARANCE = 25.0f;
constexpr float TRANSIT_CLEARANCE = 25.0f;

// §5.2: stations are placed in a disk of this many spacings of radius for each square root of their number, and this
// many spacings above and below the middle, rejecting a place nearer than the spacing to another; so many tries each.
constexpr float REGION_RADIUS = 1.0f;
constexpr float REGION_HEIGHT = 0.25f;
constexpr std::uint32_t PLACEMENT_TRIES = 1000;

// §5.2: frigates fly in flights of two to four, and capital ships alone; a route goes through two to four stations.
constexpr std::uint32_t MIN_FLIGHT = 2;
constexpr std::uint32_t MAX_FLIGHT = 4;
constexpr std::uint32_t MIN_ROUTE_STATIONS = 2;
constexpr std::uint32_t MAX_ROUTE_STATIONS = 4;

// Indices of a flight's draws, per station of its route.
constexpr std::uint32_t DRAWS_PER_FLIGHT = 8;

// §5.3, ADR-017: the classes' defaults. The speeds are units a second; a wingman flies between half its cruise and a
// quarter more, to let its slot come up to it or to catch up with it.
constexpr float CAPITAL_SHIP_CRUISE = 20.0f;
constexpr float CAPITAL_SHIP_ACCELERATION = 5.0f;
constexpr float CAPITAL_SHIP_TURN_RATE = 8.0f * RADIANS_PER_DEGREE;
constexpr float CAPITAL_SHIP_BANK_LIMIT = 15.0f * RADIANS_PER_DEGREE;
constexpr float CAPITAL_SHIP_BANK_RATE = 10.0f * RADIANS_PER_DEGREE;
constexpr float FRIGATE_CRUISE = 60.0f;
constexpr float FRIGATE_ACCELERATION = 30.0f;
constexpr float FRIGATE_TURN_RATE = 45.0f * RADIANS_PER_DEGREE;
constexpr float FRIGATE_BANK_LIMIT = 45.0f * RADIANS_PER_DEGREE;
constexpr float FRIGATE_BANK_RATE = 60.0f * RADIANS_PER_DEGREE;
constexpr float WINGMAN_LOW_SPEED = 0.5f;
constexpr float WINGMAN_TOP_SPEED = 1.25f;

// §12.1 and §11.4: the lighting's defaults, as the owner tuned them by eye on 2026-09-29, and a placeholder for the sky's
// until S-M5 tunes it: a sun from the station's _angle of 50 and 50 degrees at 0.932, a dim hemisphere brighter above than
// below, a sun 0.27 degrees across, and the galaxy's plane turned 60 degrees about x.
constexpr float SUN_DEGREES = 50.0f;
constexpr float SUN_RADIANCE = 0.932f;
constexpr float SUN_ANGULAR_RADIUS = 0.27f * RADIANS_PER_DEGREE;
constexpr float AMBIENT_UPPER = 0.167f;
constexpr float AMBIENT_LOWER = 0.090f;
constexpr NeuronCore::Quaternion GALACTIC_PLANE{0.5f, 0.0f, 0.0f, 0.8660254f};

// The world's randomness (§5.2): PcgHash of an index and a stream, offset by the seed as a detonation offsets its
// voxels' (Design/ADR/ADR-014), so that the same seed gives the same world.
constexpr std::uint32_t SEED_STEP = 0x9E3779B9u;
constexpr std::uint32_t STREAMS = 16;

enum class Stream : std::uint32_t
{
  StationX,
  StationY,
  StationZ,
  StationTurn,
  FlightSize,
  RouteLength,
  RouteShuffle,
  OrbitTilt,
  OrbitTiltAxis,
  OrbitRadius,
  OrbitLaps,
  OrbitDirection,
  RouteStart,
  Detonation
};

class WorldRandom
{
public:
  explicit WorldRandom(std::uint32_t _seed) noexcept
    : m_seed(_seed)
  {
  }

  [[nodiscard]] std::uint32_t Bits(std::uint32_t _index, Stream _stream) const noexcept
  {
    return NeuronCore::PcgHash(_index * STREAMS + static_cast<std::uint32_t>(_stream) + m_seed * SEED_STEP);
  }

  // In [0, 1), from the top 24 bits, which a float holds exactly.
  [[nodiscard]] float Unit(std::uint32_t _index, Stream _stream) const noexcept
  {
    return static_cast<float>(Bits(_index, _stream) >> 8u) * (1.0f / 16777216.0f);
  }

private:
  std::uint32_t m_seed;
};

[[nodiscard]] std::unexpected<SectorError> Refuse(SectorRefusal _refusal, std::string _detail)
{
  return std::unexpected(SectorError{_refusal, std::move(_detail)});
}

// A model's measure: the sphere about the center of its occupied box, and its file's hash.
struct ModelMeasure
{
  float radius;
  std::uint64_t hash;
};

[[nodiscard]] std::expected<ModelMeasure, SectorError> MeasureModel(const std::filesystem::path& _path, const std::string& _file)
{
  const auto bytes = NeuronCore::ReadNvfFile(_path);
  if (!bytes)
  {
    return Refuse(SectorRefusal::ModelNotLoaded, _file + ": " + NeuronCore::NvfErrorName(bytes.error()));
  }
  const auto model = NeuronCore::ParseNvfModel(*bytes);
  if (!model)
  {
    return Refuse(SectorRefusal::ModelNotLoaded, _file + ": " + NeuronCore::NvfErrorName(model.error()));
  }

  const std::optional<NeuronCore::VoxelBounds> bounds = NeuronCore::OccupiedBounds(NeuronCore::FlattenNvfModel(*model));
  if (!bounds)
  {
    return Refuse(SectorRefusal::ModelNotLoaded, _file + ": holds no visible voxel");
  }
  const NeuronCore::Int3 size = bounds->upper - bounds->lower;
  const Float3 extent{static_cast<float>(size.x), static_cast<float>(size.y), static_cast<float>(size.z)};
  return ModelMeasure{0.5f * NeuronCore::Length(extent), NeuronCore::Fnv1aHash64(*bytes)};
}

// The normal of an orbit about _station, which its route reaches from _previous and leaves toward _next (§5.2). Its plane
// holds the direction the route passes the station in, so that the transits join the orbit nearly tangentially, and is
// turned about that direction by _share, from -1 to 1, of the turn that tilts it MAX_ORBIT_TILT from the horizontal. A
// route through one station passes it in no direction, and its orbit tilts by _share of MAX_ORBIT_TILT about the
// horizontal axis at _heading. The layout keeps every direction between stations within 27 degrees of the horizontal
// (REGION_HEIGHT), so a plane that holds one can always be tilted less than MAX_ORBIT_TILT.
[[nodiscard]] Float3 OrbitNormal(Float3 _previous, Float3 _station, Float3 _next, float _share, float _heading) noexcept
{
  const Float3 in = _station - _previous;
  const Float3 out = _next - _station;
  Float3 through = in + out * (NeuronCore::Dot(in, out) >= 0.0f ? 1.0f : -1.0f);
  if (NeuronCore::Length(through) < 1.0e-3f)
  {
    through = in;
  }
  const float length = NeuronCore::Length(through);
  const float horizontal = length > 0.0f ? std::sqrt(through.x * through.x + through.z * through.z) / length : 0.0f;
  if (length < 1.0e-3f || horizontal < 1.0e-3f)
  {
    const Float3 axis{std::cos(_heading), 0.0f, std::sin(_heading)};
    const float tilt = _share * MAX_ORBIT_TILT;
    return WORLD_UP * std::cos(tilt) + NeuronCore::Cross(axis, WORLD_UP) * std::sin(tilt);
  }
  through = through * (1.0f / length);
  const Float3 level = NeuronCore::Normalize(WORLD_UP - through * through.y);
  const float turn = _share * std::acos(std::min(1.0f, std::cos(MAX_ORBIT_TILT) / horizontal));
  return NeuronCore::Normalize(level * std::cos(turn) + NeuronCore::Cross(through, level) * std::sin(turn));
}

// A whole number of quarter turns about the vertical, exactly (§5.1).
[[nodiscard]] NeuronCore::Rotation QuarterTurnsAboutY(std::uint32_t _turns) noexcept
{
  switch (_turns % 4u)
  {
  case 1u:
    return {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
  case 2u:
    return {{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
  case 3u:
    return {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}};
  default:
    return NeuronCore::IDENTITY_ROTATION;
  }
}

// The sizes of the frigates' flights: two to four each, drawn in turn, the last taking what is left, and no flight of
// one unless there is but one frigate.
[[nodiscard]] std::vector<std::uint32_t> FlightSizes(std::uint32_t _frigates, const WorldRandom& _random)
{
  std::vector<std::uint32_t> sizes;
  std::uint32_t remaining = _frigates;
  for (std::uint32_t i = 0; remaining > 0; ++i)
  {
    std::uint32_t size = MIN_FLIGHT + _random.Bits(i, Stream::FlightSize) % (MAX_FLIGHT - MIN_FLIGHT + 1);
    if (remaining <= MAX_FLIGHT)
    {
      size = remaining;
    }
    else if (remaining - size == 1)
    {
      size = size > MIN_FLIGHT ? size - 1 : size + 1;
    }
    sizes.push_back(size);
    remaining -= size;
  }
  return sizes;
}

} // namespace

const char* SectorRefusalName(SectorRefusal _refusal) noexcept
{
  switch (_refusal)
  {
  case SectorRefusal::BadParameter:
    return "BadParameter";
  case SectorRefusal::ModelNotLoaded:
    return "ModelNotLoaded";
  case SectorRefusal::ShipsWithoutStations:
    return "ShipsWithoutStations";
  case SectorRefusal::SpacingTooSmall:
    return "SpacingTooSmall";
  case SectorRefusal::DoesNotFit:
    return "DoesNotFit";
  }
  return "Unknown";
}

std::expected<std::unique_ptr<Sector>, SectorError> Sector::Create(const SectorParameters& _parameters,
                                                                   const std::filesystem::path& _modelDirectory)
{
  const float spacing = _parameters.stationSpacing;
  const float lifetime = _parameters.debrisLifetimeSeconds;
  if (_parameters.tickRate == 0 || !std::isfinite(spacing) || spacing <= 0.0f || !std::isfinite(lifetime) || lifetime < 0.0f)
  {
    return Refuse(SectorRefusal::BadParameter, "a tick rate of 0, a spacing not above 0 or a lifetime below 0");
  }
  if (std::uint64_t{_parameters.frigates} + _parameters.capitalShips > 0 && _parameters.stations == 0)
  {
    return Refuse(SectorRefusal::ShipsWithoutStations, "ships need a station to orbit");
  }

  std::unique_ptr<Sector> sector(new Sector());
  sector->m_parameters = _parameters;
  for (std::size_t model = 0; model < MODEL_COUNT; ++model)
  {
    const std::string file = std::string(MODEL_NAMES[model]) + ".nvf";
    const auto measure = MeasureModel(_modelDirectory / file, file);
    if (!measure)
    {
      return std::unexpected(measure.error());
    }
    sector->m_radii[model] = measure->radius;
    sector->m_manifest.push_back({MODEL_NAMES[model], measure->hash});
  }
  sector->m_composites = NeuronCore::SingleModelComposites(MODEL_COUNT);
  sector->m_classes[CAPITAL_SHIP_MODEL] = {sector->m_radii[CAPITAL_SHIP_MODEL],
                                           CAPITAL_SHIP_CRUISE,
                                           WINGMAN_LOW_SPEED * CAPITAL_SHIP_CRUISE,
                                           WINGMAN_TOP_SPEED * CAPITAL_SHIP_CRUISE,
                                           CAPITAL_SHIP_ACCELERATION,
                                           CAPITAL_SHIP_TURN_RATE,
                                           CAPITAL_SHIP_BANK_LIMIT,
                                           CAPITAL_SHIP_BANK_RATE};
  sector->m_classes[FRIGATE_MODEL] = {sector->m_radii[FRIGATE_MODEL],
                                      FRIGATE_CRUISE,
                                      WINGMAN_LOW_SPEED * FRIGATE_CRUISE,
                                      WINGMAN_TOP_SPEED * FRIGATE_CRUISE,
                                      FRIGATE_ACCELERATION,
                                      FRIGATE_TURN_RATE,
                                      FRIGATE_BANK_LIMIT,
                                      FRIGATE_BANK_RATE};

  // Each class's orbits: 1.3 to 2 keep-out radii, but no further out than keeps its flights clear of the next station's
  // keep-out, which a spacing too small leaves no room for. A frigate's flight reaches a slot's width to either side.
  const WorldRandom random(_parameters.seed);
  const std::vector<std::uint32_t> frigateFlights = FlightSizes(_parameters.frigates, random);
  std::array<float, MODEL_COUNT> reach{};
  std::array<float, MODEL_COUNT> minOrbit{};
  std::array<float, MODEL_COUNT> maxOrbit{};
  float outermost = 0.0f;
  for (const std::uint16_t model : {CAPITAL_SHIP_MODEL, FRIGATE_MODEL})
  {
    const bool present = model == CAPITAL_SHIP_MODEL ? _parameters.capitalShips > 0 : _parameters.frigates > 0;
    const bool wingmen = model == FRIGATE_MODEL && std::ranges::any_of(frigateFlights, [](std::uint32_t _size) { return _size > 1; });
    reach[model] = wingmen ? std::abs(FormationSlot(0, sector->m_classes[model]).x) : 0.0f;
    const float keepOut = sector->KeepOutRadius(model);
    minOrbit[model] = MIN_ORBIT * keepOut;
    maxOrbit[model] = std::min(MAX_ORBIT * keepOut, spacing - keepOut - reach[model] - ORBIT_CLEARANCE);
    if (present && _parameters.stations > 1 && maxOrbit[model] < minOrbit[model])
    {
      return Refuse(SectorRefusal::SpacingTooSmall, std::string(MODEL_NAMES[model]) + "'s orbits do not fit between the stations");
    }
    maxOrbit[model] = std::max(maxOrbit[model], minOrbit[model]);
    if (present)
    {
      outermost = std::max(outermost, maxOrbit[model] + reach[model] + sector->m_radii[model]);
    }
  }

  // The stations' region, which must hold them and their orbits within the world's bound.
  const float regionRadius = REGION_RADIUS * spacing * std::sqrt(static_cast<float>(_parameters.stations));
  const float regionHeight = REGION_HEIGHT * spacing;
  outermost = std::max(outermost, sector->m_radii[STATION_MODEL]);
  if (regionRadius + outermost > WORLD_BOUND || regionHeight + outermost > WORLD_BOUND)
  {
    return Refuse(SectorRefusal::DoesNotFit, "the stations' region and their orbits leave the world's bound");
  }
  std::vector<Float3> stations;
  for (std::uint32_t attempt = 0; stations.size() < _parameters.stations && attempt < _parameters.stations * PLACEMENT_TRIES; ++attempt)
  {
    const float x = (2.0f * random.Unit(attempt, Stream::StationX) - 1.0f) * regionRadius;
    const float z = (2.0f * random.Unit(attempt, Stream::StationZ) - 1.0f) * regionRadius;
    if (x * x + z * z > regionRadius * regionRadius)
    {
      continue;
    }
    const float y = (2.0f * random.Unit(attempt, Stream::StationY) - 1.0f) * regionHeight;
    const Float3 candidate{std::round(x), std::round(y), std::round(z)};
    if (std::ranges::all_of(stations, [&](Float3 _placed) { return NeuronCore::Length(_placed - candidate) >= spacing; }))
    {
      stations.push_back(candidate);
    }
  }
  if (stations.size() < _parameters.stations)
  {
    return Refuse(SectorRefusal::DoesNotFit, "the stations' region cannot hold them a spacing apart");
  }
  for (std::uint32_t i = 0; i < _parameters.stations; ++i)
  {
    sector->m_entities.push_back({static_cast<std::uint32_t>(sector->m_entities.size() + 1), STATION_MODEL, stations[i],
                                  QuarterTurnsAboutY(random.Bits(i, Stream::StationTurn)), ShipMotion{}, 0, std::nullopt, false});
  }

  // The flights: the capital ships alone, then the frigates' flights, each on a route of its own through two to four
  // stations, starting somewhere along it with its leader on the route and its wingmen in their slots.
  std::vector<std::pair<std::uint16_t, std::uint32_t>> flights;
  flights.reserve(_parameters.capitalShips + frigateFlights.size());
  for (std::uint32_t i = 0; i < _parameters.capitalShips; ++i)
  {
    flights.emplace_back(CAPITAL_SHIP_MODEL, 1u);
  }
  for (const std::uint32_t size : frigateFlights)
  {
    flights.emplace_back(FRIGATE_MODEL, size);
  }
  std::vector<std::uint32_t> order(_parameters.stations);
  for (std::uint32_t f = 0; f < flights.size(); ++f)
  {
    const auto [model, size] = flights[f];
    const ShipClass& shipClass = sector->m_classes[model];
    const std::uint32_t count = std::min(_parameters.stations, MIN_ROUTE_STATIONS + random.Bits(f, Stream::RouteLength) %
                                                                                      (MAX_ROUTE_STATIONS - MIN_ROUTE_STATIONS + 1));
    std::iota(order.begin(), order.end(), 0u);
    for (std::uint32_t j = 0; j < count; ++j)
    {
      std::swap(order[j], order[j + random.Bits(f * DRAWS_PER_FLIGHT + j, Stream::RouteShuffle) % (_parameters.stations - j)]);
    }
    std::vector<Orbit> orbits;
    for (std::uint32_t j = 0; j < count; ++j)
    {
      const std::uint32_t draw = f * DRAWS_PER_FLIGHT + j;
      const Float3 normal =
        OrbitNormal(stations[order[(j + count - 1) % count]], stations[order[j]], stations[order[(j + 1) % count]],
                    2.0f * random.Unit(draw, Stream::OrbitTilt) - 1.0f, random.Unit(draw, Stream::OrbitTiltAxis) * TWO_PI);
      orbits.push_back({stations[order[j]], normal,
                        minOrbit[model] + random.Unit(draw, Stream::OrbitRadius) * (maxOrbit[model] - minOrbit[model]),
                        1u + random.Bits(draw, Stream::OrbitLaps) % 2u, random.Bits(f, Stream::OrbitDirection) % 2u == 1u});
    }
    std::vector<Obstacle> obstacles;
    obstacles.reserve(stations.size());
    for (const Float3& station : stations)
    {
      obstacles.push_back({station, sector->KeepOutRadius(model) + reach[model] + TRANSIT_CLEARANCE});
    }
    Route route = BuildRoute(orbits, obstacles);

    const float start = random.Unit(f, Stream::RouteStart) * route.Length();
    ShipMotion leader{};
    leader.position = route.PositionAt(start);
    leader.forward = NeuronCore::Normalize(route.PositionAt(start + 1.0f) - leader.position);
    leader.speed = shipClass.cruiseSpeed;
    leader.up = SteadyUp(route, start, leader.forward, leader.speed, shipClass);
    leader.progress = start;
    std::vector<std::uint32_t> members;
    for (std::uint32_t rank = 0; rank < size; ++rank)
    {
      ShipMotion motion = leader;
      if (rank > 0)
      {
        motion.position = SlotPosition(leader, FormationSlot(rank - 1, shipClass));
        motion.progress = route.Track(start, motion.position);
      }
      const auto id = static_cast<std::uint32_t>(sector->m_entities.size() + 1);
      sector->m_entities.push_back({id, model, motion.position, ShipRotation(motion), motion, f, std::nullopt, false});
      members.push_back(id);
    }
    sector->m_flights.push_back({members, std::move(route), members.front()});
  }

  sector->m_settings = SpaceSettings(_parameters.seed);
  if (lifetime > 0.0f)
  {
    sector->m_lifetimeTicks =
      std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::llround(lifetime * static_cast<float>(_parameters.tickRate))));
  }
  return sector;
}

std::uint32_t Sector::TickRate() const noexcept
{
  return m_parameters.tickRate;
}

NeuronCore::WorldSettings SpaceSettings(std::uint32_t _skySeed) noexcept
{
  return {NeuronCore::SunDirection(SUN_DEGREES * RADIANS_PER_DEGREE, SUN_DEGREES * RADIANS_PER_DEGREE),
          {SUN_RADIANCE, SUN_RADIANCE, SUN_RADIANCE},
          SUN_ANGULAR_RADIUS,
          {AMBIENT_UPPER, AMBIENT_UPPER, AMBIENT_UPPER},
          {AMBIENT_LOWER, AMBIENT_LOWER, AMBIENT_LOWER},
          _skySeed,
          GALACTIC_PLANE};
}

const NeuronCore::WorldSettings& Sector::Settings() const noexcept
{
  return m_settings;
}

std::span<const NeuronCore::ManifestEntry> Sector::Manifest() const noexcept
{
  return m_manifest;
}

std::span<const NeuronCore::CompositeModel> Sector::Composites() const noexcept
{
  return m_composites;
}

void Sector::Advance(std::uint64_t _worldTick)
{
  // Debris whose lifetime has run out leaves the world with its entity, on the tick it runs out (§5.5).
  if (m_lifetimeTicks > 0)
  {
    for (Entity& entity : m_entities)
    {
      if (!entity.removed && entity.detonation && _worldTick - entity.detonation->worldTick >= m_lifetimeTicks)
      {
        entity.removed = true;
      }
    }
  }

  // A flight keeps its leader while the leader is whole. When it is not, the first whole ship by rank leads, and the
  // others take the slots in rank order (ADR-017): the first wingman leads and the rest re-slot on it. A ship restored
  // rejoins as a wingman, and leads only a flight with no other ship whole.
  const float seconds = 1.0f / static_cast<float>(m_parameters.tickRate);
  for (Flight& flight : m_flights)
  {
    if (!IsWhole(flight.leader))
    {
      const auto whole = std::ranges::find_if(flight.members, [this](std::uint32_t _id) { return IsWhole(_id); });
      flight.leader = whole == flight.members.end() ? 0u : *whole;
    }
    Entity* leading = Find(flight.leader);
    if (leading == nullptr)
    {
      continue;
    }
    const ShipClass& shipClass = m_classes[leading->model];
    const ShipMotion before = leading->motion;
    FlyLeader(leading->motion, flight.route, shipClass, seconds);
    std::size_t slot = 0;
    for (const std::uint32_t id : flight.members)
    {
      if (id == flight.leader || !IsWhole(id))
      {
        continue;
      }
      Entity* wingman = Find(id);
      FlyWingman(wingman->motion, before, leading->motion, FormationSlot(slot, shipClass), flight.route, shipClass, seconds);
      ++slot;
    }
  }
}

void Sector::Detonate(std::uint32_t _entity, std::uint64_t _worldTick)
{
  Entity* entity = Find(_entity);
  if (entity == nullptr || entity->detonation)
  {
    return;
  }
  // The entity freezes where it is; its velocity joins its debris's launch (§7.7), and each detonation draws a seed of
  // its own.
  const Float3 velocity = IsShip(*entity) ? entity->motion.forward * entity->motion.speed : Float3{0.0f, 0.0f, 0.0f};
  const WorldRandom random(m_parameters.seed);
  entity->detonation = NeuronCore::DetonationEvent{entity->id, random.Bits(m_detonations, Stream::Detonation), _worldTick, velocity};
  ++m_detonations;
}

void Sector::Restore(std::uint32_t _entity)
{
  // Whole again where it blew up, with its flight as it was then; pure pursuit picks its route up from there (ADR-017).
  if (Entity* entity = Find(_entity); entity != nullptr)
  {
    entity->detonation.reset();
  }
}

void Sector::Describe(NeuronCore::Snapshot& _snapshot) const
{
  for (const Entity& entity : m_entities)
  {
    if (entity.removed)
    {
      continue;
    }
    if (IsShip(entity))
    {
      const Float3 velocity = entity.detonation ? Float3{0.0f, 0.0f, 0.0f} : entity.motion.forward * entity.motion.speed;
      _snapshot.entities.push_back(
        {entity.id, entity.model, 0, entity.motion.position, NeuronCore::QuaternionOf(ShipRotation(entity.motion)), velocity});
    }
    else
    {
      _snapshot.entities.push_back(
        {entity.id, entity.model, 0, entity.position, NeuronCore::QuaternionOf(entity.rotation), {0.0f, 0.0f, 0.0f}});
    }
  }
  for (const Entity& entity : m_entities)
  {
    if (!entity.removed && entity.detonation)
    {
      _snapshot.detonations.push_back(*entity.detonation);
    }
  }
}

float Sector::ModelRadius(std::uint16_t _model) const noexcept
{
  return m_radii[_model];
}

const ShipClass& Sector::ClassOf(std::uint16_t _shipModel) const noexcept
{
  return m_classes[_shipModel];
}

float Sector::KeepOutRadius(std::uint16_t _shipModel) const noexcept
{
  return m_radii[STATION_MODEL] + m_radii[_shipModel] + KEEP_OUT_MARGIN;
}

std::size_t Sector::FlightCount() const noexcept
{
  return m_flights.size();
}

std::span<const std::uint32_t> Sector::FlightMembers(std::size_t _flight) const noexcept
{
  return m_flights[_flight].members;
}

std::uint32_t Sector::FlightLeader(std::size_t _flight) const noexcept
{
  return m_flights[_flight].leader;
}

const Route& Sector::FlightRoute(std::size_t _flight) const noexcept
{
  return m_flights[_flight].route;
}

bool Sector::IsShip(const Entity& _entity) const noexcept
{
  return _entity.model != STATION_MODEL;
}

bool Sector::IsWhole(std::uint32_t _id) const noexcept
{
  const Entity* entity = _id >= 1 && _id <= m_entities.size() ? &m_entities[_id - 1] : nullptr;
  return entity != nullptr && !entity->removed && !entity->detonation;
}

Sector::Entity* Sector::Find(std::uint32_t _id) noexcept
{
  if (_id < 1 || _id > m_entities.size() || m_entities[_id - 1].removed)
  {
    return nullptr;
  }
  return &m_entities[_id - 1];
}

} // namespace GameLogic
