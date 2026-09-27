#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "DebugViewPass.h"
#include "DescriptorHeap.h"
#include "GraphicsDevice.h"
#include "SwapChain.h"
#include "UploadRing.h"
#include "ViewSplatPass.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "DebugView.h"
#include "PerspectiveView.h"
#include "VoxModel.h"

#include <array>
#include <cstdint>
#include <memory>

namespace NeuronClient
{

struct RendererDesc
{
  GraphicsDeviceDesc device;
  HWND window;
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
};

// The frame of Design/SampleRenderer.md §8 as far as M2 builds it: the view splat into the visibility buffer, then the
// debug view of it into the back buffer. Two frames are in flight, each with its own allocator, constants and fence
// value.
class Renderer
{
public:
  static constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;

  Renderer(const RendererDesc& _desc, const NeuronCore::VoxModel& _model);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer&&) = delete;

  // Follows the window's client area. A zero size, a minimized window, renders nothing until the next resize.
  void Resize(std::uint32_t _widthPixels, std::uint32_t _heightPixels);

  // Renders and presents one frame. _view must be the size the renderer was last resized to. On a failure, the catch
  // block adds Device().DescribeRemoval() to its message while the device still exists (§13).
  void Render(const NeuronCore::PerspectiveView& _view, NeuronCore::DebugView _debugView, bool _vsync);

  [[nodiscard]] const GraphicsDevice& Device() const noexcept
  {
    return m_device;
  }

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept
  {
    return m_targets.WidthPixels();
  }

  [[nodiscard]] std::uint32_t HeightPixels() const noexcept
  {
    return m_targets.HeightPixels();
  }

private:
  struct Frame
  {
    winrt::com_ptr<ID3D12CommandAllocator> allocator;
    std::unique_ptr<UploadRing> constants;
    std::uint64_t fenceValue = 0;
  };

  GraphicsDevice m_device;
  DescriptorHeap m_rtvHeap;
  DescriptorHeap m_dsvHeap;
  DescriptorHeap m_shaderHeap;
  DescriptorHeap m_cpuHeap;
  SwapChain m_swapChain;
  ViewTargets m_targets;
  VoxelScene m_scene;
  ViewSplatPass m_viewSplat;
  DebugViewPass m_debugView;
  std::array<Frame, FRAMES_IN_FLIGHT> m_frames;
  winrt::com_ptr<ID3D12GraphicsCommandList> m_list;
  std::uint32_t m_frameIndex = 0;
};

} // namespace NeuronClient
