#pragma once

#include "Float3.h"
#include "PerspectiveView.h"

#include <cstdint>

namespace GameLib
{

// The commander's camera (the concept's §9, Design/ADR/ADR-034): it looks down at a focus on the sector's plane from a
// distance, along a heading about the world's +Y, where a heading of 0 looks along +X. It pans along the plane, zooms in
// and out, and turns about its focus; it neither rolls nor looks up.
class StrategicCamera
{
public:
  // The orbit camera's field of view (Design/Archive/SampleRenderer.md §3), and a near plane for looking down from afar.
  static constexpr float FOV_Y_RADIANS = 0.785398163f;
  static constexpr float NEAR_PLANE = 0.5f;

  // Looks at _focus from _distance along _headingRadians, at the default tilt.
  StrategicCamera(NeuronCore::Float3 _focus, float _distance, float _headingRadians) noexcept;

  // Looks at _focus from _distance, keeping the heading and the tilt.
  void Frame(NeuronCore::Float3 _focus, float _distance) noexcept;

  // Moves the focus along the plane: _rightUnits across the view and _aheadUnits along its heading.
  void Pan(float _rightUnits, float _aheadUnits) noexcept;

  // Zooms in by _notches of the wheel, or out by negative ones, within the distances it keeps to.
  void Zoom(float _notches) noexcept;

  // Turns about the focus by a drag, in physical pixels: across turns the heading, and down tilts toward straight down.
  void Turn(float _deltaXPixels, float _deltaYPixels) noexcept;

  [[nodiscard]] NeuronCore::PerspectiveView View(std::uint32_t _widthPixels, std::uint32_t _heightPixels) const noexcept;

  [[nodiscard]] NeuronCore::Float3 Focus() const noexcept
  {
    return m_focus;
  }

  [[nodiscard]] float Distance() const noexcept
  {
    return m_distance;
  }

private:
  [[nodiscard]] NeuronCore::Float3 Forward() const noexcept;

  NeuronCore::Float3 m_focus;
  float m_distance;
  float m_headingRadians;
  float m_tiltRadians; // below the horizon
};

} // namespace GameLib
