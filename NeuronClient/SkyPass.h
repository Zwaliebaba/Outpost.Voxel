#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "StarCatalog.h"

#include <cstdint>
#include <span>

namespace NeuronClient
{

class GraphicsDevice;

// The sky pass (Design/Archive/SpaceScene.md §11.5): one triangle at the far plane that writes the galaxy and the sun into every
// pixel of the HDR color no voxel covers, then one quad a star from the world's catalog, blended additively. Both test
// the view's depth, bound read-only, for equality with the far plane (§9), and write none.
class SkyPass
{
public:
  // _stars are the world's catalog, copied to the GPU once; it may be empty.
  SkyPass(GraphicsDevice& _device, std::span<const NeuronCore::StarRecord> _stars);

  // Between ViewTargets::BeginSky and EndSky. The constants are ViewConstants and SkyConstants.
  void Record(ID3D12GraphicsCommandList* _list, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants, D3D12_GPU_VIRTUAL_ADDRESS _skyConstants) const;

  [[nodiscard]] std::uint32_t StarCount() const noexcept
  {
    return m_starCount;
  }

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_sky;
  winrt::com_ptr<ID3D12PipelineState> m_stars;
  winrt::com_ptr<ID3D12Resource> m_starBuffer;
  std::uint32_t m_starCount = 0;
};

} // namespace NeuronClient
