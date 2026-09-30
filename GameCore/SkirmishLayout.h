#pragma once

#include "Float3.h"
#include "Message.h"
#include "RigidTransform.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace GameCore
{

// The MVP's skirmish as its seed lays it out (Design/MvpPlan.md §2.1, §8.4; Design/GameConcept.md §3, §7.1, G66;
// Design/ADR/ADR-030): two cores 3,600 units apart, each side's starting ships in front of its core, two small fields of
// asteroids near each core and two larger ones in the middle. The sector is its own half turn about its center, with the
// sides exchanged: the generator lays out half of it and turns that half. Everything stands aligned, at whole coordinates
// and turned by quarter turns, which no build rounds differently, so that server and client could each lay it out from
// the seed.

// A skirmish's sides, by their number on the wire (Design/ADR/ADR-029): side 1 starts at -x and side 2 at +x.
inline constexpr std::size_t SIDE_COUNT = 2;

// A side's name and color (G24): the color every palette of the side's entities shows in its side entry.
struct SideSpec
{
  std::string_view name;
  NeuronCore::SideColor color;
};

inline constexpr std::array<SideSpec, SIDE_COUNT> SKIRMISH_SIDES{{{"Blue", {70, 130, 220}}, {"Red", {220, 80, 60}}}};

// The asteroid models, by their names in GameData (Design/ADR/ADR-027).
inline constexpr std::array<std::string_view, 3> ASTEROID_MODELS{"AsteroidA", "AsteroidB", "AsteroidC"};

// What each side starts with (§2.1): its core, and two miners and two gunships holding station in a line in front of it,
// the miners outside. Side 1's stand at these anchors, and side 2's at their half turns (§8.4).
inline constexpr std::string_view CORE_DESIGN = "StationCore";
inline constexpr NeuronCore::Int3 CORE_ANCHOR{-1800, 0, 0};
inline constexpr std::array<std::string_view, 4> STARTING_SHIPS{"Miner", "Miner", "Gunship", "Gunship"};
inline constexpr std::array<NeuronCore::Int3, 4> STARTING_SHIP_ANCHORS{{{-1680, 0, -90}, {-1680, 0, 90}, {-1680, 0, -30}, {-1680, 0, 30}}};

// The battle a skirmish stages with --battle (Design/ADR/ADR-035): each side's whole command budget of 20 points in combat
// ships, two cruisers, six gunships and six lancers, in place of its starting ships, facing the other side in a block
// about 450 units from the sector's center. The nearest two stand 780 apart, out of each other's sensors, so that the
// fight starts when an order starts it. It is the heaviest fight phase 5 can stage, which its done-when measures.
inline constexpr std::array<std::string_view, 14> BATTLE_FLEET{"Cruiser", "Cruiser", "Gunship", "Gunship", "Gunship", "Gunship", "Gunship",
                                                               "Gunship", "Lancer",  "Lancer",  "Lancer",  "Lancer",  "Lancer",  "Lancer"};
inline constexpr std::array<NeuronCore::Int3, 14> BATTLE_FLEET_ANCHORS{{{-520, 0, -60},
                                                                        {-520, 0, 60},
                                                                        {-440, 0, -150},
                                                                        {-440, 0, -90},
                                                                        {-440, 0, -30},
                                                                        {-440, 0, 30},
                                                                        {-440, 0, 90},
                                                                        {-440, 0, 150},
                                                                        {-390, 0, -150},
                                                                        {-390, 0, -90},
                                                                        {-390, 0, -30},
                                                                        {-390, 0, 30},
                                                                        {-390, 0, 90},
                                                                        {-390, 0, 150}}};

// The fields (§8.4): side 1's two near fields and the first middle field, which the half turn maps onto the other three.
// Each holds the same number of asteroids whatever the seed, within its radius of its center.
struct FieldSpec
{
  NeuronCore::Int3 center;
  std::int32_t radius;
  std::uint32_t asteroids;
};

inline constexpr std::array<FieldSpec, 3> HALF_FIELDS{{{{-1400, 0, 500}, 110, 6}, {{-1400, 0, -500}, 110, 6}, {{0, 0, 800}, 180, 10}}};

// How far an asteroid may stand above or below the plane, and how far apart two asteroids' anchors stand at least: more
// than the largest asteroid's diameter, so that no two touch.
inline constexpr std::int32_t ASTEROID_HEIGHT = 20;
inline constexpr std::int32_t ASTEROID_SPACING = 48;

// Where a thing of the layout stands: its anchor, whole coordinates, and its turn, one of the cube's 24. The anchor is
// where the lower corner of the cell holding the middle of the thing's box goes, turned: so every voxel's cell is whole
// there, and the thing draws aligned (Design/Archive/SpaceScene.md §7.2).
struct Anchor
{
  NeuronCore::Int3 position;
  NeuronCore::Rotation turn;
};

// A design a side starts with, by its name in the catalogue.
struct LayoutUnit
{
  std::string_view design;
  std::uint8_t side; // 1 or 2
  Anchor anchor;
};

struct LayoutAsteroid
{
  std::uint32_t model; // in ASTEROID_MODELS
  Anchor anchor;
};

struct SkirmishLayout
{
  std::vector<LayoutUnit> units;         // side 1's core and ships, then side 2's, each the half turn of side 1's
  std::vector<LayoutAsteroid> asteroids; // HALF_FIELDS' asteroids, field after field, then the half turn of each
};

// The half turn about the sector's center, which maps each side's start onto the other's: (x, y, z) to (-x, y, -z).
inline constexpr NeuronCore::Rotation HALF_TURN{{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};

// The turn of everything of side 1: a quarter turn about +y, which sends a design's front, +Z, to +x, toward side 2.
inline constexpr NeuronCore::Rotation FACING_SIDE_2{{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};

[[nodiscard]] constexpr NeuronCore::Int3 HalfTurn(NeuronCore::Int3 _point) noexcept
{
  return {-_point.x, _point.y, -_point.z};
}

// _anchor turned a half turn about the sector's center: its position's image, and the half turn after its turn.
[[nodiscard]] Anchor HalfTurn(const Anchor& _anchor) noexcept;

// The cube's 24 rotations, in a fixed order, which the generator draws from.
[[nodiscard]] std::span<const NeuronCore::Rotation, 24> CubeRotations() noexcept;

// The skirmish that _seed lays out. The same seed gives the same layout on every build.
[[nodiscard]] SkirmishLayout MakeSkirmishLayout(std::uint32_t _seed);

// The skirmish that _seed lays out with each side's starting ships replaced by BATTLE_FLEET.
[[nodiscard]] SkirmishLayout MakeBattleLayout(std::uint32_t _seed);

// Where a thing whose box has its middle at _middle, in its own space, stands when anchored at _anchor: the anchor, plus
// the turned offset of the middle from the lower corner of its cell. Every term is a whole or a half voxel, and exact.
[[nodiscard]] NeuronCore::Float3 AnchoredPosition(const Anchor& _anchor, NeuronCore::Float3 _middle) noexcept;

} // namespace GameCore
