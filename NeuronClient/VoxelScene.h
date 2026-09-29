#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "PaletteConstants.h"

#include "Fragmentation.h"
#include "Message.h"
#include "VoxModel.h"

#include <cstdint>
#include <span>
#include <vector>

namespace NeuronClient
{

class GraphicsDevice;

// The scene's models on the GPU (Design/Archive/SpaceScene.md §7.1): every model's voxel records in one buffer, model after
// model as NeuronCore::SceneRecords lays them out; their palettes in another, each model's own and then its variant for
// each side, model after model (NeuronCore::SidePaletteIndex, Design/ADR/ADR-029); and the fragments each model breaks
// into when it detonates in two more, as NeuronCore::SceneFragments lays them out (Design/ADR/ADR-024), all uploaded once.
// A model's records are stored once however many placements draw them. The buffers rest in the common state and are
// promoted by each read.
class VoxelScene
{
public:
  // _fragments are _models' own, which the client also poses its detonated placements with, so that they are broken
  // once, and _sides the colors of the world's sides, none for a world without them. Throws std::invalid_argument for no
  // voxel at all, for a turned model, which the renderer never draws (Design/Archive/NeuronVoxelFormat.md §6.1), and for
  // fragments that are not of these models' records.
  VoxelScene(GraphicsDevice& _device, std::span<const NeuronCore::VoxModel> _models, const NeuronCore::SceneFragments& _fragments,
             std::span<const NeuronCore::SideColor> _sides = {});

  // A scene without sides that breaks _models itself, for one that nothing else poses.
  VoxelScene(GraphicsDevice& _device, std::span<const NeuronCore::VoxModel> _models);

  // The records as one StructuredBuffer<uint>.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Records() const noexcept
  {
    return m_records->GetGPUVirtualAddress();
  }

  // Each record's fragment within its model, parallel to the records, as one StructuredBuffer<uint>.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS FragmentOf() const noexcept
  {
    return m_fragmentOf->GetGPUVirtualAddress();
  }

  // Every model's fragments, model after model, as one StructuredBuffer<Fragment>.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Fragments() const noexcept
  {
    return m_fragments->GetGPUVirtualAddress();
  }

  // The palettes as one StructuredBuffer<PaletteConstants>, by model and side.
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Palettes() const noexcept
  {
    return m_palettes->GetGPUVirtualAddress();
  }

  [[nodiscard]] std::uint32_t RecordCount() const noexcept
  {
    return m_recordCount;
  }

  [[nodiscard]] std::uint32_t FragmentCount() const noexcept
  {
    return m_fragmentCount;
  }

  [[nodiscard]] std::uint32_t PaletteCount() const noexcept
  {
    return static_cast<std::uint32_t>(m_paletteValues.size());
  }

  // What palette _palette holds, for the twins the tests compare with.
  [[nodiscard]] const PaletteConstants& PaletteValues(std::uint32_t _palette) const noexcept
  {
    return m_paletteValues[_palette];
  }

private:
  winrt::com_ptr<ID3D12Resource> m_records;
  winrt::com_ptr<ID3D12Resource> m_palettes;
  winrt::com_ptr<ID3D12Resource> m_fragmentOf;
  winrt::com_ptr<ID3D12Resource> m_fragments;
  std::uint32_t m_recordCount = 0;
  std::uint32_t m_fragmentCount = 0;
  std::vector<PaletteConstants> m_paletteValues;
};

} // namespace NeuronClient
