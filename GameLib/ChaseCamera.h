#pragma once

#include "Float3.h"
#include "PerspectiveView.h"
#include "RigidTransform.h"

#include <cstdint>

namespace GameLib
{

// The chase camera of Design/Archive/SpaceScene.md §13: behind and above an entity, in the entity's own frame, so that a ship's
// banking reads. Its frame is sprung: it follows the entity's with a first-order lag, a time constant of 0.35 s, so that
// the ship turns and banks before the camera does and nothing small shakes the view. Every model faces its +Z with its
// +Y up (§4).
class ChaseCamera
{
public:
  // Behind the entity at _position, turned by _rotation, whose sphere's radius is _radius, with no lag to make up.
  void Reset(NeuronCore::Float3 _position, const NeuronCore::Rotation& _rotation, float _radius) noexcept;

  // Follows the entity through _seconds.
  void Follow(NeuronCore::Float3 _position, const NeuronCore::Rotation& _rotation, float _radius, float _seconds) noexcept;

  [[nodiscard]] NeuronCore::PerspectiveView View(std::uint32_t _widthPixels, std::uint32_t _heightPixels) const noexcept;

private:
  NeuronCore::Float3 m_target{};                  // the entity's position
  NeuronCore::Float3 m_forward{0.0f, 0.0f, 1.0f}; // the camera's frame, lagging the entity's
  NeuronCore::Float3 m_up{0.0f, 1.0f, 0.0f};
  float m_radius = 1.0f;
};

} // namespace GameLib
