#include "pch.h"

#include "VoxelScene.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"

#include "Fragmentation.h"
#include "Placement.h"

#include <stdexcept>
#include <string>

namespace NeuronClient
{

VoxelScene::VoxelScene(GraphicsDevice& _device, std::span<const NeuronCore::VoxModel> _models)
{
  // The .vox reader accepts a turned model only for a marker, which the renderer does not draw: a marker becomes a
  // hardpoint in the .nvf (Design/Archive/NeuronVoxelFormat.md §6.1).
  for (const NeuronCore::VoxModel& model : _models)
  {
    for (const NeuronCore::ModelInstance& instance : model.instances)
    {
      if (!NeuronCore::IsIdentityRotation(instance.rotation))
      {
        throw std::invalid_argument(
          "A turned model cannot be drawn: " + (instance.name.empty() ? std::string("an unnamed model") : instance.name) +
          ". A turned model is a marker, which belongs in the .nvf.");
      }
    }
  }
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

  const NeuronCore::SceneFragments fragments(_models);
  m_fragmentCount = static_cast<std::uint32_t>(fragments.Fragments().size());
  m_fragmentOf = CreateStaticBuffer(_device, std::as_bytes(fragments.FragmentOf()), L"Fragment of each record");
  m_fragments = CreateStaticBuffer(_device, std::as_bytes(fragments.Fragments()), L"Fragments");
}

} // namespace NeuronClient
