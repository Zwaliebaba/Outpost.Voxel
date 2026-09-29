#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>

#include "BloomChain.h"
#include "BloomPass.h"
#include "Canvas.h"
#include "CapturedFrame.h"
#include "CoveragePass.h"
#include "DebugViewPass.h"
#include "DescriptorHeap.h"
#include "FrameQueries.h"
#include "GasShellPass.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "LightingPass.h"
#include "ShadowMap.h"
#include "SkyPass.h"
#include "SplatPass.h"
#include "SwapChain.h"
#include "ToneMapPass.h"
#include "UploadRing.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "Blast.h"
#include "DebugView.h"
#include "Fragmentation.h"
#include "Lighting.h"
#include "Message.h"
#include "OrthographicView.h"
#include "PerspectiveView.h"
#include "Placement.h"
#include "Sky.h"
#include "StarCatalog.h"
#include "VoxModel.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace NeuronClient
{

struct RendererDesc
{
  GraphicsDeviceDesc device;
  HWND window;
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  NeuronCore::OrthographicView shadowView;       // the sun's, fitted to the scene (§10); its size is the shadow map's
  std::span<const NeuronCore::StarRecord> stars; // the world's catalog (Design/Archive/SpaceScene.md §11.2), copied once
};

// What a frame shows (§11, §13), what it measures besides (§9.3, §14), and whether it is kept (Design/ADR/ADR-031).
struct FrameSettings
{
  std::optional<NeuronCore::DebugView> debugView; // empty: the lit image
  NeuronCore::LightingParameters lighting;
  NeuronCore::SkyParameters sky; // Design/Archive/SpaceScene.md §11
  float exposure;
  bool vsync;
  bool plainDepth;    // the view splat writes SV_Depth rather than conservative depth (§9.3); the overdraw view overrides it
  bool countCoverage; // counts the pixels a voxel covers (§14)
  std::span<const NeuronCore::Blast> blasts; // the detonations whose light the frame shows (Design/ADR/ADR-025)
  bool capture;                              // copies the frame as it is presented back to the CPU, for TakeCapture (Design/ADR/ADR-031)
};

// The frame of Design/Archive/SampleRenderer.md §8 and Design/Archive/SpaceScene.md §8: the shadow splat into the
// shadow map and the view splat into the depth and visibility buffers, then the lighting into HDR color, the sky over
// every pixel no voxel covers, the detonations' shells of gas added over both (Design/ADR/ADR-025), bloom's chain from
// it and the tone map into the back buffer, or a debug view in their place, and last the canvas over it all (§13). What
// the splats draw is the frame's placements (Design/Archive/SpaceScene.md §7): each view culls them by their spheres
// and draws each it keeps with one draw, through the aligned permutation when it is whole and turned by a symmetry of
// the cube and the oriented one otherwise, the camera nearest first (§7.4). Two frames are in flight, each with its own
// allocator, constants, fence value and slot of queries: every pass is timed and the view splat's pipeline statistics
// taken, and a frame's measurements and draw counts come back with the Render two frames after it, through
// TakeStatistics.
class Renderer
{
public:
  static constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;

  // _models are the scene's, whose records and palettes the placements name (NeuronCore::SceneRecords); _fragments what
  // they break into, the ones the placements' detonations view (Design/ADR/ADR-024); and _sides the colors of the world's
  // sides, whose palettes the placements name too (NeuronCore::SidePaletteIndex).
  Renderer(const RendererDesc& _desc, std::span<const NeuronCore::VoxModel> _models, const NeuronCore::SceneFragments& _fragments,
           std::span<const NeuronCore::SideColor> _sides);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer&&) = delete;

  // Follows the window's client area. A zero size, a minimized window, renders nothing until the next resize.
  void Resize(std::uint32_t _widthPixels, std::uint32_t _heightPixels);

  // Moves the sun's view from the next Render on. The space scene fits one view to its whole layout until S-M7's
  // cascades, and moves it only when something reaches beyond it (Design/Archive/SpaceScene.md §10). Throws
  // std::invalid_argument for a view of another size than the shadow map.
  void SetShadowView(const NeuronCore::OrthographicView& _view);

  // Renders and presents one frame of _placements, whose ids NeuronCore::AssignVoxelIds gave them. _view must be the size
  // the renderer was last resized to. Throws std::invalid_argument, before recording anything, for a placement that names
  // records or a palette the scene lacks, or ids that fall back, overlap or reach NO_VOXEL (Design/Archive/SpaceScene.md §7.3).
  // On a failure, the catch block adds Device().DescribeRemoval() to its message while the device still exists (§13).
  void Render(const NeuronCore::PerspectiveView& _view, std::span<const NeuronCore::Placement> _placements, const FrameSettings& _settings);

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

  // The frame the last Render asked to capture drew, as it was presented, once the GPU has finished it (Design/ADR/ADR-031).
  // Nothing when no frame has been captured since the last call.
  [[nodiscard]] std::optional<CapturedFrame> TakeCapture();

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
    DrawCounts draws{};
  };

  // A frame's copy on its way back, and the fence value its frame signals when the GPU has finished it.
  struct PendingCapture
  {
    TextureReadback readback;
    std::uint64_t fenceValue;
  };

  // The view splat for the variant the frame asks for.
  [[nodiscard]] const SplatPass& ViewSplat(SplatPass::Variant _variant) const noexcept;

  GraphicsDevice m_device;
  DescriptorHeap m_rtvHeap;
  DescriptorHeap m_dsvHeap;
  DescriptorHeap m_shaderHeap;
  DescriptorHeap m_cpuHeap;
  SwapChain m_swapChain;
  ViewTargets m_targets;
  NeuronCore::OrthographicView m_shadowView;
  ShadowMap m_shadowMap;
  VoxelScene m_scene;
  SplatPass m_shadowSplat;
  SplatPass m_viewSplat;
  SplatPass m_viewSplatPlainDepth;
  SplatPass m_viewSplatOverdraw;
  CoveragePass m_coverage;
  LightingPass m_lighting;
  SkyPass m_sky;
  GasShellPass m_gasShell;
  BloomChain m_bloomChain;
  BloomPass m_bloom;
  ToneMapPass m_toneMap;
  DebugViewPass m_debugView;
  Canvas m_canvas;
  FrameQueries m_queries;
  std::array<Frame, FRAMES_IN_FLIGHT> m_frames;
  winrt::com_ptr<ID3D12GraphicsCommandList> m_list;
  std::vector<FrameStatistics> m_statistics;
  std::optional<PendingCapture> m_capture;
  std::uint32_t m_frameIndex = 0;
  std::uint64_t m_frameNumber = 0;
};

} // namespace NeuronClient
