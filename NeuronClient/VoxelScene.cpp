#include "pch.h"

#include "VoxelScene.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"

#include "Placement.h"

#include <stdexcept>

namespace NeuronClient
{

VoxelScene::VoxelScene(GraphicsDevice& _device, std::span<const NeuronCore::VoxModel> _models)
{
  const std::vector<std::uint32_t> records = NeuronCore::SceneRecords(_models);
  if (_models.empty() || records.empty())
  {
    throw std::invalid_argument("A scene needs at least one model with a voxel.");
  }
  m_recordCount = static_cast<std::uint32_t>(records.size());
  m_records = CreateStaticBuffer(_device, std::as_bytes(std::span(records)), L"Voxel records");

  m_paletteValues.reserve(_models.size());
  for (const NeuronCore::VoxModel& model : _models)
  {
    m_paletteValues.push_back(MakePaletteConstants(model.palette));
  }
  m_palettes = CreateStaticBuffer(_device, std::as_bytes(std::span(m_paletteValues)), L"Palettes");
}

} // namespace NeuronClient
