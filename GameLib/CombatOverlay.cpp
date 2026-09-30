#include "pch.h"

#include "CombatOverlay.h"

#include "Overlay.h"
#include "Pick.h"

#include "ColorSpace.h"

#include <algorithm>
#include <optional>

namespace GameLib
{
namespace
{

using NeuronCore::Float2;
using NeuronCore::Float3;

// The alphas of an ownership mark, seen and remembered, of the bars and of the shots.
constexpr float MARK_ALPHA = 0.9f;
constexpr float REMEMBERED_MARK_ALPHA = 0.4f;
constexpr float BAR_ALPHA = 0.85f;
constexpr float SHOT_ALPHA = 0.95f;

// A shot's color is its side's taken this share of the way to white, so that it reads over its own side's hulls.
constexpr float SHOT_LIGHTENING = 0.5f;

// A shell's streak is as long as it flies in this time.
constexpr float STREAK_SECONDS = 0.04f;

// The color of side _side's shots.
[[nodiscard]] Float3 ShotColor(std::span<const Float3> _sideColors, std::uint8_t _side) noexcept
{
  const Float3 white{1.0f, 1.0f, 1.0f};
  const Float3 side = _side != 0 && _side <= _sideColors.size() ? _sideColors[_side - 1u] : white;
  return side + (white - side) * SHOT_LIGHTENING;
}

} // namespace

std::vector<Float3> LinearSideColors(std::span<const NeuronCore::SideColor> _sides)
{
  std::vector<Float3> colors;
  colors.reserve(_sides.size());
  for (const NeuronCore::SideColor& side : _sides)
  {
    colors.push_back({NeuronCore::SrgbToLinear(side.red), NeuronCore::SrgbToLinear(side.green), NeuronCore::SrgbToLinear(side.blue)});
  }
  return colors;
}

void DrawOwnership(NeuronClient::Surface& _surface, const NeuronCore::PerspectiveView& _view, const NeuronClient::WorldSample& _sample,
                   std::span<const NeuronClient::SampledEntity> _remembered, std::span<const float> _radii,
                   std::span<const Float3> _sideColors, const CombatLook& _look)
{
  const auto mark = [&_surface, &_view, _radii, _sideColors, &_look](const NeuronClient::SampledEntity& _entity, float _alpha)
  {
    if (_entity.side == 0 || _entity.side > _sideColors.size() || _entity.detonation.has_value() || _entity.composite >= _radii.size())
    {
      return;
    }
    // A disc just below the bottom of its sphere as the view shows it.
    const std::optional<NeuronClient::ScreenPoint> under =
      NeuronClient::ProjectPoint(_view, _entity.position - _view.up * _radii[_entity.composite]);
    if (!under.has_value())
    {
      return;
    }
    const Float2 at{under->pixels.x, under->pixels.y + _look.gapPixels + 0.5f * _look.markPixels};
    _surface.DrawSegment(at, at, _look.markPixels, _sideColors[_entity.side - 1u], _alpha);
  };
  for (const NeuronClient::SampledEntity& entity : _remembered)
  {
    mark(entity, REMEMBERED_MARK_ALPHA);
  }
  for (const NeuronClient::SampledEntity& entity : _sample.entities)
  {
    mark(entity, MARK_ALPHA);
  }
}

void DrawConditions(NeuronClient::Surface& _surface, const NeuronCore::PerspectiveView& _view, const NeuronClient::WorldSample& _sample,
                    std::span<const std::uint32_t> _selection, std::span<const std::vector<GameCore::CombatComponent>> _components,
                    std::span<const float> _radii, const CombatLook& _look)
{
  for (const NeuronClient::SampledEntity& entity : _sample.entities)
  {
    if (entity.side == 0 || entity.detonation.has_value() || entity.composite >= _components.size() || entity.composite >= _radii.size())
    {
      continue;
    }
    if (entity.gone.empty() && std::ranges::find(_selection, entity.id) == _selection.end())
    {
      continue;
    }
    const GameCore::Condition condition = GameCore::ConditionOf(_components[entity.composite], entity.gone);
    const Float3 vitalColor = condition.vital <= CRITICAL_SHARE  ? _look.criticalColor
                              : condition.vital <= WARNING_SHARE ? _look.warningColor
                                                                 : _look.healthyColor;
    // The hull's bar just above the top of its sphere as the view shows it, and the vital one above that.
    const Float3 top = entity.position + _view.up * _radii[entity.composite];
    const float hullRaise = _look.gapPixels + 0.5f * _look.barHeightPixels;
    const float vitalRaise = hullRaise + _look.barHeightPixels + _look.gapPixels;
    NeuronClient::DrawBar(_surface, _view, top, vitalRaise, _look.barWidthPixels, _look.barHeightPixels, condition.vital, vitalColor,
                          _look.barBackColor, BAR_ALPHA);
    NeuronClient::DrawBar(_surface, _view, top, hullRaise, _look.barWidthPixels, _look.barHeightPixels, condition.hull, _look.hullColor,
                          _look.barBackColor, BAR_ALPHA);
  }
}

void DrawShots(NeuronClient::Surface& _surface, const NeuronCore::PerspectiveView& _view, const GameCore::SnapshotPayload& _payload,
               float _payloadSeconds, std::span<const Float3> _sideColors, const CombatLook& _look)
{
  for (const GameCore::ShellState& shell : _payload.shells)
  {
    const Float3 at = shell.position + shell.velocity * _payloadSeconds;
    NeuronClient::DrawLine(_surface, _view, at - shell.velocity * STREAK_SECONDS, at, _look.shellPixels, ShotColor(_sideColors, shell.side),
                           SHOT_ALPHA);
  }
  for (const GameCore::BeamState& beam : _payload.beams)
  {
    NeuronClient::DrawLine(_surface, _view, beam.from, beam.to, _look.beamPixels, ShotColor(_sideColors, beam.side), SHOT_ALPHA);
  }
}

} // namespace GameLib
