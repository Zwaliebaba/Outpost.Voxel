#pragma once

#include "Float3.h"
#include "PerspectiveView.h"

#include <cstdint>

namespace GameLib
{

// The camera of Design/SampleRenderer.md §13. It orbits a target, pans across the view and dollies towards the target,
// or, in fly mode, moves freely and looks about. Angles are about the world's +Z, which is up.
class OrbitCamera
{
public:
  // The file's 45° vertical field of view (§3), and the near plane of §7.5.
  static constexpr float FOV_Y_RADIANS = 0.785398163f;
  static constexpr float NEAR_PLANE = 0.1f;

  // An elevated three-quarter view of the sphere, from as far as frames it.
  OrbitCamera(NeuronCore::Float3 _center, float _radius) noexcept;

  // Looks at the sphere from as far as frames it, keeping the direction; leaves fly mode.
  void Frame(NeuronCore::Float3 _center, float _radius) noexcept;

  // Drags, in physical pixels of a view _heightPixels tall. In fly mode, Orbit turns the view about the eye.
  void Orbit(float _deltaXPixels, float _deltaYPixels) noexcept;
  void Pan(float _deltaXPixels, float _deltaYPixels, std::uint32_t _heightPixels) noexcept;
  void Dolly(float _notches) noexcept;

  [[nodiscard]] bool IsFlying() const noexcept
  {
    return m_flying;
  }

  // Fly mode starts where the orbit is; leaving it puts the orbit's target in front of the camera.
  void ToggleFlying() noexcept;

  // Moves in fly mode: _forward, _right and _up in world units, along the view's own axes except _up, which is world up.
  void Fly(float _forward, float _right, float _up) noexcept;

  [[nodiscard]] NeuronCore::PerspectiveView View(std::uint32_t _widthPixels, std::uint32_t _heightPixels) const noexcept;

  // The distance to the target, for scaling fly speed.
  [[nodiscard]] float Distance() const noexcept
  {
    return m_distance;
  }

private:
  [[nodiscard]] NeuronCore::Float3 Forward() const noexcept;
  [[nodiscard]] NeuronCore::Float3 Eye() const noexcept;

  NeuronCore::Float3 m_target;
  float m_distance;
  float m_yawRadians;   // the view direction's heading
  float m_pitchRadians; // the view direction's angle above the horizon; negative looks down
  bool m_flying = false;
  NeuronCore::Float3 m_eye{};
};

} // namespace GameLib
