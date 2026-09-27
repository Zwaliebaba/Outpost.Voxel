#include "pch.h"

#include "ViewConstants.h"

namespace NeuronClient
{

ViewConstants MakeViewConstants(const NeuronCore::PerspectiveView& _view) noexcept
{
  return {_view.position,  _view.tanHalfFovY, _view.right,       _view.aspect,      _view.up,
          _view.nearPlane, _view.forward,     _view.widthPixels, _view.heightPixels};
}

} // namespace NeuronClient
