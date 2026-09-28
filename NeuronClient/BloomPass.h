#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <cstdint>

namespace NeuronClient
{

class BloomChain;
class GraphicsDevice;
class ViewTargets;

// Bloom (Design/SpaceScene.md §12.2): one compute dispatch a level, down the chain from the HDR color and back up it, so
// that the chain's first level holds what the tone map mixes in. Each level is written as a UAV and then read as an SRV
// by the next dispatch, with a transition between.
class BloomPass
{
public:
  // Shader/BloomPass.hlsli relies on the same group size.
  static constexpr std::uint32_t GROUP_PIXELS = 8;

  explicit BloomPass(GraphicsDevice& _device);

  // The whole chain: RecordDown, then RecordUp.
  void Record(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, const BloomChain& _chain) const;

  // The halvings, each level from the one above, the first from the HDR color with Karis's average. The HDR color and
  // every level must be readable, as they rest between frames, and the shader-visible heap set on _list; every level is
  // readable again afterwards.
  void RecordDown(ID3D12GraphicsCommandList* _list, const ViewTargets& _targets, const BloomChain& _chain) const;

  // The way back up: each level from the second smallest, blended in place with the tent over the one below it. As
  // RecordDown leaves the chain.
  void RecordUp(ID3D12GraphicsCommandList* _list, const BloomChain& _chain) const;

private:
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_down;
  winrt::com_ptr<ID3D12PipelineState> m_up;
};

} // namespace NeuronClient
