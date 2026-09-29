#pragma once

#include "Float3.h"
#include "Fragmentation.h"
#include "PerspectiveView.h"
#include "Ray.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace NeuronCore
{

// The light of a detonation (Design/ADR/ADR-025): its debris glowing as it cools, a flash that lights what is near, and a
// shell of hot gas that grows and fades. Each is a pure function of the event and the time since it, as the debris is
// (D3), and none of it reaches the server. A frame lights at most MAX_LIT_BLASTS detonations, the nearest the camera.
inline constexpr std::uint32_t MAX_LIT_BLASTS = 8;

// The heat ramp's steps, from the coldest glow to white heat.
inline constexpr std::uint32_t HEAT_COLOR_STEPS = 16;
inline constexpr float COLDEST_GLOW_KELVIN = 900.0f;
inline constexpr float HOTTEST_GLOW_KELVIN = 6500.0f;

// The shell's samples along a ray through it.
inline constexpr std::uint32_t SHELL_STEPS = 24;

// A detonation as the client lights it, in the world: where it happened, how the debris field drifts, how large the
// model was, its seed, and the time since.
struct Blast
{
  Float3 origin;      // the blast origin at the event
  Float3 drift;       // the inherited velocity over the drag: how far the field's frame moves in all
  float drag;         // the lone voxel's, under which the drift is made
  float extent;       // the model's radius, which the flash and the shell scale with
  std::uint32_t seed; // the event's
  float timeSeconds;  // since the event
};

// How a detonated part heats, in its space: a fragment the blast reaches d from its origin starts at a heat of
// 1 / (1 + (d / heatDistance)²) and cools as e^(-coolingRate × size scale × t), so that small pieces cool first.
struct HeatParameters
{
  float heatDistance; // 0 heats nothing
  float coolingRate;  // per second, of a lone voxel
};

// The defaults of ADR-025 for a model of radius _extent.
[[nodiscard]] HeatParameters DefaultHeatParameters(float _extent) noexcept;

// What the lighting pass reads of a placement's heat, in its structured buffer parallel to the placements. R16: this
// struct is the truth, Shader/PlacementHeat.hlsli mirrors it, and the layout echo in NeuronClientTests proves the two
// agree. A whole placement's time is 0, which heats nothing.
struct PlacementHeat
{
  Float3 blastOrigin; // in the part's space
  float timeSeconds;
  float shockSpeed;
  float heatDistance;
  float coolingRate;
  std::uint32_t firstFragment; // where the placement's model's fragments start in the scene's buffer
};

static_assert(sizeof(PlacementHeat) == 32);
static_assert(offsetof(PlacementHeat, blastOrigin) == 0);
static_assert(offsetof(PlacementHeat, timeSeconds) == 12);
static_assert(offsetof(PlacementHeat, shockSpeed) == 16);
static_assert(offsetof(PlacementHeat, heatDistance) == 20);
static_assert(offsetof(PlacementHeat, coolingRate) == 24);
static_assert(offsetof(PlacementHeat, firstFragment) == 28);

// One flash as the lighting pass reads it: a point light without a shadow.
struct Flash
{
  Float3 position;
  float softness; // the square of the distance within which the light no longer grows
  Float3 intensity;
  float padding;
};

static_assert(sizeof(Flash) == 32);
static_assert(offsetof(Flash, position) == 0);
static_assert(offsetof(Flash, softness) == 12);
static_assert(offsetof(Flash, intensity) == 16);

// What the lighting pass knows of the frame's detonations. R16: this struct is the truth, Shader/BlastLighting.hlsli
// mirrors it as a constant buffer, and the layout echo proves the two agree.
struct BlastLighting
{
  std::array<Float4, HEAT_COLOR_STEPS> heatColors; // the ramp: a unit-luminance color in xyz, w unused
  std::array<Flash, MAX_LIT_BLASTS> flashes;
  std::uint32_t flashCount;
  float heatGain; // the radiance of white heat
  float padding0;
  float padding1;
};

static_assert(sizeof(BlastLighting) == 528);
static_assert(offsetof(BlastLighting, heatColors) == 0);
static_assert(offsetof(BlastLighting, flashes) == 256);
static_assert(offsetof(BlastLighting, flashCount) == 512);
static_assert(offsetof(BlastLighting, heatGain) == 516);

// One shell of hot gas as the gas shell pass reads it.
struct GasShell
{
  Float3 center;
  float radius;       // of the shell's peak
  Float3 emission;    // radiance per unit of path through the peak, before the noise
  float thickness;    // the shell's density falls as e^(-(r - radius)² / thickness²)
  std::uint32_t seed; // the noise's
  std::uint32_t padding0;
  std::uint32_t padding1;
  std::uint32_t padding2;
};

static_assert(sizeof(GasShell) == 48);
static_assert(offsetof(GasShell, center) == 0);
static_assert(offsetof(GasShell, radius) == 12);
static_assert(offsetof(GasShell, emission) == 16);
static_assert(offsetof(GasShell, thickness) == 28);
static_assert(offsetof(GasShell, seed) == 32);

// The frame's shells. R16: this struct is the truth, Shader/GasShells.hlsli mirrors it as a constant buffer, and the
// layout echo proves the two agree.
struct GasShells
{
  std::array<GasShell, MAX_LIT_BLASTS> shells;
  std::uint32_t count;
  std::uint32_t padding0;
  std::uint32_t padding1;
  std::uint32_t padding2;
};

static_assert(sizeof(GasShells) == 400);
static_assert(offsetof(GasShells, shells) == 0);
static_assert(offsetof(GasShells, count) == 384);

// Where _blast's field is now: its origin, drifted as the debris drifts.
[[nodiscard]] Float3 BlastCenter(const Blast& _blast) noexcept;

// The frame's lighting of _blasts, the MAX_LIT_BLASTS nearest _viewPosition among those still flashing, with the heat
// ramp and gain of ADR-025.
[[nodiscard]] BlastLighting MakeBlastLighting(std::span<const Blast> _blasts, Float3 _viewPosition);

// The frame's shells of _blasts, the MAX_LIT_BLASTS nearest _viewPosition among those still glowing.
[[nodiscard]] GasShells MakeGasShells(std::span<const Blast> _blasts, Float3 _viewPosition);

// The heat of fragment _shape of a placement heating as _heat has it, from 0 to 1. The twin of FragmentHeat in
// Shader/Blast.hlsli (R15).
[[nodiscard]] float FragmentHeat(const Fragment& _shape, const PlacementHeat& _heat) noexcept;

// The ramp's color at _heat, from 0 to 1, between its two nearest steps. The twin of HeatColor in Shader/Blast.hlsli.
[[nodiscard]] Float3 HeatColor(float _heat, const BlastLighting& _lighting) noexcept;

// What a surface at _heat gives off: the ramp's color times the gain times the heat squared. The twin of HeatRadiance.
[[nodiscard]] Float3 HeatRadiance(float _heat, const BlastLighting& _lighting) noexcept;

// What the flashes give a surface at _position with _normal and _albedo. The twin of FlashLight.
[[nodiscard]] Float3 FlashLight(Float3 _position, Float3 _normal, Float3 _albedo, const BlastLighting& _lighting) noexcept;

// What the lighting pass adds for pixel (x, y) to what LightPixel gives: the voxel's glow at _heat and the flashes'
// light on it; nothing where no voxel was hit. The twin of BlastPixel in Shader/Blast.hlsli.
[[nodiscard]] Float3 BlastPixel(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY, std::uint32_t _voxel,
                                Float3 _normal, float _depth, Float3 _albedo, float _heat, const BlastLighting& _lighting) noexcept;

// The shell's noise at _point, in cells, for _seed: two octaves of value noise, with a mean near 1. The twin of
// ShellNoise in Shader/GasShell.hlsli.
[[nodiscard]] float ShellNoise(Float3 _point, std::uint32_t _seed) noexcept;

// What _shell gives off along _ray from _nearest to _farthest, in the ray's parameter: SHELL_STEPS samples at the
// middles of equal steps through where the ray meets the shell's sphere. The twin of ShellRadiance.
[[nodiscard]] Float3 ShellRadiance(const Ray& _ray, float _nearest, float _farthest, const GasShell& _shell) noexcept;

// What the gas shell pass adds to pixel (x, y), whose depth the view splat wrote: every shell, up to the voxel there or
// without end. The twin of the gas shell compute shader's pixel.
[[nodiscard]] Float3 GasShellPixel(const PerspectiveView& _view, std::uint32_t _pixelX, std::uint32_t _pixelY, float _depth,
                                   const GasShells& _shells) noexcept;

} // namespace NeuronCore
