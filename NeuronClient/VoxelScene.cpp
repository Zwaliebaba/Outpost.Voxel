#include "pch.h"

#include "VoxelScene.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "InstanceConstants.h"

#include <cstddef>
#include <cstring>

namespace NeuronClient
{
namespace
{

// A constant buffer view starts on this boundary, so the palette and every instance take one slot each.
constexpr std::size_t CONSTANTS_SLOT_BYTES = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

static_assert(sizeof(PaletteConstants) <= CONSTANTS_SLOT_BYTES);
static_assert(sizeof(InstanceConstants) <= CONSTANTS_SLOT_BYTES);

} // namespace

VoxelScene::VoxelScene(GraphicsDevice& _device, const NeuronCore::VoxModel& _model)
  : m_palette(MakePaletteConstants(_model.palette))
{
  m_records = CreateStaticBuffer(_device, std::as_bytes(std::span(_model.records)), L"Voxel records");

  std::vector<std::byte> constants((1 + _model.instances.size()) * CONSTANTS_SLOT_BYTES);
  std::memcpy(constants.data(), &m_palette, sizeof(m_palette));
  for (std::size_t i = 0; i < _model.instances.size(); ++i)
  {
    const InstanceConstants instance = MakeInstanceConstants(_model.instances[i]);
    std::memcpy(constants.data() + (1 + i) * CONSTANTS_SLOT_BYTES, &instance, sizeof(instance));
  }
  m_constants = CreateStaticBuffer(_device, constants, L"Palette and instance constants");

  m_instances.reserve(_model.instances.size());
  for (std::size_t i = 0; i < _model.instances.size(); ++i)
  {
    m_instances.push_back({m_constants->GetGPUVirtualAddress() + (1 + i) * CONSTANTS_SLOT_BYTES, _model.instances[i].recordCount});
  }
}

} // namespace NeuronClient
