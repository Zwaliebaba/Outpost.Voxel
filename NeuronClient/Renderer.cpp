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
// shadow map's DSVs; the visibility SRV and UAV, the depth SRV, the HDR color's SRV and UAV and the shadow map's SRV;
// and the visibility UAV's CPU-only twin for its clear.
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
    m_lighting(m_device),
    m_toneMap(m_device, SwapChain::VIEW_FORMAT),
    m_debugView(m_device, SwapChain::VIEW_FORMAT)
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
  winrt::check_hresult(frame.allocator->Reset());
  winrt::check_hresult(m_list->Reset(frame.allocator.get(), nullptr));
  frame.constants->Reset();
  const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = frame.constants->Push(MakeViewConstants(_view));
  const D3D12_GPU_VIRTUAL_ADDRESS shadowViewConstants = frame.constants->Push(MakeShadowViewConstants(m_shadowView));
  const D3D12_GPU_VIRTUAL_ADDRESS lightingConstants = frame.constants->Push(MakeLightingConstants(_settings.lighting, m_shadowView));
  const D3D12_GPU_VIRTUAL_ADDRESS explosionConstants =
    frame.constants->Push(MakeExplosionConstants(m_explosion, _settings.explosionSeconds));

  // At time 0 every rotation is the identity, and the aligned permutation draws; after it, the oriented one (§12).
  const bool exploding = _settings.explosionSeconds > 0.0f;
  const SplatPass& shadowSplat = exploding ? m_shadowSplatOriented : m_shadowSplat;
  const SplatPass& viewSplat = exploding ? m_viewSplatOriented : m_viewSplat;

  ID3D12GraphicsCommandList* list = m_list.get();
  std::array<ID3D12DescriptorHeap*, 1> heaps{m_shaderHeap.Heap()};
  list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
  m_shadowMap.BeginSplat(list);
  shadowSplat.Record(list, m_scene, shadowViewConstants, explosionConstants);
  m_shadowMap.EndSplat(list);
  m_targets.BeginSplat(list);
  viewSplat.Record(list, m_scene, viewConstants, explosionConstants);
  m_targets.EndSplat(list);
  if (!_settings.debugView)
  {
    m_targets.BeginLighting(list);
    m_lighting.Record(list, m_targets, m_shadowMap, m_scene, viewConstants, shadowViewConstants, lightingConstants);
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
  }
  else
  {
    m_toneMap.Record(list, m_targets, _settings.exposure);
  }
  const D3D12_RESOURCE_BARRIER toPresent = Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  list->ResourceBarrier(1, &toPresent);
  winrt::check_hresult(list->Close());

  std::array<ID3D12CommandList*, 1> lists{list};
  m_device.Queue()->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
  m_swapChain.Present(_settings.vsync);
  frame.fenceValue = m_device.Signal();
  m_frameIndex = (m_frameIndex + 1) % FRAMES_IN_FLIGHT;
}

} // namespace NeuronClient
