#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace NeuronClient
{

class GraphicsDevice;

// The passes a frame's timestamps bracket (Design/Archive/SampleRenderer.md §8), in the order a frame runs them. A frame runs
// the lighting and the tone map or a debug view in their place, and counts coverage only when asked to.
enum class GpuPass : std::uint8_t
{
  ShadowSplat,
  ViewSplat,
  Coverage,
  Lighting,
  ToneMap,
  DebugView,
  Canvas
};

inline constexpr std::uint32_t GPU_PASS_COUNT = 7;

// What the GPU measured of one frame (§8, §14).
struct FrameStatistics
{
  std::uint64_t frame; // the renderer's count of frames, from 0
  // By GpuPass: from the end of the previous pass's work to the end of this one's, so that the barriers between them
  // count too. Empty for a pass the frame did not run.
  std::array<std::optional<float>, GPU_PASS_COUNT> passMilliseconds;
  float gpuMilliseconds; // from before the first pass to after the last
  // The view splat's pipeline statistics: Direct3D's VSInvocations, PSInvocations and CPrimitives.
  std::uint64_t vertexShaderInvocations;
  std::uint64_t pixelShaderInvocations;
  std::uint64_t primitives;
  std::optional<std::uint64_t> coveredPixels; // when the frame counted them (§14)
};

// A frame's queries (§8): a timestamp before the first pass and after each one, a pipeline-statistics query around the
// view splat, and an occlusion query around the coverage count. Each frame in flight has a slot of its own in the query
// heaps and in the readback buffer, which the frame's last command resolves into and which is read once the GPU has
// finished the frame.
class FrameQueries
{
public:
  // The most timestamps a frame takes: one before the first pass and one after each.
  static constexpr std::uint32_t TIMESTAMPS_PER_SLOT = GPU_PASS_COUNT + 1;

  FrameQueries(const GraphicsDevice& _device, std::uint32_t _slots);

  // Starts _slot's measurements of frame _frame with the first timestamp. Whatever the slot held unread is dropped.
  void Begin(ID3D12GraphicsCommandList* _list, std::uint32_t _slot, std::uint64_t _frame);

  // The timestamp after _pass. A frame runs each pass at most once.
  void EndPass(ID3D12GraphicsCommandList* _list, std::uint32_t _slot, GpuPass _pass);

  // Around the view splat's draws.
  void BeginStatistics(ID3D12GraphicsCommandList* _list, std::uint32_t _slot) const;
  void EndStatistics(ID3D12GraphicsCommandList* _list, std::uint32_t _slot);

  // Around the coverage count's draw.
  void BeginCoverage(ID3D12GraphicsCommandList* _list, std::uint32_t _slot) const;
  void EndCoverage(ID3D12GraphicsCommandList* _list, std::uint32_t _slot);

  // Copies what _slot measured into its readback memory; recorded after the frame's last pass.
  void Resolve(ID3D12GraphicsCommandList* _list, std::uint32_t _slot);

  // What _slot measured, once the GPU has finished the frame that resolved it; empty when the slot holds nothing unread.
  // Reading marks it read.
  [[nodiscard]] std::optional<FrameStatistics> Read(std::uint32_t _slot);

private:
  struct Slot
  {
    std::uint64_t frame = 0;
    std::array<GpuPass, GPU_PASS_COUNT> passes{}; // the pass each timestamp after the first ends
    std::uint32_t timestamps = 0;                 // recorded, the first included
    bool statistics = false;
    bool coverage = false;
    bool resolved = false; // and not yet read
  };

  winrt::com_ptr<ID3D12QueryHeap> m_timestampHeap;
  winrt::com_ptr<ID3D12QueryHeap> m_statisticsHeap;
  winrt::com_ptr<ID3D12QueryHeap> m_occlusionHeap;
  winrt::com_ptr<ID3D12Resource> m_readback;
  std::vector<Slot> m_slots;
  double m_ticksPerMillisecond = 0.0;
};

} // namespace NeuronClient
