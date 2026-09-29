#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstdint>

namespace NeuronClient
{

class GraphicsDevice;
class ViewTargets;

// The gas shell pass (Design/ADR/ADR-025): one compute dispatch, after the sky and before bloom, that adds the frame's
// shells of hot gas to the HDR color of ViewTargets, each up to the depth the view splat wrote. The shells only give off
// light, so they need no order among themselves.
class GasShellPass
{
public:
  // Shader/GasShellPass.hlsli relies on the same number.
  static constexpr std::uint32_t GROUP_PIXELS = 8;

  explicit GasShellPass(GraphicsDevice& _device);

  // The depth must be readable and the HDR color writable, as ViewTargets::BeginLighting leaves it, with the
  // shader-visible heap set on _list. The constants are ViewConstants and NeuronCore::GasShells.
  void Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, D3D12_GPU_VIRTUAL_ADDRESS _viewConstants,
              D3D12_GPU_VIRTUAL_ADDRESS _shells) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace NeuronClient
