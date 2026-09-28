#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "Canvas.h"
#include "CoveragePass.h"
#include "DebugViewPass.h"
#include "DescriptorHeap.h"
#include "FrameQueries.h"
#include "GraphicsDevice.h"
#include "LightingPass.h"
#include "ShadowMap.h"
#include "SplatPass.h"
#include "SwapChain.h"
#include "ToneMapPass.h"
#include "UploadRing.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "DebugView.h"
#include "Explosion.h"
#include "Lighting.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "VoxModel.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace NeuronClient
{

struct RendererDesc
{
  GraphicsDeviceDesc device;
  HWND window;
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  NeuronCore::OrthographicView shadowView; // the sun's, fitted once to the scene (§10); its size is the shadow map's
  NeuronCore::ExplosionParameters explosion;
};

// What a frame shows (§11, §12, §13), and what it measures besides (§9.3, §14).
struct FrameSettings
{
  std::optional<NeuronCore::DebugView> debugView; // empty: the lit image
  NeuronCore::LightingParameters lighting;
  float exposure;
  float explosionSeconds; // since the detonation; 0 is the intact model
  bool vsync;
  bool plainDepth;    // the view splat writes SV_Depth rather than conservative depth (§9.3); the overdraw view overrides it
  bool countCoverage; // counts the pixels a voxel covers (§14)
};

// The frame of Design/SampleRenderer.md §8: the shadow splat into the shadow map and the view splat into the depth and
// visibility buffers, then the lighting into HDR color and the tone map into the back buffer, or a debug view in
// their place, and last the canvas over it all (§13). Both splats draw the aligned permutation while the model is intact
// and the oriented one once the explosion has started (§12). Two frames are in flight, each with its own allocator,
// constants, fence value and slot of queries: every pass is timed and the view splat's pipeline statistics taken, and a
// frame's measurements come back with the Render two frames after it, through TakeStatistics.
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
  void Render(const NeuronCore::PerspectiveView& _view, const FrameSettings& _settings);

  [[nodiscard]] const GraphicsDevice& Device() const noexcept
  {
    return m_device;
  }

  // The 2D overlay the next Render draws over the frame, in pixels of the frame: collect into it, then call Render.
  [[nodiscard]] Canvas& Overlay() noexcept
  {
    return m_canvas;
  }

  // The measurements of the frames the GPU has finished since the last call, oldest first (§8).
  [[nodiscard]] std::vector<FrameStatistics> TakeStatistics();

  // Waits for the GPU, so that the next TakeStatistics holds every frame rendered so far.
  void FinishFrames();

  // The number the next Render's frame carries in its statistics: the frames rendered so far.
  [[nodiscard]] std::uint64_t NextFrame() const noexcept
  {
    return m_frameNumber;
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

  // The view splat for the permutation the explosion's time calls for and the variant the frame asks for.
  [[nodiscard]] const SplatPass& ViewSplat(bool _exploding, SplatPass::Variant _variant) const noexcept;

  GraphicsDevice m_device;
  DescriptorHeap m_rtvHeap;
  DescriptorHeap m_dsvHeap;
  DescriptorHeap m_shaderHeap;
  DescriptorHeap m_cpuHeap;
  SwapChain m_swapChain;
  ViewTargets m_targets;
  NeuronCore::OrthographicView m_shadowView;
  NeuronCore::ExplosionParameters m_explosion;
  ShadowMap m_shadowMap;
  VoxelScene m_scene;
  SplatPass m_shadowSplat;
  SplatPass m_viewSplat;
  SplatPass m_shadowSplatOriented;
  SplatPass m_viewSplatOriented;
  SplatPass m_viewSplatPlainDepth;
  SplatPass m_viewSplatOrientedPlainDepth;
  SplatPass m_viewSplatOverdraw;
  SplatPass m_viewSplatOrientedOverdraw;
  CoveragePass m_coverage;
  LightingPass m_lighting;
  ToneMapPass m_toneMap;
  DebugViewPass m_debugView;
  Canvas m_canvas;
  FrameQueries m_queries;
  std::array<Frame, FRAMES_IN_FLIGHT> m_frames;
  winrt::com_ptr<ID3D12GraphicsCommandList> m_list;
  std::vector<FrameStatistics> m_statistics;
  std::uint32_t m_frameIndex = 0;
  std::uint64_t m_frameNumber = 0;
};

} // namespace NeuronClient
