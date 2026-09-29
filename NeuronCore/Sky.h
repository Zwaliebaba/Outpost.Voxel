#pragma once

#include "Float3.h"
#include "Message.h"
#include "PerspectiveView.h"
#include "RigidTransform.h"
#include "StarCatalog.h"

#include <cstdint>
#include <optional>

namespace NeuronCore
{

// The sky (Design/Archive/SpaceScene.md §11): the galaxy and the sun, which one full-screen triangle at the far plane writes
// into every pixel no voxel covers, and the stars, which one quad each adds (§11.5). These are the twins of
// Shader/Sky.hlsli (R15); StarCatalog.h makes the stars they draw.

// §11.3: the galaxy's brightness, tuned by eye so that it stays a faint spread in the background.
inline constexpr float GALAXY_GAIN = 0.06f;

// §11.2: what a star of flux 1, magnitude 0, adds to the HDR color, tuned by eye.
inline constexpr float STAR_GAIN = 12.0f;

// §11.2: the point-spread function's standard deviation, in pixels.
inline constexpr float STAR_SIGMA_PIXELS = 0.7f;

// The darkest HDR value the tone map shows at the default exposure: below it, the ACES fit's toe writes an sRGB byte of
// zero. A star's quad reaches as far as its light stays above it, and a star whose light never does is not drawn.
inline constexpr float STAR_DARKEST_VISIBLE = 0.004f;

// §11.4: the sun's limb darkening, I(μ) = 1 - u (1 - μ).
inline constexpr float SUN_LIMB_DARKENING = 0.6f;

// What the sky pass knows besides the view (§11.5). The twin of NeuronClient's SkyConstants (R15).
struct SkyParameters
{
  Float3 toSun; // unit
  float sunAngularRadiusRadians;
  Float3 sunRadiance; // at the middle of the disc
  Rotation galaxy;    // the galaxy's frame in the world: +Y its north pole and +X its core
  std::uint32_t seed; // the galaxy's noise's
  float galaxyGain;
  float starGain;
};

// The sky of the world's settings, with the gains' defaults. The sun is bright enough that its disc gives the
// irradiance the lighting's sun does: E = L π R² (1 - u / 3) over its darkened limb.
[[nodiscard]] SkyParameters MakeSkyParameters(const WorldSettings& _settings) noexcept;

// §11.3: the galaxy's radiance toward _direction, a unit vector in the galaxy's frame, before the gain: the disk, its
// star clouds and the bulge, dimmed and reddened by the dust.
[[nodiscard]] Float3 GalaxyRadiance(Float3 _direction, std::uint32_t _seed) noexcept;

// §11.4: the sun's radiance toward _direction, a unit vector in the world, with its edge spread over _pixelRadians.
[[nodiscard]] Float3 SunRadiance(Float3 _direction, const SkyParameters& _sky, float _pixelRadians) noexcept;

// The angle a pixel spans at the middle of _view, over which the sun's edge is spread.
[[nodiscard]] float PixelRadians(const PerspectiveView& _view) noexcept;

// What the sky's triangle writes into pixel (x, y) where no voxel is (§11.5): the galaxy, then the sun, toward the ray
// through the pixel's centre.
[[nodiscard]] Float3 SkyPixel(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY,
                              const SkyParameters& _sky) noexcept;

// §11.2: the complementary error function at _x >= 0, by Abramowitz and Stegun's 7.1.26, whose error is below 1.5e-7.
[[nodiscard]] float Erfc(float _x) noexcept;

// The share of a star's light that falls on a pixel, along one axis: the point-spread function integrated over the
// pixel's width, whose centre lies _offsetPixels from the star's. Over every pixel of a row, the shares sum to one.
[[nodiscard]] float StarAxisShare(float _offsetPixels) noexcept;

// The share over the pixel's square: the product of the two axes'.
[[nodiscard]] float StarShare(Float2 _offsetPixels) noexcept;

// Where a star toward _direction, at infinity, lies in _view's image, in pixels from its top-left corner with y down,
// where a pixel's centre is at a half: nothing for a star behind the camera or beside it.
[[nodiscard]] std::optional<Float2> StarPosition(const PerspectiveView& _view, Float3 _direction) noexcept;

// Half the side of a star's quad, in pixels, for _brightness, its flux times the gain: far enough from its centre that
// every pixel outside receives less than STAR_DARKEST_VISIBLE. Zero for a star so faint that no pixel ever receives as
// much, which is not drawn.
[[nodiscard]] float StarQuadRadius(float _brightness) noexcept;

// What a star's quad adds to pixel (x, y) of _view: its brightness and color times the pixel's share, where the pixel's
// centre lies inside the quad, and nothing elsewhere.
[[nodiscard]] Float3 StarPixel(const PerspectiveView& _view, const StarRecord& _star, float _starGain, std::uint32_t _pixelX,
                               std::uint32_t _pixelY) noexcept;

} // namespace NeuronCore
