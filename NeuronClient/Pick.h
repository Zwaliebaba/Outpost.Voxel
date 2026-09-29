#pragma once

#include "Surface.h"

#include "Float3.h"
#include "PerspectiveView.h"
#include "RigidTransform.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace NeuronClient
{

// A point of the world where a view shows it: its place in pixels of the target, +x right and +y down, not rounded, so that
// a pixel's centre is at a half; and its depth along the view.
struct ScreenPoint
{
  NeuronCore::Float2 pixels;
  float depth;
};

// Where _view shows _point, or nothing when it lies on or behind the near plane: the inverse of NeuronCore::PerspectiveRay.
[[nodiscard]] std::optional<ScreenPoint> ProjectPoint(const NeuronCore::PerspectiveView& _view, NeuronCore::Float3 _point) noexcept;

// An entity as the pointer picks it (Design/ADR/ADR-034): its id and its box, where the box's middle stands, its rotation,
// and half its size along its own axes.
struct PickBox
{
  std::uint32_t id;
  NeuronCore::Float3 middle;
  NeuronCore::Rotation rotation;
  NeuronCore::Float3 halfSize;
};

// A projected box is never narrower or shorter than this, so that an entity far off is still picked (the concept's §9).
inline constexpr float MIN_PICK_PIXELS = 10.0f;

// The rectangle _box covers in _view's target: the bounds of its corners' projections, widened and heightened about their
// middle to MIN_PICK_PIXELS where they fall short of it. Nothing when a corner lies behind the near plane.
[[nodiscard]] std::optional<PixelRect> PickRect(const NeuronCore::PerspectiveView& _view, const PickBox& _box) noexcept;

// The entity under _pointer: the one whose PickRect holds it; where several do, the one whose middle is nearest the eye,
// and of those as near, the first. Nothing when none does.
[[nodiscard]] std::optional<std::uint32_t> Pick(const NeuronCore::PerspectiveView& _view, std::span<const PickBox> _boxes,
                                                NeuronCore::Float2 _pointer) noexcept;

// The entities whose middles _view shows within the rectangle between _corner and _opposite, edges included, in their
// order: what a box selects.
[[nodiscard]] std::vector<std::uint32_t> PickWithin(const NeuronCore::PerspectiveView& _view, std::span<const PickBox> _boxes,
                                                    NeuronCore::Float2 _corner, NeuronCore::Float2 _opposite);

} // namespace NeuronClient
