#include "pch.h"

#include "DebugView.h"
#include "TraceHit.h"

#include <algorithm>

namespace NeuronCore
{

Float3 DebugViewColor(DebugView _view, std::uint32_t _voxel, Float3 _normal, Float3 _albedo, Float3 _forward) noexcept
{
  if (_voxel == NO_VOXEL)
  {
    return {0.0f, 0.0f, 0.0f};
  }
  switch (_view)
  {
  case DebugView::Albedo:
    return _albedo;
  case DebugView::Normal:
    return _normal * 0.5f + Float3{0.5f, 0.5f, 0.5f};
  case DebugView::VoxelIndex:
  {
    const std::uint32_t hash = PcgHash(_voxel);
    const Float3 bytes{static_cast<float>(hash & 0xFFu), static_cast<float>((hash >> 8u) & 0xFFu),
                       static_cast<float>((hash >> 16u) & 0xFFu)};
    return bytes / Float3{255.0f, 255.0f, 255.0f};
  }
  case DebugView::Headlight:
    break;
  }
  return _albedo * (0.25f + 0.75f * std::clamp(Dot(_normal, -_forward), 0.0f, 1.0f));
}

} // namespace NeuronCore
