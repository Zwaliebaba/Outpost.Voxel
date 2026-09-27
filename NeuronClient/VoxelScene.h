#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "PaletteConstants.h"
#include "VoxModel.h"

#include <cstdint>
#include <span>
#include <vector>

namespace NeuronClient
{

class GraphicsDevice;

// One placed model as a draw needs it.
struct SceneInstance
{
  D3D12_GPU_VIRTUAL_ADDRESS constants; // its InstanceConstants
  std::uint32_t recordCount;
};

// A model's static data on the GPU (Design/SampleRenderer.md §7, §8): the voxel records, the palette and a constant
// buffer per placed model, uploaded once. The buffers rest in the common state and are promoted by each read.
class VoxelScene
{
public:
  VoxelScene(GraphicsDevice& _device, const NeuronCore::VoxModel& _model);

  // The records as one StructuredBuffer<uint>, instance after instance.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Records() const noexcept
  {
    return m_records->GetGPUVirtualAddress();
  }

  // PaletteConstants.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Palette() const noexcept
  {
    return m_constants->GetGPUVirtualAddress();
  }

  [[nodiscard]] std::span<const SceneInstance> Instances() const noexcept
  {
    return m_instances;
  }

  // What the palette's constant buffer holds, for the twins the tests compare with.
  [[nodiscard]] const PaletteConstants& PaletteValues() const noexcept
  {
    return m_palette;
  }

private:
  winrt::com_ptr<ID3D12Resource> m_records;
  winrt::com_ptr<ID3D12Resource> m_constants; // the palette, then one InstanceConstants per instance
  std::vector<SceneInstance> m_instances;
  PaletteConstants m_palette{};
};

} // namespace NeuronClient
