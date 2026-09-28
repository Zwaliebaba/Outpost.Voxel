#pragma once

#include "Float3.h"
#include "VoxModel.h"

#include <cstddef>
#include <cstdint>

namespace NeuronClient
{

// One placed model as the splat shaders read it: where its voxels sit and which records are its own (R16,
// Design/Archive/SampleRenderer.md §7.1, §7.4). This struct is the truth; Shader/InstanceConstants.hlsli mirrors it, and the
// layout echo in NeuronClientTests proves the two agree.
struct InstanceConstants
{
  NeuronCore::Int3 modelOrigin; // world position of the minimum corner of voxel (0, 0, 0)
  std::uint32_t firstRecord;
  std::uint32_t recordCount;
};

static_assert(sizeof(InstanceConstants) == 20);
static_assert(offsetof(InstanceConstants, modelOrigin) == 0);
static_assert(offsetof(InstanceConstants, firstRecord) == 12);
static_assert(offsetof(InstanceConstants, recordCount) == 16);

[[nodiscard]] constexpr InstanceConstants MakeInstanceConstants(const NeuronCore::ModelInstance& _instance) noexcept
{
  return {_instance.origin, _instance.firstRecord, _instance.recordCount};
}

} // namespace NeuronClient
