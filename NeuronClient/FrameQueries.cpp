#include "pch.h"

#include "FrameQueries.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"

#include <cstring>

namespace NeuronClient
{
namespace
{

// A slot of the readback buffer: the timestamps, then the pipeline statistics, then the occlusion count, each at the
// eight-byte alignment ResolveQueryData needs.
constexpr std::uint64_t TIMESTAMPS_OFFSET_BYTES = 0;
constexpr std::uint64_t STATISTICS_OFFSET_BYTES = FrameQueries::TIMESTAMPS_PER_SLOT * sizeof(std::uint64_t);
constexpr std::uint64_t OCCLUSION_OFFSET_BYTES = STATISTICS_OFFSET_BYTES + sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
constexpr std::uint64_t SLOT_BYTES = 256;
static_assert(STATISTICS_OFFSET_BYTES % 8 == 0 && OCCLUSION_OFFSET_BYTES % 8 == 0);
static_assert(OCCLUSION_OFFSET_BYTES + sizeof(std::uint64_t) <= SLOT_BYTES);

[[nodiscard]] winrt::com_ptr<ID3D12QueryHeap> CreateQueryHeap(const GraphicsDevice& _device, D3D12_QUERY_HEAP_TYPE _type,
                                                              std::uint32_t _count, const wchar_t* _name)
{
  const D3D12_QUERY_HEAP_DESC desc{_type, _count, 0};
  winrt::com_ptr<ID3D12QueryHeap> heap;
  winrt::check_hresult(_device.Device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put())));
  heap->SetName(_name);
  return heap;
}

} // namespace

FrameQueries::FrameQueries(const GraphicsDevice& _device, std::uint32_t _slots)
  : m_timestampHeap(CreateQueryHeap(_device, D3D12_QUERY_HEAP_TYPE_TIMESTAMP, _slots * TIMESTAMPS_PER_SLOT, L"Frame timestamps")),
    m_statisticsHeap(CreateQueryHeap(_device, D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, _slots, L"View splat statistics")),
    m_occlusionHeap(CreateQueryHeap(_device, D3D12_QUERY_HEAP_TYPE_OCCLUSION, _slots, L"Covered pixels")),
    m_readback(CreateBuffer(_device, D3D12_HEAP_TYPE_READBACK, _slots * SLOT_BYTES, L"Frame queries")),
    m_slots(_slots)
{
  UINT64 ticksPerSecond = 0;
  winrt::check_hresult(_device.Queue()->GetTimestampFrequency(&ticksPerSecond));
  m_ticksPerMillisecond = static_cast<double>(ticksPerSecond) / 1000.0;
}

void FrameQueries::Begin(ID3D12GraphicsCommandList* _list, std::uint32_t _slot, std::uint64_t _frame)
{
  m_slots[_slot] = Slot{};
  m_slots[_slot].frame = _frame;
  m_slots[_slot].timestamps = 1;
  _list->EndQuery(m_timestampHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, _slot * TIMESTAMPS_PER_SLOT);
}

void FrameQueries::EndPass(ID3D12GraphicsCommandList* _list, std::uint32_t _slot, GpuPass _pass)
{
  Slot& slot = m_slots[_slot];
  slot.passes[slot.timestamps - 1] = _pass;
  _list->EndQuery(m_timestampHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, _slot * TIMESTAMPS_PER_SLOT + slot.timestamps);
  ++slot.timestamps;
}

void FrameQueries::BeginStatistics(ID3D12GraphicsCommandList* _list, std::uint32_t _slot) const
{
  _list->BeginQuery(m_statisticsHeap.get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, _slot);
}

void FrameQueries::EndStatistics(ID3D12GraphicsCommandList* _list, std::uint32_t _slot)
{
  _list->EndQuery(m_statisticsHeap.get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, _slot);
  m_slots[_slot].statistics = true;
}

void FrameQueries::BeginCoverage(ID3D12GraphicsCommandList* _list, std::uint32_t _slot) const
{
  _list->BeginQuery(m_occlusionHeap.get(), D3D12_QUERY_TYPE_OCCLUSION, _slot);
}

void FrameQueries::EndCoverage(ID3D12GraphicsCommandList* _list, std::uint32_t _slot)
{
  _list->EndQuery(m_occlusionHeap.get(), D3D12_QUERY_TYPE_OCCLUSION, _slot);
  m_slots[_slot].coverage = true;
}

void FrameQueries::Resolve(ID3D12GraphicsCommandList* _list, std::uint32_t _slot)
{
  Slot& slot = m_slots[_slot];
  const std::uint64_t base = _slot * SLOT_BYTES;
  _list->ResolveQueryData(m_timestampHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, _slot * TIMESTAMPS_PER_SLOT, slot.timestamps, m_readback.get(),
                          base + TIMESTAMPS_OFFSET_BYTES);
  if (slot.statistics)
  {
    _list->ResolveQueryData(m_statisticsHeap.get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, _slot, 1, m_readback.get(),
                            base + STATISTICS_OFFSET_BYTES);
  }
  if (slot.coverage)
  {
    _list->ResolveQueryData(m_occlusionHeap.get(), D3D12_QUERY_TYPE_OCCLUSION, _slot, 1, m_readback.get(), base + OCCLUSION_OFFSET_BYTES);
  }
  slot.resolved = true;
}

std::optional<FrameStatistics> FrameQueries::Read(std::uint32_t _slot)
{
  Slot& slot = m_slots[_slot];
  if (!slot.resolved)
  {
    return std::nullopt;
  }
  slot.resolved = false;

  std::array<std::uint64_t, TIMESTAMPS_PER_SLOT> timestamps{};
  D3D12_QUERY_DATA_PIPELINE_STATISTICS statistics{};
  std::uint64_t covered = 0;
  const std::uint64_t base = _slot * SLOT_BYTES;
  const D3D12_RANGE read{static_cast<SIZE_T>(base), static_cast<SIZE_T>(base + SLOT_BYTES)};
  void* mapped = nullptr;
  winrt::check_hresult(m_readback->Map(0, &read, &mapped));
  const auto* bytes = static_cast<const std::byte*>(mapped) + base;
  std::memcpy(timestamps.data(), bytes + TIMESTAMPS_OFFSET_BYTES, slot.timestamps * sizeof(std::uint64_t));
  std::memcpy(&statistics, bytes + STATISTICS_OFFSET_BYTES, sizeof(statistics));
  std::memcpy(&covered, bytes + OCCLUSION_OFFSET_BYTES, sizeof(covered));
  const D3D12_RANGE nothingWritten{0, 0};
  m_readback->Unmap(0, &nothingWritten);

  // A GPU's timestamps only ever increase on one queue; a pair that seems to run backwards is counted as no time.
  const auto milliseconds = [this](std::uint64_t _from, std::uint64_t _to)
  { return _to > _from ? static_cast<float>(static_cast<double>(_to - _from) / m_ticksPerMillisecond) : 0.0f; };
  FrameStatistics result{};
  result.frame = slot.frame;
  for (std::uint32_t i = 1; i < slot.timestamps; ++i)
  {
    result.passMilliseconds[static_cast<std::size_t>(slot.passes[i - 1])] = milliseconds(timestamps[i - 1], timestamps[i]);
  }
  result.gpuMilliseconds = milliseconds(timestamps[0], timestamps[slot.timestamps - 1]);
  if (slot.statistics)
  {
    result.vertexShaderInvocations = statistics.VSInvocations;
    result.pixelShaderInvocations = statistics.PSInvocations;
    result.primitives = statistics.CPrimitives;
  }
  if (slot.coverage)
  {
    result.coveredPixels = covered;
  }
  return result;
}

} // namespace NeuronClient
