#pragma once

#include "Float3.h"
#include "Message.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "VoxModel.h"

#include <cstdint>
#include <span>

namespace NeuronCore
{

// What the lighting pass knows besides the pixel it shades (Design/Archive/SampleRenderer.md §11). The twin of the lighting
// values in NeuronClient's LightingConstants (R15).
struct LightingParameters
{
  Float3 toSun;       // unit, from a surface towards the sun
  Float3 sunRadiance; // E_sun: what a surface facing the sun receives
  Float3 skyColor;    // the ambient's upper end
  float skyIntensity; // scales both ends of the ambient
  Float3 groundColor; // the ambient's lower end
  Float3 background;  // what a ray that meets no voxel sees
  float emissiveGain; // multiplies every palette entry's emissive scale; tuned by eye (§7.2)
};

// A shadow map as the lighting pass reads it: standard-Z depth per texel, row by row from the top (§10).
struct ShadowMapImage
{
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  std::span<const float> depth;
};

// How far a shaded point moves along its normal before the shadow map is consulted, in texels of the map (§10).
inline constexpr float SHADOW_NORMAL_OFFSET_TEXELS = 1.5f;

// The direction towards the sun (§10): _elevationRadians above the horizon, at _azimuthRadians from -Z towards +X. That
// is MagicaVoxel's -Y towards +X, the convention Design/ADR/ADR-008 assumes for its _angle, in the engine's axes.
[[nodiscard]] Float3 SunDirection(float _elevationRadians, float _azimuthRadians) noexcept;

// A palette entry's emissive scale (§7.2). MagicaVoxel does not document how _emit and _flux become radiance, so the
// sample defines _emit × 2^_flux (Design/ADR/ADR-008): 2.4 for the station's glowing entries. The viewer's gain
// multiplies it. An entry that is not emissive has none.
[[nodiscard]] float EmissiveScale(const PaletteEntry& _entry) noexcept;

// The lighting the world's settings describe (Design/Archive/SpaceScene.md §12.1), with the emissive gain the viewer chose: the
// sun, and the hemisphere's two colors as they are, over a black background that the sky covers (§11.5).
[[nodiscard]] LightingParameters MakeLightingParameters(const WorldSettings& _settings, float _emissiveGain) noexcept;

// §11's hemisphere: the intensity × lerp(lower color, upper color, ½ + ½ N.y).
[[nodiscard]] Float3 Ambient(Float3 _normal, const LightingParameters& _lighting) noexcept;

// §11: C = albedo × (E_sun × max(0, N·S) × shadow + ambient(N)) + albedo × emissive, with the emissive scale times the
// gain. Emissive light reaches no other surface.
[[nodiscard]] Float3 ShadeSurface(Float3 _albedo, float _emissiveScale, Float3 _normal, float _shadow,
                                  const LightingParameters& _lighting) noexcept;

// The world distance SHADOW_NORMAL_OFFSET_TEXELS spans in _view's map.
[[nodiscard]] float ShadowNormalOffset(const OrthographicView& _view) noexcept;

// The fraction of the sun _position sees, from 0 to 1: nine comparison taps a texel apart around its place in the map,
// each filtered bilinearly as SampleCmpLevelZero does with a linear comparison filter, passing where the point's depth is
// at most the map's (LESS_EQUAL), with a lit border. The depth is clamped to the far plane, so that a point beyond it
// is lit wherever the map holds nothing. The twin of ShadowFactor in Lighting.hlsli (R15).
[[nodiscard]] float ShadowFactor(const ShadowMapImage& _map, const OrthographicView& _view, Float3 _position) noexcept;

// What the lighting pass writes for pixel (x, y) (§11): a voxel is shaded at the depth the view splat wrote, with its
// normal and its palette entry's albedo and emissive scale; where no voxel was hit, the background is. The twin of the
// lighting compute shader's pixel (R15).
[[nodiscard]] Float3 LightPixel(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY, std::uint32_t _voxel,
                                Float3 _normal, float _depth, Float3 _albedo, float _emissiveScale, const ShadowMapImage& _shadowMap,
                                const OrthographicView& _shadowView, const LightingParameters& _lighting) noexcept;

} // namespace NeuronCore
