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

// The scene's models on the GPU (Design/Archive/SpaceScene.md §7.1): every model's voxel records in one buffer, model after
// model as NeuronCore::SceneRecords lays them out, and their palettes in another, one per model, uploaded once. A model's
// records are stored once however many placements draw them. The buffers rest in the common state and are promoted by
// each read.
class VoxelScene
{
public:
  // Throws std::invalid_argument for no voxel at all, and for a turned model, which the renderer never draws
  // (Design/Archive/NeuronVoxelFormat.md §6.1).
  VoxelScene(GraphicsDevice& _device, std::span<const NeuronCore::VoxModel> _models);

  // The records as one StructuredBuffer<uint>.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Records() const noexcept
  {
    return m_records->GetGPUVirtualAddress();
  }

  // The palettes as one StructuredBuffer<PaletteConstants>, by model.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Palettes() const noexcept
  {
    return m_palettes->GetGPUVirtualAddress();
  }

  [[nodiscard]] std::uint32_t RecordCount() const noexcept
  {
    return m_recordCount;
  }

  [[nodiscard]] std::uint32_t ModelCount() const noexcept
  {
    return static_cast<std::uint32_t>(m_paletteValues.size());
  }

  // What model _model's palette holds, for the twins the tests compare with.
  [[nodiscard]] const PaletteConstants& PaletteValues(std::uint32_t _model) const noexcept
  {
    return m_paletteValues[_model];
  }

private:
  winrt::com_ptr<ID3D12Resource> m_records;
  winrt::com_ptr<ID3D12Resource> m_palettes;
  std::uint32_t m_recordCount = 0;
  std::vector<PaletteConstants> m_paletteValues;
};

} // namespace NeuronClient
