#pragma once

#include "Float3.h"
#include "Quaternion.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NeuronCore
{

// The stars (Design/Archive/SpaceScene.md §11.2): a catalog generated from the world's sky seed, on the CPU only, and the
// record the sky pass draws each star from.

// §11.2: 20,000 stars by default (§17).
inline constexpr std::uint32_t STAR_COUNT = 20000;

// The catalog's law: the number of stars brighter than magnitude m, over the whole sky, is
// STAR_REFERENCE_COUNT × 10^(STAR_COUNT_SLOPE × (m - STAR_REFERENCE_MAGNITUDE)): a handful brighter than -1, and ten
// times as many every 2.2 magnitudes fainter, down to the faintest a catalog's count reaches. No star is brighter than
// STAR_BRIGHTEST_MAGNITUDE, where the real sky's brightest, Sirius, stands.
inline constexpr double STAR_COUNT_SLOPE = 0.45;
inline constexpr double STAR_REFERENCE_MAGNITUDE = -1.0;
inline constexpr double STAR_REFERENCE_COUNT = 4.0;
inline constexpr double STAR_BRIGHTEST_MAGNITUDE = -1.5;

// §11.2: the temperatures a star's color is drawn from, in kelvin.
inline constexpr float STAR_COOLEST_KELVIN = 2500.0f;
inline constexpr float STAR_HOTTEST_KELVIN = 30000.0f;

// §11.2: how far each color is moved from white toward its black body's.
inline constexpr float STAR_SATURATION = 0.35f;

// One star, as the sky pass reads it from its structured buffer. R16: this struct is the truth,
// Shader/StarRecord.hlsli mirrors it, and the layout echo in NeuronClientTests proves the two agree. A structured
// buffer packs its elements to four bytes, so it is 28 bytes as it stands.
struct StarRecord
{
  Float3 direction; // unit, in the world: towards the star
  float flux;       // 10^(-0.4 m), for its magnitude m
  Float3 color;     // linear Rec. 709, of unit luminance
};

static_assert(sizeof(StarRecord) == 28);
static_assert(offsetof(StarRecord, direction) == 0);
static_assert(offsetof(StarRecord, flux) == 12);
static_assert(offsetof(StarRecord, color) == 16);

// The faintest magnitude a catalog of _count stars reaches under the law.
[[nodiscard]] double FaintestStarMagnitude(std::uint32_t _count) noexcept;

// §11.2: the color of a black body at _kelvin: its spectrum integrated against the CIE 1931 color-matching functions,
// as Wyman, Sloan and Shirley's multi-lobe fit gives them, into linear Rec. 709; normalized to unit luminance, and moved
// toward white by STAR_SATURATION.
[[nodiscard]] Float3 StarColor(float _kelvin) noexcept;

// The color of a black body at _kelvin as StarColor finds it, normalized to unit luminance, but not moved toward white:
// the color of hot debris and of a detonation's light (Design/ADR/ADR-025). Far below 1,000 K a channel can fall below
// zero, outside Rec. 709; the caller clamps it.
[[nodiscard]] Float3 BlackBodyColor(float _kelvin) noexcept;

// The catalog of _seed: _count stars in the galaxy's frame, turned into the world by _galacticPlane (§11.3). Bright
// stars fall almost evenly over the sky, and fainter ones gather toward the galactic plane and its core. The same seed
// gives the same bytes.
[[nodiscard]] std::vector<StarRecord> MakeStarCatalog(std::uint32_t _seed, Quaternion _galacticPlane, std::uint32_t _count);

} // namespace NeuronCore
