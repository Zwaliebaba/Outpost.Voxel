#include "pch.h"

#include "Renderer.h"

#include "GpuResources.h"
#include "LightingConstants.h"
#include "ShadowViewConstants.h"
#include "SkyConstants.h"
#include "ViewConstants.h"

#include "Sphere.h"
#include "TraceHit.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <stdexcept>

namespace NeuronClient
{
namespace
{

// Descriptors the renderer needs, with room to spare: the visibility's and the HDR color's RTVs and the back buffers;
// the view's DSV, read-write and read-only, and the shadow map's; the visibility SRV and UAV, the depth SRV, the HDR
// color's SRV and UAV, the overdraw count's SRV and UAV, the shadow map's SRV, the glyph atlas's SRV, and an SRV and a
// UAV for every level bloom's chain can have; and the CPU-only twins of the visibility's and the overdraw count's UAVs,
// for their clears.
constexpr std::uint32_t RTV_CAPACITY = 8;
constexpr std::uint32_t DSV_CAPACITY = 4;
constexpr std::uint32_t SHADER_CAPACITY = 16 + 2 * NeuronCore::BLOOM_MAX_LEVELS;
constexpr std::uint32_t CPU_CAPACITY = 4;

// Per-frame constants to start with: a handful of 256-byte pieces and a thousand placements. A frame that needs more
// grows its ring (Design/SpaceScene.md §7.4).
constexpr std::uint64_t CONSTANTS_PER_FRAME_BYTES = std::uint64_t{64} * 1024;

// The view's, the sun's, the lighting's and the sky's constants, one aligned piece each, besides the placements.
constexpr std::uint64_t FIXED_CONSTANTS_BYTES = std::uint64_t{4} * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
static_assert(sizeof(ViewConstants) <= D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
static_assert(sizeof(ShadowViewConstants) <= D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
static_assert(sizeof(LightingConstants) <= D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
static_assert(sizeof(SkyConstants) <= D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

// Refuses what the shaders could not read safely, since they index the scene's buffers with what a placement names and
// no bound: records beyond the scene's, a palette it lacks, and ids that fall back, overlap or reach NO_VOXEL, which the
// binary search over them and the visibility buffer rely on (Design/SpaceScene.md §7.3).
void CheckPlacements(const VoxelScene& _scene, std::span<const NeuronCore::Placement> _placements)
{
  std::uint64_t nextVoxel = 0;
  for (std::size_t i = 0; i < _placements.size(); ++i)
  {
    const NeuronCore::Placement& placement = _placements[i];
    if (std::uint64_t{placement.firstRecord} + placement.recordCount > _scene.RecordCount())
    {
      throw std::invalid_argument(std::format("Placement {} draws records beyond the scene's {}.", i, _scene.RecordCount()));
    }
    if (placement.paletteIndex >= _scene.ModelCount())
    {
      throw std::invalid_argument(
        std::format("Placement {} takes palette {}, of the scene's {}.", i, placement.paletteIndex, _scene.ModelCount()));
    }
    if (placement.firstVoxel < nextVoxel)
    {
      throw std::invalid_argument(std::format("Placement {}'s ids start at {}, among the ones before it.", i, placement.firstVoxel));
    }
    nextVoxel = std::uint64_t{placement.firstVoxel} + placement.recordCount;
    if (nextVoxel > NeuronCore::NO_VOXEL)
    {
      throw std::invalid_argument(std::format("Placement {}'s ids reach NO_VOXEL.", i));
    }
  }
}

// The draws of the placements _indices names, in that order.
[[nodiscard]] std::vector<SplatDraw> DrawsOf(const SplatPlacements& _placements, const std::vector<std::uint32_t>& _indices)
{
  std::vector<SplatDraw> draws;
  draws.reserve(_indices.size());
  for (const std::uint32_t index : _indices)
  {
    draws.push_back(_placements.draws[index]);
  }
  return draws;
}

// The voxels _draws draw.
[[nodiscard]] std::uint32_t VoxelsOf(const std::vector<SplatDraw>& _draws) noexcept
{
  std::uint32_t voxels = 0;
  for (const SplatDraw& draw : _draws)
  {
    voxels += draw.recordCount;
  }
  return voxels;
}

} // namespace

Renderer::Renderer(const RendererDesc& _desc, std::span<const NeuronCore::VoxModel> _models)
  : m_device(_desc.device),
    m_rtvHeap(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_CAPACITY, false, L"Render target views"),
    m_dsvHeap(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DSV_CAPACITY, false, L"Depth stencil views"),
    m_shaderHeap(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SHADER_CAPACITY, true, L"Shader views"),
    m_cpuHeap(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CPU_CAPACITY, false, L"CPU-only views"),
    m_swapChain(m_device, _desc.window, _desc.widthPixels, _desc.heightPixels, FRAMES_IN_FLIGHT, m_rtvHeap),
    m_targets(m_rtvHeap, m_dsvHeap, m_shaderHeap, m_cpuHeap),
    m_shadowView(_desc.shadowView),
    m_shadowMap(m_device, m_dsvHeap, m_shaderHeap, _desc.shadowView.widthPixels),
    m_scene(m_device, _models),
    m_shadowSplat(m_device, SplatPass::Kind::Shadow),
    m_viewSplat(m_device, SplatPass::Kind::View),
    m_viewSplatPlainDepth(m_device, SplatPass::Kind::View, SplatPass::Variant::PlainDepth),
    m_viewSplatOverdraw(m_device, SplatPass::Kind::View, SplatPass::Variant::Overdraw),
    m_coverage(m_device),
    m_lighting(m_device),
    m_sky(m_device, _desc.stars),
    m_bloomChain(m_shaderHeap),
    m_bloom(m_device),
    m_toneMap(m_device, SwapChain::VIEW_FORMAT),
    m_debugView(m_device, SwapChain::VIEW_FORMAT),
    m_canvas(m_device, m_shaderHeap, SwapChain::VIEW_FORMAT, FRAMES_IN_FLIGHT),
    m_queries(m_device, FRAMES_IN_FLIGHT)
{
  m_targets.Resize(m_device, _desc.widthPixels, _desc.heightPixels);
  m_bloomChain.Resize(m_device, _desc.widthPixels, _desc.heightPixels);
  for (Frame& frame : m_frames)
  {
    winrt::check_hresult(m_device.Device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(frame.allocator.put())));
    frame.constants = std::make_unique<UploadRing>(m_device, CONSTANTS_PER_FRAME_BYTES, L"Frame constants");
  }
  winrt::check_hresult(m_device.Device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames[0].allocator.get(), nullptr,
                                                            IID_PPV_ARGS(m_list.put())));
  m_list->SetName(L"Frame");
  winrt::check_hresult(m_list->Close());
}

Renderer::~Renderer()
{
  // The members are released after this body, and the GPU may still be reading them. A device lost on the way out has
  // nothing left to wait for.
  try
  {
    m_device.Flush();
  }
  catch (...)
  {
    OutputDebugStringW(L"The renderer could not wait for the GPU while it was destroyed; the device is gone.\n");
  }
}

void Renderer::Resize(std::uint32_t _widthPixels, std::uint32_t _heightPixels)
{
  if (_widthPixels == 0 || _heightPixels == 0 || (_widthPixels == WidthPixels() && _heightPixels == HeightPixels()))
  {
    return;
  }
  m_device.Flush();
  m_swapChain.Resize(m_device, _widthPixels, _heightPixels);
  m_targets.Resize(m_device, _widthPixels, _heightPixels);
  m_bloomChain.Resize(m_device, _widthPixels, _heightPixels);
}

void Renderer::SetShadowView(const NeuronCore::OrthographicView& _view)
{
  if (_view.widthPixels != m_shadowView.widthPixels || _view.heightPixels != m_shadowView.heightPixels)
  {
    throw std::invalid_argument(std::format("The sun's view is {} by {} texels, and its shadow map {} by {}.", _view.widthPixels,
                                            _view.heightPixels, m_shadowView.widthPixels, m_shadowView.heightPixels));
  }
  m_shadowView = _view;
}

void Renderer::Render(const NeuronCore::PerspectiveView& _view, std::span<const NeuronCore::Placement> _placements,
                      const FrameSettings& _settings)
{
  CheckPlacements(m_scene, _placements);
  Frame& frame = m_frames[m_frameIndex];
  m_swapChain.WaitForFrame();
  m_device.WaitFor(frame.fenceValue);
  // The GPU has finished the frame this slot carried last, so its measurements can be read (§8), with its draw counts.
  if (std::optional<FrameStatistics> statistics = m_queries.Read(m_frameIndex))
  {
    statistics->draws = frame.draws;
    m_statistics.push_back(*statistics);
  }
  winrt::check_hresult(frame.allocator->Reset());
  winrt::check_hresult(m_list->Reset(frame.allocator.get(), nullptr));

  // The frame's constants grow with its placements, at 64 bytes each and an aligned piece for each detonation
  // (Design/SpaceScene.md §7.4). The GPU has finished with this slot's ring, so a larger one can take its place.
  const std::uint64_t neededBytes = FIXED_CONSTANTS_BYTES + SplatPlacementBytes(_placements);
  if (neededBytes > frame.constants->CapacityBytes())
  {
    frame.constants =
      std::make_unique<UploadRing>(m_device, std::max(neededBytes, 2 * frame.constants->CapacityBytes()), L"Frame constants");
  }
  frame.constants->Reset();
  const auto placementCount = static_cast<std::uint32_t>(_placements.size());
  const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = frame.constants->Push(MakeViewConstants(_view));
  const D3D12_GPU_VIRTUAL_ADDRESS shadowViewConstants = frame.constants->Push(MakeShadowViewConstants(m_shadowView));
  const D3D12_GPU_VIRTUAL_ADDRESS lightingConstants =
    frame.constants->Push(MakeLightingConstants(_settings.lighting, m_shadowView, placementCount));
  const D3D12_GPU_VIRTUAL_ADDRESS skyConstants = frame.constants->Push(MakeSkyConstants(_settings.sky));
  const SplatPlacements placements = PushSplatPlacements(*frame.constants, _placements);

  // Each view culls the placements by their spheres; the camera draws what it keeps nearest first, and the sun in their
  // order (§7.4).
  std::vector<NeuronCore::Sphere> spheres;
  spheres.reserve(_placements.size());
  for (const NeuronCore::Placement& placement : _placements)
  {
    spheres.push_back(NeuronCore::PlacementSphere(placement));
  }
  const std::vector<SplatDraw> viewDraws = DrawsOf(placements, NeuronCore::ListViewDraws(_view, spheres));
  const std::vector<SplatDraw> shadowDraws = DrawsOf(placements, NeuronCore::ListShadowDraws(m_shadowView, spheres));
  const auto viewDrawn = static_cast<std::uint32_t>(viewDraws.size());
  const auto shadowDrawn = static_cast<std::uint32_t>(shadowDraws.size());
  frame.draws = {viewDrawn,           placementCount - viewDrawn, shadowDrawn, placementCount - shadowDrawn,
                 VoxelsOf(viewDraws), VoxelsOf(shadowDraws)};

  // The overdraw view needs the view splat's overdraw variant; otherwise the frame chooses between conservative and plain
  // depth (§9.3, §11).
  SplatPass::Variant variant = _settings.plainDepth ? SplatPass::Variant::PlainDepth : SplatPass::Variant::Standard;
  if (_settings.debugView == NeuronCore::DebugView::Overdraw)
  {
    variant = SplatPass::Variant::Overdraw;
  }
  const SplatPass& viewSplat = ViewSplat(variant);

  // A pass's time runs from its predecessor's timestamp to its own, taken after its last draw or dispatch, so that the
  // transitions between two passes count to the second (§8).
  ID3D12GraphicsCommandList* list = m_list.get();
  std::array<ID3D12DescriptorHeap*, 1> heaps{m_shaderHeap.Heap()};
  list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
  m_queries.Begin(list, m_frameIndex, m_frameNumber);
  m_shadowMap.BeginSplat(list);
  m_shadowSplat.Record(list, m_scene, shadowViewConstants, placements.constants, shadowDraws);
  m_queries.EndPass(list, m_frameIndex, GpuPass::ShadowSplat);
  m_shadowMap.EndSplat(list);
  m_targets.BeginSplat(list);
  if (viewSplat.CountsOverdraw())
  {
    m_targets.BeginOverdraw(list);
  }
  m_queries.BeginStatistics(list, m_frameIndex);
  viewSplat.Record(list, m_scene, viewConstants, placements.constants, viewDraws, m_targets.OverdrawWriteTable());
  m_queries.EndStatistics(list, m_frameIndex);
  m_queries.EndPass(list, m_frameIndex, GpuPass::ViewSplat);
  if (viewSplat.CountsOverdraw())
  {
    m_targets.EndOverdraw(list);
  }
  if (_settings.countCoverage)
  {
    m_queries.BeginCoverage(list, m_frameIndex);
    m_coverage.Record(list, m_targets);
    m_queries.EndCoverage(list, m_frameIndex);
    m_queries.EndPass(list, m_frameIndex, GpuPass::Coverage);
  }
  m_targets.EndSplat(list);
  if (!_settings.debugView)
  {
    m_targets.BeginLighting(list);
    m_lighting.Record(list, m_targets, m_shadowMap, m_scene, viewConstants, shadowViewConstants, lightingConstants, placements.constants);
    m_queries.EndPass(list, m_frameIndex, GpuPass::Lighting);
    m_targets.EndLighting(list);
    m_targets.BeginSky(list);
    m_sky.Record(list, viewConstants, skyConstants);
    m_queries.EndPass(list, m_frameIndex, GpuPass::Sky);
    m_targets.EndSky(list);
    m_bloom.Record(list, m_targets, m_bloomChain);
    m_queries.EndPass(list, m_frameIndex, GpuPass::Bloom);
  }

  ID3D12Resource* backBuffer = m_swapChain.CurrentBuffer();
  const D3D12_RESOURCE_BARRIER toDraw = Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  list->ResourceBarrier(1, &toDraw);
  const D3D12_CPU_DESCRIPTOR_HANDLE target = m_swapChain.CurrentView();
  list->OMSetRenderTargets(1, &target, FALSE, nullptr);
  if (_settings.debugView)
  {
    m_debugView.Record(list, m_targets, m_shadowMap, m_scene, viewConstants, placements.constants, placementCount, *_settings.debugView);
    m_queries.EndPass(list, m_frameIndex, GpuPass::DebugView);
  }
  else
  {
    m_toneMap.Record(list, m_targets, m_bloomChain, _settings.exposure);
    m_queries.EndPass(list, m_frameIndex, GpuPass::ToneMap);
  }
  m_canvas.Record(list, m_frameIndex, WidthPixels(), HeightPixels());
  m_queries.EndPass(list, m_frameIndex, GpuPass::Canvas);
  const D3D12_RESOURCE_BARRIER toPresent = Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  list->ResourceBarrier(1, &toPresent);
  m_queries.Resolve(list, m_frameIndex);
  winrt::check_hresult(list->Close());

  std::array<ID3D12CommandList*, 1> lists{list};
  m_device.Queue()->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
  m_swapChain.Present(_settings.vsync);
  frame.fenceValue = m_device.Signal();
  ++m_frameNumber;
  m_frameIndex = (m_frameIndex + 1) % FRAMES_IN_FLIGHT;
}

std::vector<FrameStatistics> Renderer::TakeStatistics()
{
  std::vector<FrameStatistics> taken;
  taken.swap(m_statistics);
  return taken;
}

void Renderer::FinishFrames()
{
  m_device.Flush();
  // The slot the next frame takes holds the oldest frame still unread.
  for (std::uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
  {
    const std::uint32_t slot = (m_frameIndex + i) % FRAMES_IN_FLIGHT;
    if (std::optional<FrameStatistics> statistics = m_queries.Read(slot))
    {
      statistics->draws = m_frames[slot].draws;
      m_statistics.push_back(*statistics);
    }
  }
}

const SplatPass& Renderer::ViewSplat(SplatPass::Variant _variant) const noexcept
{
  switch (_variant)
  {
  case SplatPass::Variant::PlainDepth:
    return m_viewSplatPlainDepth;
  case SplatPass::Variant::Overdraw:
    return m_viewSplatOverdraw;
  case SplatPass::Variant::Standard:
    break;
  }
  return m_viewSplat;
}

} // namespace NeuronClient
