#include "pch.h"

#include "StrategicCamera.h"

#include <algorithm>
#include <cmath>

namespace GameLib
{
namespace
{

using NeuronCore::Float3;

constexpr Float3 WORLD_UP{0.0f, 1.0f, 0.0f};

// The distances it keeps to: close enough to see a ship's modules, far enough to see the whole sector.
constexpr float MINIMUM_DISTANCE = 150.0f;
constexpr float MAXIMUM_DISTANCE = 9000.0f;
constexpr float ZOOM_PER_NOTCH = 0.85f;

// The tilt: 55 degrees below the horizon at first, and between 25 and 85 as it turns.
constexpr float DEFAULT_TILT_RADIANS = 0.959931089f;
constexpr float MINIMUM_TILT_RADIANS = 0.436332313f;
constexpr float MAXIMUM_TILT_RADIANS = 1.483529864f;
constexpr float TURN_RADIANS_PER_PIXEL = 0.005f;

} // namespace

StrategicCamera::StrategicCamera(Float3 _focus, float _distance, float _headingRadians) noexcept
  : m_focus(_focus),
    m_distance(std::clamp(_distance, MINIMUM_DISTANCE, MAXIMUM_DISTANCE)),
    m_headingRadians(_headingRadians),
    m_tiltRadians(DEFAULT_TILT_RADIANS)
{
}

void StrategicCamera::Frame(Float3 _focus, float _distance) noexcept
{
  m_focus = _focus;
  m_distance = std::clamp(_distance, MINIMUM_DISTANCE, MAXIMUM_DISTANCE);
}

void StrategicCamera::Pan(float _rightUnits, float _aheadUnits) noexcept
{
  const Float3 ahead{std::cos(m_headingRadians), 0.0f, std::sin(m_headingRadians)};
  const Float3 right{ahead.z, 0.0f, -ahead.x};
  m_focus = m_focus + right * _rightUnits + ahead * _aheadUnits;
}

void StrategicCamera::Zoom(float _notches) noexcept
{
  m_distance = std::clamp(m_distance * std::pow(ZOOM_PER_NOTCH, _notches), MINIMUM_DISTANCE, MAXIMUM_DISTANCE);
}

void StrategicCamera::Turn(float _deltaXPixels, float _deltaYPixels) noexcept
{
  m_headingRadians -= _deltaXPixels * TURN_RADIANS_PER_PIXEL;
  m_tiltRadians = std::clamp(m_tiltRadians + _deltaYPixels * TURN_RADIANS_PER_PIXEL, MINIMUM_TILT_RADIANS, MAXIMUM_TILT_RADIANS);
}

NeuronCore::PerspectiveView StrategicCamera::View(std::uint32_t _widthPixels, std::uint32_t _heightPixels) const noexcept
{
  const Float3 eye = m_focus - Forward() * m_distance;
  return NeuronCore::MakePerspectiveView(eye, m_focus, WORLD_UP, FOV_Y_RADIANS, NEAR_PLANE, _widthPixels, _heightPixels);
}

Float3 StrategicCamera::Forward() const noexcept
{
  const float level = std::cos(m_tiltRadians);
  return {level * std::cos(m_headingRadians), -std::sin(m_tiltRadians), level * std::sin(m_headingRadians)};
}

} // namespace GameLib
