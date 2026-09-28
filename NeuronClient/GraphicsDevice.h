#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace NeuronClient
{

// How the device is chosen and what watches over it (Design/Archive/SampleRenderer.md §5, §13).
struct GraphicsDeviceDesc
{
  bool warp;                            // WARP rather than hardware (--warp)
  std::optional<std::uint32_t> adapter; // the adapter at this place in high-performance order (--adapter)
  bool debugLayer;                      // the Direct3D 12 debug layer, where it is installed
  bool gpuBasedValidation;              // GPU-based validation, which runs inside the debug layer (--gbv)
};

// The debug layer belongs to an optional Windows feature, so a build can ask for it and not get it.
enum class DebugLayerState : std::uint8_t
{
  Off,
  On,
  Unavailable
};

// The adapter, the device at feature level 12_1, and the one direct queue every pass submits to, with a fence to wait
// on it. At start-up it takes the first adapter in high-performance order that offers 12_1 and Shader Model 6.7, and
// refuses with a message naming every adapter it passed over and what that adapter lacks. Every failed call throws
// through winrt::check_hresult (AGENTS.md R12).
class GraphicsDevice
{
public:
  explicit GraphicsDevice(const GraphicsDeviceDesc& _desc);
  ~GraphicsDevice();

  GraphicsDevice(const GraphicsDevice&) = delete;
  GraphicsDevice& operator=(const GraphicsDevice&) = delete;
  GraphicsDevice(GraphicsDevice&&) = delete;
  GraphicsDevice& operator=(GraphicsDevice&&) = delete;

  [[nodiscard]] ID3D12Device* Device() const noexcept
  {
    return m_device.get();
  }

  [[nodiscard]] ID3D12CommandQueue* Queue() const noexcept
  {
    return m_queue.get();
  }

  [[nodiscard]] IDXGIFactory6* Factory() const noexcept
  {
    return m_factory.get();
  }

  [[nodiscard]] const std::wstring& AdapterName() const noexcept
  {
    return m_adapterName;
  }

  [[nodiscard]] DebugLayerState DebugLayer() const noexcept
  {
    return m_debugLayer;
  }

  // Adds a signal to the queue. Once the fence reaches the value returned, the GPU has finished everything submitted
  // before it.
  [[nodiscard]] std::uint64_t Signal();

  void WaitFor(std::uint64_t _value);

  // Waits until the GPU has finished everything submitted so far.
  void Flush();

  // Records one command list, submits it and waits for it to finish: uploads, readbacks and tests.
  void Execute(const std::function<void(ID3D12GraphicsCommandList*)>& _record);

  // Why the device was removed and what DRED recorded of it, then the debug layer's messages; empty while the device is
  // well. A failure's catch block adds this to its message while the device still exists (Design/Archive/SampleRenderer.md §13).
  [[nodiscard]] std::string DescribeRemoval() const;

  // The debug layer's warnings and errors since the last call, oldest first. Empty without the debug layer.
  [[nodiscard]] std::vector<std::string> TakeDebugMessages() const;

private:
  // Creates the device on _adapter, or says what the adapter lacks.
  [[nodiscard]] std::optional<std::string> TryAdapter(IDXGIAdapter1* _adapter);

  winrt::com_ptr<IDXGIFactory6> m_factory;
  winrt::com_ptr<IDXGIAdapter1> m_adapter;
  winrt::com_ptr<ID3D12Device> m_device;
  winrt::com_ptr<ID3D12CommandQueue> m_queue;
  winrt::com_ptr<ID3D12Fence> m_fence;
  winrt::com_ptr<ID3D12CommandAllocator> m_executeAllocator;
  winrt::com_ptr<ID3D12GraphicsCommandList> m_executeList;
  winrt::com_ptr<ID3D12InfoQueue> m_infoQueue;
  winrt::handle m_fenceEvent;
  std::uint64_t m_fenceValue = 0;
  std::wstring m_adapterName;
  DebugLayerState m_debugLayer = DebugLayerState::Off;
};

} // namespace NeuronClient
