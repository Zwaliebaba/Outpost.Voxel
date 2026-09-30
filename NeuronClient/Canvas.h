#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>
#include <dwrite_2.h>

#include "CanvasQuad.h"
#include "GlyphAtlas.h"
#include "Surface.h"
#include "UploadRing.h"

#include "Float3.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NeuronClient
{

class DescriptorHeap;
class GraphicsDevice;

// The 2D overlay drawn over the finished frame, and the surface the interface and the world's overlay draw on
// (Design/Archive/SampleRenderer.md §13, Design/ADR/ADR-010, ADR-034). During a frame it collects rectangles, text and
// segments: DirectWrite lays text out with IDWriteTextLayout and hands each glyph run to the canvas's own
// IDWriteTextRenderer, which makes every glyph a quad over its bitmap in the glyph atlas. Record draws the lot, in the
// order it came, with one instanced draw, and starts the next collection. Direct2D takes no part: DirectWrite lays out
// and rasterizes on the CPU, and Direct3D 12 does the rest.
class Canvas final : public Surface
{
public:
  // Quads beyond this many in one frame are dropped.
  static constexpr std::uint32_t MAX_QUADS = 16384;

  // _targetFormat is the format of the render target view Record draws through; the application's is the swap chain's
  // sRGB view, so the blend runs in linear light. _slots is the number of frames in flight.
  Canvas(const GraphicsDevice& _device, DescriptorHeap& _shaderHeap, DXGI_FORMAT _targetFormat, std::uint32_t _slots);

  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;
  Canvas(Canvas&&) = delete;
  Canvas& operator=(Canvas&&) = delete;
  ~Canvas() override = default;

  void FillRectangle(std::int32_t _xPixels, std::int32_t _yPixels, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                     NeuronCore::Float3 _color, float _alpha) override;
  TextExtent Print(std::wstring_view _text, float _xPixels, float _yPixels, const TextStyle& _style, NeuronCore::Float3 _color,
                   float _alpha) override;
  [[nodiscard]] TextExtent Measure(std::wstring_view _text, const TextStyle& _style) override;

  // A segment is a quad sloped along it (Design/ADR/ADR-034), whose pixels the segment covers in part at its edge.
  void DrawSegment(NeuronCore::Float2 _start, NeuronCore::Float2 _end, float _widthPixels, NeuronCore::Float3 _color,
                   float _alpha) override;

  // Draws what was collected since the last Record over the render target bound on _list, which is _widthPixels by
  // _heightPixels and in the canvas's format, and starts the next collection. The glyphs new since the last Record are
  // uploaded first, through _slot's memory, whose previous frame the GPU must have finished. The shader-visible heap
  // must be set on _list.
  void Record(ID3D12GraphicsCommandList* _list, std::uint32_t _slot, std::uint32_t _widthPixels, std::uint32_t _heightPixels);

  // What the next Record draws, in order.
  [[nodiscard]] std::span<const CanvasQuad> Quads() const noexcept
  {
    return m_quads;
  }

  [[nodiscard]] const GlyphAtlas& Atlas() const noexcept
  {
    return m_atlas;
  }

private:
  struct Format
  {
    std::wstring fontFamily;
    float sizePixels;
    std::uint16_t weight;
    winrt::com_ptr<IDWriteTextFormat> format;
  };

  [[nodiscard]] IDWriteTextFormat* FormatOf(const TextStyle& _style);
  [[nodiscard]] winrt::com_ptr<IDWriteTextLayout> Layout(std::wstring_view _text, const TextStyle& _style);

  winrt::com_ptr<IDWriteFactory2> m_factory;
  GlyphAtlas m_atlas;
  winrt::com_ptr<IDWriteTextRenderer> m_renderer;
  std::vector<Format> m_formats;
  std::vector<CanvasQuad> m_quads;
  std::vector<std::unique_ptr<UploadRing>> m_quadRings; // one for each frame in flight
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace NeuronClient
