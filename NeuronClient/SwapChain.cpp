#include "pch.h"

#include "SwapChain.h"

#include "DescriptorHeap.h"
#include "GraphicsDevice.h"

namespace NeuronClient
{

SwapChain::SwapChain(const GraphicsDevice& _device, HWND _window, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                     std::uint32_t _maximumFrameLatency, DescriptorHeap& _rtvHeap)
  : m_rtvHeap(_rtvHeap)
{
  BOOL tearing = FALSE;
  IDXGIFactory6* factory = _device.Factory();
  m_tearing = SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing != FALSE;
  m_flags = static_cast<UINT>(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
  if (m_tearing)
  {
    m_flags |= static_cast<UINT>(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
  }

  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = _widthPixels;
  desc.Height = _heightPixels;
  desc.Format = BUFFER_FORMAT;
  desc.SampleDesc = {1, 0};
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = BUFFER_COUNT;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
  desc.Flags = m_flags;
  winrt::com_ptr<IDXGISwapChain1> swapChain;
  winrt::check_hresult(factory->CreateSwapChainForHwnd(_device.Queue(), _window, &desc, nullptr, nullptr, swapChain.put()));
  m_swapChain = swapChain.as<IDXGISwapChain3>();
  // The window is borderless fullscreen already (§13); DXGI's own Alt+Enter would switch to exclusive fullscreen.
  winrt::check_hresult(factory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER));
  winrt::check_hresult(m_swapChain->SetMaximumFrameLatency(_maximumFrameLatency));
  m_frameLatency.attach(m_swapChain->GetFrameLatencyWaitableObject());

  for (std::uint32_t& view : m_views)
  {
    view = m_rtvHeap.Allocate();
  }
  CreateViews(_device);
}

void SwapChain::Resize(const GraphicsDevice& _device, std::uint32_t _widthPixels, std::uint32_t _heightPixels)
{
  for (winrt::com_ptr<ID3D12Resource>& buffer : m_buffers)
  {
    buffer = nullptr;
  }
  winrt::check_hresult(m_swapChain->ResizeBuffers(BUFFER_COUNT, _widthPixels, _heightPixels, BUFFER_FORMAT, m_flags));
  CreateViews(_device);
}

void SwapChain::WaitForFrame() const noexcept
{
  WaitForSingleObjectEx(m_frameLatency.get(), 1000, TRUE);
}

void SwapChain::Present(bool _vsync)
{
  // Tearing is only for a frame that does not wait for the vertical blank.
  const UINT flags = !_vsync && m_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
  winrt::check_hresult(m_swapChain->Present(_vsync ? 1u : 0u, flags));
}

ID3D12Resource* SwapChain::CurrentBuffer() const noexcept
{
  return m_buffers[m_swapChain->GetCurrentBackBufferIndex()].get();
}

D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::CurrentView() const noexcept
{
  return m_rtvHeap.Cpu(m_views[m_swapChain->GetCurrentBackBufferIndex()]);
}

void SwapChain::CreateViews(const GraphicsDevice& _device)
{
  D3D12_RENDER_TARGET_VIEW_DESC view{};
  view.Format = VIEW_FORMAT;
  view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  for (std::uint32_t i = 0; i < BUFFER_COUNT; ++i)
  {
    winrt::check_hresult(m_swapChain->GetBuffer(i, IID_PPV_ARGS(m_buffers[i].put())));
    m_buffers[i]->SetName(i == 0 ? L"Back buffer 0" : L"Back buffer 1");
    _device.Device()->CreateRenderTargetView(m_buffers[i].get(), &view, m_rtvHeap.Cpu(m_views[i]));
  }
}

} // namespace NeuronClient
