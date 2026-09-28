#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

namespace NeuronClient
{

class GraphicsDevice;
class ViewTargets;

// The tone map pass (Design/Archive/SampleRenderer.md §8, §11): one triangle over the render target bound by the caller, which
// shows the HDR color through the exposure and the ACES fit. _targetFormat is the render target's view format; the
// application's is the swap chain's sRGB view, which encodes the result, and a test's may be a float format.
class ToneMapPass
{
public:
  ToneMapPass(GraphicsDevice& _device, DXGI_FORMAT _targetFormat);

  // The HDR color must be readable by pixel shaders, as ViewTargets::EndLighting leaves it, and the shader-visible heap
  // set on _list.
  void Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, float _exposure) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace NeuronClient
