#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <array>
#include <cstdint>

namespace NeuronClient
{

class DescriptorHeap;
class GraphicsDevice;

// The flip-model swap chain of Design/Archive/SampleRenderer.md §13: two 8-bit buffers written through an sRGB view, a
// frame-latency waitable object, and tearing when vsync is off and the system allows it.
class SwapChain
{
public:
  static constexpr DXGI_FORMAT BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
  static constexpr DXGI_FORMAT VIEW_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  static constexpr std::uint32_t BUFFER_COUNT = 2;

  SwapChain(const GraphicsDevice& _device, HWND _window, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
            std::uint32_t _maximumFrameLatency, DescriptorHeap& _rtvHeap);

  // Resizes the buffers. The GPU must be done with them.
  void Resize(const GraphicsDevice& _device, std::uint32_t _widthPixels, std::uint32_t _heightPixels);

  // Blocks until the swap chain can take another frame without the queue running further ahead than its latency.
  void WaitForFrame() const noexcept;

  void Present(bool _vsync);

  [[nodiscard]] ID3D12Resource* CurrentBuffer() const noexcept;
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CurrentView() const noexcept;

private:
  void CreateViews(const GraphicsDevice& _device);

  DescriptorHeap& m_rtvHeap;
  winrt::com_ptr<IDXGISwapChain3> m_swapChain;
  std::array<winrt::com_ptr<ID3D12Resource>, BUFFER_COUNT> m_buffers;
  std::array<std::uint32_t, BUFFER_COUNT> m_views{};
  winrt::handle m_frameLatency;
  UINT m_flags = 0;
  bool m_tearing = false;
};

} // namespace NeuronClient
