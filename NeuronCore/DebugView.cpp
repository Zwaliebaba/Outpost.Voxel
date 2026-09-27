#include "pch.h"

#include "DebugView.h"
#include "TraceHit.h"

#include <algorithm>

namespace NeuronCore
{

Float3 DebugViewColor(DebugView _view, std::uint32_t _voxel, Float3 _normal, Float3 _albedo) noexcept
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
  case DebugView::ShadowMap:
    break;
  }
  return {0.0f, 0.0f, 0.0f};
}

bool ShadowMapViewTexel(std::uint32_t _pixelX, std::uint32_t _pixelY, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                        std::uint32_t _mapWidthPixels, std::uint32_t _mapHeightPixels, std::uint32_t& _texelX,
                        std::uint32_t& _texelY) noexcept
{
  const std::uint32_t side = std::min(_widthPixels, _heightPixels);
  const std::uint32_t left = (_widthPixels - side) / 2u;
  const std::uint32_t top = (_heightPixels - side) / 2u;
  if (_pixelX < left || _pixelY < top || _pixelX >= left + side || _pixelY >= top + side)
  {
    return false;
  }
  // The texel under the pixel's centre, (pixel + ½) × map / side, doubled so that it stays in integers.
  _texelX = ((_pixelX - left) * 2u + 1u) * _mapWidthPixels / (side * 2u);
  _texelY = ((_pixelY - top) * 2u + 1u) * _mapHeightPixels / (side * 2u);
  return true;
}

} // namespace NeuronCore
