#pragma once

#include "Float3.h"
#include "VoxModel.h"

#include <span>

namespace NeuronCore
{

// The lighting a MagicaVoxel scene asks for in its rOBJ chunks (Design/SampleRenderer.md §3, §10, §11), in the
// renderer's terms: angles in radians, colors linear. How the file's values are read is Design/ADR/ADR-008's. Whatever a
// file leaves out, or writes in a form this does not read, keeps the value the station's file gives it.
struct RenderSettings
{
  float sunElevationRadians; // _inf _angle, the first value: above the horizon
  float sunAzimuthRadians;   // _inf _angle, the second value: from -Y towards +X in MagicaVoxel's axes (SunDirection)
  Float3 sunColor;           // _inf _k
  float sunIntensity;        // _inf _i
  Float3 skyColor;           // _uni _k
  float skyIntensity;        // _uni _i
  bool groundVisible;        // _setting _ground
  Float3 groundColor;        // _ground _color
  Float3 backgroundColor;    // _bg _color
  float exposure;            // _film _expo, a multiplier
};

// The station's settings (§3), which a file that says nothing gets.
[[nodiscard]] RenderSettings DefaultRenderSettings() noexcept;

[[nodiscard]] RenderSettings ReadRenderSettings(std::span<const VoxAttributes> _renderObjects);

} // namespace NeuronCore
