#include "pch.h"

#include "ShadowViewConstants.h"

namespace NeuronClient
{

ShadowViewConstants MakeShadowViewConstants(const NeuronCore::OrthographicView& _view) noexcept
{
  return {_view.origin,     _view.halfWidth, _view.right,       _view.halfHeight,  _view.up,
          _view.depthRange, _view.forward,   _view.widthPixels, _view.heightPixels};
}

} // namespace NeuronClient
