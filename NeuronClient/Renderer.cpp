#include "pch.h"

#include "Renderer.h"

#include "ExplosionConstants.h"
#include "GpuResources.h"
#include "LightingConstants.h"
#include "ShadowViewConstants.h"
#include "ViewConstants.h"

namespace NeuronClient
{
namespace
{

// Descriptors the renderer needs, with room to spare: the visibility RTV and the back buffers; the view's and the
// shadow map's DSVs; the visibility SRV and UAV, the depth SRV, the HDR color's SRV and UAV, the overdraw count's SRV
// and UAV, the shadow map's SRV and the glyph atlas's SRV; and the CPU-only twins of the visibility's and the overdraw
// count's UAVs, for their clears.
constexpr std::uint32_t RTV_CAPACITY = 8;
constexpr std::uint32_t DSV_CAPACITY = 4;
constexpr std::uint32_t SHADER_CAPACITY = 16;
constexpr std::uint32_t CPU_CAPACITY = 4;

// Per-frame constants: a handful of 256-byte pieces.
constexpr std::uint64_t CONSTANTS_PER_FRAME_BYTES = std::uint64_t{64} * 1024;

} // namespace

Renderer::Renderer(const RendererDesc& _desc, const NeuronCore::VoxModel& _model)
  : m_device(_desc.device),
    m_rtvHeap(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_CAPACITY, false, L"Render target views"),
    m_dsvHeap(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DSV_CAPACITY, false, L"Depth stencil views"),
    m_shaderHeap(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SHADER_CAPACITY, true, L"Shader views"),
    m_cpuHeap(m_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CPU_CAPACITY, false, L"CPU-only views"),
    m_swapChain(m_device, _desc.window, _desc.widthPixels, _desc.heightPixels, FRAMES_IN_FLIGHT, m_rtvHeap),
    m_targets(m_rtvHeap, m_dsvHeap, m_shaderHeap, m_cpuHeap),
    m_shadowView(_desc.shadowView),
    m_explosion(_desc.explosion),
    m_shadowMap(m_device, m_dsvHeap, m_shaderHeap, _desc.shadowView.widthPixels),
    m_scene(m_device, _model),
    m_shadowSplat(m_device, SplatPass::Kind::Shadow),
    m_viewSplat(m_device, SplatPass::Kind::View),
    m_shadowSplatOriented(m_device, SplatPass::Kind::Shadow, SplatPass::Permutation::Oriented),
    m_viewSplatOriented(m_device, SplatPass::Kind::View, SplatPass::Permutation::Oriented),
    m_viewSplatPlainDepth(m_device, SplatPass::Kind::View, SplatPass::Permutation::Aligned, SplatPass::Variant::PlainDepth),
    m_viewSplatOrientedPlainDepth(m_device, SplatPass::Kind::View, SplatPass::Permutation::Oriented, SplatPass::Variant::PlainDepth),
    m_viewSplatOverdraw(m_device, SplatPass::Kind::View, SplatPass::Permutation::Aligned, SplatPass::Variant::Overdraw),
    m_viewSplatOrientedOverdraw(m_device, SplatPass::Kind::View, SplatPass::Permutation::Oriented, SplatPass::Variant::Overdraw),
    m_coverage(m_device),
    m_lighting(m_device),
    m_toneMap(m_device, SwapChain::VIEW_FORMAT),
    m_debugView(m_device, SwapChain::VIEW_FORMAT),
    m_canvas(m_device, m_shaderHeap, SwapChain::VIEW_FORMAT, FRAMES_IN_FLIGHT),
    m_queries(m_device, FRAMES_IN_FLIGHT)
{
  m_targets.Resize(m_device, _desc.widthPixels, _desc.heightPixels);
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
}

void Renderer::Render(const NeuronCore::PerspectiveView& _view, const FrameSettings& _settings)
{
  Frame& frame = m_frames[m_frameIndex];
  m_swapChain.WaitForFrame();
  m_device.WaitFor(frame.fenceValue);
  // The GPU has finished the frame this slot carried last, so its measurements can be read (§8).
  if (std::optional<FrameStatistics> statistics = m_queries.Read(m_frameIndex))
  {
    m_statistics.push_back(*statistics);
  }
  winrt::check_hresult(frame.allocator->Reset());
  winrt::check_hresult(m_list->Reset(frame.allocator.get(), nullptr));
  frame.constants->Reset();
  const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = frame.constants->Push(MakeViewConstants(_view));
  const D3D12_GPU_VIRTUAL_ADDRESS shadowViewConstants = frame.constants->Push(MakeShadowViewConstants(m_shadowView));
  const D3D12_GPU_VIRTUAL_ADDRESS lightingConstants = frame.constants->Push(MakeLightingConstants(_settings.lighting, m_shadowView));
  const D3D12_GPU_VIRTUAL_ADDRESS explosionConstants =
    frame.constants->Push(MakeExplosionConstants(m_explosion, _settings.explosionSeconds));

  // At time 0 every voxel is intact, and the aligned permutation draws; after it, the oriented one (Design/SpaceScene.md
  // §5.5).
  const bool exploding = _settings.explosionSeconds > 0.0f;
  const SplatPass& shadowSplat = exploding ? m_shadowSplatOriented : m_shadowSplat;
  // The overdraw view needs the view splat's overdraw variant; otherwise the frame chooses between conservative and plain
  // depth (§9.3, §11).
  SplatPass::Variant variant = _settings.plainDepth ? SplatPass::Variant::PlainDepth : SplatPass::Variant::Standard;
  if (_settings.debugView == NeuronCore::DebugView::Overdraw)
  {
    variant = SplatPass::Variant::Overdraw;
  }
  const SplatPass& viewSplat = ViewSplat(exploding, variant);

  // A pass's time runs from its predecessor's timestamp to its own, taken after its last draw or dispatch, so that the
  // transitions between two passes count to the second (§8).
  ID3D12GraphicsCommandList* list = m_list.get();
  std::array<ID3D12DescriptorHeap*, 1> heaps{m_shaderHeap.Heap()};
  list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
  m_queries.Begin(list, m_frameIndex, m_frameNumber);
  m_shadowMap.BeginSplat(list);
  shadowSplat.Record(list, m_scene, shadowViewConstants, explosionConstants);
  m_queries.EndPass(list, m_frameIndex, GpuPass::ShadowSplat);
  m_shadowMap.EndSplat(list);
  m_targets.BeginSplat(list);
  if (viewSplat.CountsOverdraw())
  {
    m_targets.BeginOverdraw(list);
  }
  m_queries.BeginStatistics(list, m_frameIndex);
  viewSplat.Record(list, m_scene, viewConstants, explosionConstants, m_targets.OverdrawWriteTable());
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
    m_lighting.Record(list, m_targets, m_shadowMap, m_scene, viewConstants, shadowViewConstants, lightingConstants);
    m_queries.EndPass(list, m_frameIndex, GpuPass::Lighting);
    m_targets.EndLighting(list);
  }

  ID3D12Resource* backBuffer = m_swapChain.CurrentBuffer();
  const D3D12_RESOURCE_BARRIER toDraw = Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  list->ResourceBarrier(1, &toDraw);
  const D3D12_CPU_DESCRIPTOR_HANDLE target = m_swapChain.CurrentView();
  list->OMSetRenderTargets(1, &target, FALSE, nullptr);
  if (_settings.debugView)
  {
    m_debugView.Record(list, m_targets, m_shadowMap, m_scene, viewConstants, *_settings.debugView);
    m_queries.EndPass(list, m_frameIndex, GpuPass::DebugView);
  }
  else
  {
    m_toneMap.Record(list, m_targets, _settings.exposure);
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
    if (std::optional<FrameStatistics> statistics = m_queries.Read((m_frameIndex + i) % FRAMES_IN_FLIGHT))
    {
      m_statistics.push_back(*statistics);
    }
  }
}

const SplatPass& Renderer::ViewSplat(bool _exploding, SplatPass::Variant _variant) const noexcept
{
  switch (_variant)
  {
  case SplatPass::Variant::PlainDepth:
    return _exploding ? m_viewSplatOrientedPlainDepth : m_viewSplatPlainDepth;
  case SplatPass::Variant::Overdraw:
    return _exploding ? m_viewSplatOrientedOverdraw : m_viewSplatOverdraw;
  case SplatPass::Variant::Standard:
    break;
  }
  return _exploding ? m_viewSplatOriented : m_viewSplat;
}

} // namespace NeuronClient
