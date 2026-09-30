#include "pch.h"

#include "Pick.h"

#include <algorithm>
#include <limits>

namespace NeuronClient
{

using NeuronCore::Float2;
using NeuronCore::Float3;

std::optional<ScreenPoint> ProjectPoint(const NeuronCore::PerspectiveView& _view, Float3 _point) noexcept
{
  const Float3 offset = _point - _view.position;
  const float depth = NeuronCore::Dot(offset, _view.forward);
  if (!(depth > _view.nearPlane))
  {
    return std::nullopt;
  }
  const float ndcX = NeuronCore::Dot(offset, _view.right) / (depth * _view.tanHalfFovY * _view.aspect);
  const float ndcY = NeuronCore::Dot(offset, _view.up) / (depth * _view.tanHalfFovY);
  return ScreenPoint{
    {0.5f * (ndcX + 1.0f) * static_cast<float>(_view.widthPixels), 0.5f * (1.0f - ndcY) * static_cast<float>(_view.heightPixels)}, depth};
}

std::optional<PixelRect> PickRect(const NeuronCore::PerspectiveView& _view, const PickBox& _box) noexcept
{
  Float2 lower{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  Float2 upper{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
  for (std::uint32_t corner = 0; corner < 8; ++corner)
  {
    const Float3 local{(corner & 1u) != 0u ? _box.halfSize.x : -_box.halfSize.x, (corner & 2u) != 0u ? _box.halfSize.y : -_box.halfSize.y,
                       (corner & 4u) != 0u ? _box.halfSize.z : -_box.halfSize.z};
    const std::optional<ScreenPoint> projected = ProjectPoint(_view, _box.middle + NeuronCore::RotateVector(_box.rotation, local));
    if (!projected.has_value())
    {
      return std::nullopt;
    }
    lower = NeuronCore::Min(lower, projected->pixels);
    upper = NeuronCore::Max(upper, projected->pixels);
  }
  const Float2 middle{0.5f * (lower.x + upper.x), 0.5f * (lower.y + upper.y)};
  const float width = std::max(upper.x - lower.x, MIN_PICK_PIXELS);
  const float height = std::max(upper.y - lower.y, MIN_PICK_PIXELS);
  return PixelRect{middle.x - 0.5f * width, middle.y - 0.5f * height, width, height};
}

std::optional<std::uint32_t> Pick(const NeuronCore::PerspectiveView& _view, std::span<const PickBox> _boxes, Float2 _pointer) noexcept
{
  std::optional<std::uint32_t> picked;
  float nearest = std::numeric_limits<float>::max();
  for (const PickBox& box : _boxes)
  {
    const std::optional<PixelRect> rect = PickRect(_view, box);
    const std::optional<ScreenPoint> middle = ProjectPoint(_view, box.middle);
    if (rect.has_value() && middle.has_value() && rect->Contains(_pointer) && middle->depth < nearest)
    {
      picked = box.id;
      nearest = middle->depth;
    }
  }
  return picked;
}

std::vector<std::uint32_t> PickWithin(const NeuronCore::PerspectiveView& _view, std::span<const PickBox> _boxes, Float2 _corner,
                                      Float2 _opposite)
{
  const Float2 lower = NeuronCore::Min(_corner, _opposite);
  const Float2 upper = NeuronCore::Max(_corner, _opposite);
  std::vector<std::uint32_t> picked;
  for (const PickBox& box : _boxes)
  {
    const std::optional<ScreenPoint> middle = ProjectPoint(_view, box.middle);
    if (middle.has_value() && middle->pixels.x >= lower.x && middle->pixels.x <= upper.x && middle->pixels.y >= lower.y &&
        middle->pixels.y <= upper.y)
    {
      picked.push_back(box.id);
    }
  }
  return picked;
}

} // namespace NeuronClient
