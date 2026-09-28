#include "pch.h"

#include "ChaseCamera.h"

#include "OrbitCamera.h"

#include <cmath>

namespace GameLib
{
namespace
{

using NeuronCore::Float3;

// Where the camera sits and looks, in the entity's radii: behind it, above it, and at a point ahead of it, so that the
// ship sits in the lower part of the view with the way ahead above it.
constexpr float BEHIND_RADII = 2.6f;
constexpr float ABOVE_RADII = 0.7f;
constexpr float AHEAD_RADII = 1.0f;

// The spring's time constant: the camera's frame closes 63 % of its lag on the entity's every this many seconds.
constexpr float LAG_SECONDS = 0.35f;

// Below this length a blended axis has no direction worth keeping.
constexpr float MIN_LENGTH = 1.0e-3f;

} // namespace

void ChaseCamera::Reset(Float3 _position, const NeuronCore::Rotation& _rotation, float _radius) noexcept
{
  m_target = _position;
  m_forward = _rotation.axisZ;
  m_up = _rotation.axisY;
  m_radius = _radius;
}

void ChaseCamera::Follow(Float3 _position, const NeuronCore::Rotation& _rotation, float _radius, float _seconds) noexcept
{
  // Frame-rate independent: after t seconds, e^(-t / LAG_SECONDS) of the lag is left.
  const float closing = 1.0f - std::exp(-_seconds / LAG_SECONDS);
  const Float3 forward = m_forward + (_rotation.axisZ - m_forward) * closing;
  const Float3 up = m_up + (_rotation.axisY - m_up) * closing;
  // Half a turn in one step leaves nothing to blend through, and the camera snaps rather than divide by nothing.
  const float forwardLength = NeuronCore::Length(forward);
  const Float3 across = forwardLength < MIN_LENGTH ? up : up - forward * (NeuronCore::Dot(up, forward) / (forwardLength * forwardLength));
  if (forwardLength < MIN_LENGTH || NeuronCore::Length(across) < MIN_LENGTH)
  {
    Reset(_position, _rotation, _radius);
    return;
  }
  m_forward = forward * (1.0f / forwardLength);
  m_up = NeuronCore::Normalize(across);
  m_target = _position;
  m_radius = _radius;
}

NeuronCore::PerspectiveView ChaseCamera::View(std::uint32_t _widthPixels, std::uint32_t _heightPixels) const noexcept
{
  const Float3 eye = m_target - m_forward * (BEHIND_RADII * m_radius) + m_up * (ABOVE_RADII * m_radius);
  return NeuronCore::MakePerspectiveView(eye, m_target + m_forward * (AHEAD_RADII * m_radius), m_up, OrbitCamera::FOV_Y_RADIANS,
                                         OrbitCamera::NEAR_PLANE, _widthPixels, _heightPixels);
}

} // namespace GameLib
