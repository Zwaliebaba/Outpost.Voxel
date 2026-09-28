#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

namespace NeuronClient
{

class GraphicsDevice;
class ViewTargets;

// Counts the pixels the view splat covered, which the pixel-shader invocations are measured against (Design/SampleRenderer.md
// §14). One triangle over the view at the far plane is depth-tested LESS against the view's depth, with no depth write and
// no pixel shader: exactly the pixels a voxel wrote pass, and an occlusion query around the draw counts them. The
// hardware's depth test does the counting, so no shader algorithm needs a twin; the test compares the count with the
// read-back depth.
class CoveragePass
{
public:
  explicit CoveragePass(const GraphicsDevice& _device);

  // Between the view splat and ViewTargets::EndSplat, while the depth is writable. Binds the depth alone.
  void Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace NeuronClient
