#include "pch.h"

#include "OrbitCamera.h"

#include <algorithm>
#include <cmath>

namespace GameLib
{
namespace
{

using NeuronCore::Float3;

constexpr Float3 WORLD_UP{0.0f, 1.0f, 0.0f};
constexpr float ORBIT_RADIANS_PER_PIXEL = 0.005f;
constexpr float DOLLY_PER_NOTCH = 0.9f;
constexpr float MINIMUM_DISTANCE = 1.0f;
// Short of straight up or down, where the view's basis would lose its right-hand axis.
constexpr float PITCH_LIMIT_RADIANS = 1.55f;
// The default view: looking north-east and 30° down.
constexpr float DEFAULT_YAW_RADIANS = 0.785398163f;
constexpr float DEFAULT_PITCH_RADIANS = -0.523598776f;

[[nodiscard]] float FramingDistance(float _radius) noexcept
{
  return _radius / std::sin(0.5f * OrbitCamera::FOV_Y_RADIANS);
}

} // namespace

OrbitCamera::OrbitCamera(Float3 _center, float _radius) noexcept
  : m_target(_center),
    m_distance(FramingDistance(_radius)),
    m_yawRadians(DEFAULT_YAW_RADIANS),
    m_pitchRadians(DEFAULT_PITCH_RADIANS)
{
}

void OrbitCamera::Frame(Float3 _center, float _radius) noexcept
{
  m_target = _center;
  m_distance = FramingDistance(_radius);
  m_flying = false;
}

void OrbitCamera::Orbit(float _deltaXPixels, float _deltaYPixels) noexcept
{
  m_yawRadians -= _deltaXPixels * ORBIT_RADIANS_PER_PIXEL;
  m_pitchRadians = std::clamp(m_pitchRadians - _deltaYPixels * ORBIT_RADIANS_PER_PIXEL, -PITCH_LIMIT_RADIANS, PITCH_LIMIT_RADIANS);
}

void OrbitCamera::Pan(float _deltaXPixels, float _deltaYPixels, std::uint32_t _heightPixels) noexcept
{
  if (m_flying || _heightPixels == 0)
  {
    return;
  }
  // One pixel at the target's depth, so that the target moves with the pointer.
  const float worldPerPixel = 2.0f * m_distance * std::tan(0.5f * FOV_Y_RADIANS) / static_cast<float>(_heightPixels);
  Float3 right{};
  Float3 up{};
  NeuronCore::MakeViewBasis(Forward(), WORLD_UP, right, up);
  m_target = m_target - right * (_deltaXPixels * worldPerPixel) + up * (_deltaYPixels * worldPerPixel);
}

void OrbitCamera::Dolly(float _notches) noexcept
{
  if (m_flying)
  {
    return;
  }
  m_distance = std::max(m_distance * std::pow(DOLLY_PER_NOTCH, _notches), MINIMUM_DISTANCE);
}

void OrbitCamera::ToggleFlying() noexcept
{
  if (m_flying)
  {
    m_target = m_eye + Forward() * m_distance;
  }
  else
  {
    m_eye = Eye();
  }
  m_flying = !m_flying;
}

void OrbitCamera::Fly(float _forward, float _right, float _up) noexcept
{
  if (!m_flying)
  {
    return;
  }
  Float3 right{};
  Float3 up{};
  NeuronCore::MakeViewBasis(Forward(), WORLD_UP, right, up);
  m_eye = m_eye + Forward() * _forward + right * _right + WORLD_UP * _up;
}

NeuronCore::PerspectiveView OrbitCamera::View(std::uint32_t _widthPixels, std::uint32_t _heightPixels) const noexcept
{
  const Float3 eye = Eye();
  return NeuronCore::MakePerspectiveView(eye, eye + Forward(), WORLD_UP, FOV_Y_RADIANS, NEAR_PLANE, _widthPixels, _heightPixels);
}

Float3 OrbitCamera::Forward() const noexcept
{
  const float horizontal = std::cos(m_pitchRadians);
  return {horizontal * std::cos(m_yawRadians), std::sin(m_pitchRadians), horizontal * std::sin(m_yawRadians)};
}

Float3 OrbitCamera::Eye() const noexcept
{
  return m_flying ? m_eye : m_target - Forward() * m_distance;
}

} // namespace GameLib
