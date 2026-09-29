#include "pch.h"

#include "Canvas.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "Shaders.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <optional>

namespace NeuronClient
{
namespace
{

enum RootParameter : std::uint8_t
{
  TargetSizeParameter,
  QuadsParameter,
  AtlasParameter,
  RootParameterCount
};

// A text is laid out in a box this large, so that nothing wraps or clips: lines break only at '\n'.
constexpr float LAYOUT_LIMIT_PIXELS = 1.0e5f;

// What Print hands DirectWrite as the drawing context, and DirectWrite hands back with every run of the text. A callback
// cannot throw through DirectWrite, which does not pass a failed HRESULT on from it either, so the first exception is
// kept here and Print rethrows it.
struct TextRun
{
  GlyphAtlas* atlas;
  std::vector<CanvasQuad>* quads;
  NeuronCore::Float3 color;
  float alpha;
  std::exception_ptr failure;
};

// Keeps the exception being handled, unless an earlier one is kept already, and returns its HRESULT for DirectWrite.
[[nodiscard]] HRESULT Keep(TextRun& _run) noexcept
{
  if (!_run.failure)
  {
    _run.failure = std::current_exception();
  }
  return winrt::to_hresult();
}

void AddQuad(std::vector<CanvasQuad>& _quads, const CanvasQuad& _quad)
{
  if (_quads.size() < Canvas::MAX_QUADS)
  {
    _quads.push_back(_quad);
  }
}

// A glyph's or a fill's quad, over its rectangle.
[[nodiscard]] CanvasQuad RectangleQuad(std::int32_t _xPixels, std::int32_t _yPixels, std::uint32_t _widthPixels,
                                       std::uint32_t _heightPixels, std::uint32_t _atlasX, std::uint32_t _atlasY, CanvasQuadKind _kind,
                                       NeuronCore::Float3 _color, float _alpha) noexcept
{
  return {_xPixels, _yPixels, _widthPixels, _heightPixels, _atlasX, _atlasY, static_cast<std::uint32_t>(_kind),
          _color,   _alpha,   0.0f,         0.0f,          0.0f,    0.0f,    0.0f};
}

// An underline or strikethrough: a fill along the baseline at _offset below it, _thickness thick and at least a pixel.
void AddLine(TextRun& _run, float _baselineOriginX, float _baselineOriginY, float _offset, float _width, float _thickness,
             bool _rightToLeft)
{
  const float left = _rightToLeft ? _baselineOriginX - _width : _baselineOriginX;
  AddQuad(*_run.quads,
          RectangleQuad(static_cast<std::int32_t>(std::lround(left)), static_cast<std::int32_t>(std::lround(_baselineOriginY + _offset)),
                        static_cast<std::uint32_t>(std::max(1L, std::lround(_width))),
                        static_cast<std::uint32_t>(std::max(1L, std::lround(_thickness))), 0u, 0u, CanvasQuadKind::Fill, _run.color,
                        _run.alpha));
}

// The canvas's IDWriteTextRenderer. DirectWrite calls it once for every glyph run, underline and strikethrough of a text
// it draws, and it turns them into quads. Pixel snapping stays on and a DIP is a pixel, so that every baseline lands on
// a whole pixel of the target.
struct GlyphRunSink : winrt::implements<GlyphRunSink, IDWriteTextRenderer>
{
  HRESULT __stdcall IsPixelSnappingDisabled(void* /*_context*/, BOOL* _disabled) noexcept override
  {
    *_disabled = FALSE;
    return S_OK;
  }

  HRESULT __stdcall GetCurrentTransform(void* /*_context*/, DWRITE_MATRIX* _transform) noexcept override
  {
    *_transform = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    return S_OK;
  }

  HRESULT __stdcall GetPixelsPerDip(void* /*_context*/, FLOAT* _pixelsPerDip) noexcept override
  {
    *_pixelsPerDip = 1.0f;
    return S_OK;
  }

  // Each glyph's origin advances along the baseline, leftwards in a right-to-left run, and its bitmap sits at its offset
  // from the origin rounded to the pixel.
  HRESULT __stdcall DrawGlyphRun(void* _context, FLOAT _baselineOriginX, FLOAT _baselineOriginY, DWRITE_MEASURING_MODE /*_measuringMode*/,
                                 const DWRITE_GLYPH_RUN* _glyphRun, const DWRITE_GLYPH_RUN_DESCRIPTION* /*_description*/,
                                 IUnknown* /*_effect*/) noexcept override
  {
    TextRun& run = *static_cast<TextRun*>(_context);
    try
    {
      const bool rightToLeft = (_glyphRun->bidiLevel & 1u) != 0u;
      float penX = _baselineOriginX;
      for (UINT32 i = 0; i < _glyphRun->glyphCount; ++i)
      {
        const float advance = _glyphRun->glyphAdvances != nullptr ? _glyphRun->glyphAdvances[i] : 0.0f;
        float originX = rightToLeft ? penX - advance : penX;
        float originY = _baselineOriginY;
        if (_glyphRun->glyphOffsets != nullptr)
        {
          originX += rightToLeft ? -_glyphRun->glyphOffsets[i].advanceOffset : _glyphRun->glyphOffsets[i].advanceOffset;
          originY -= _glyphRun->glyphOffsets[i].ascenderOffset;
        }
        penX += rightToLeft ? -advance : advance;
        const std::optional<GlyphAtlas::Glyph> glyph =
          run.atlas->Find(_glyphRun->fontFace, _glyphRun->fontEmSize, _glyphRun->glyphIndices[i], _glyphRun->isSideways != FALSE);
        if (!glyph || glyph->widthPixels == 0)
        {
          continue;
        }
        AddQuad(*run.quads, RectangleQuad(static_cast<std::int32_t>(std::lround(originX)) + glyph->offsetX,
                                          static_cast<std::int32_t>(std::lround(originY)) + glyph->offsetY, glyph->widthPixels,
                                          glyph->heightPixels, glyph->atlasX, glyph->atlasY, CanvasQuadKind::Glyph, run.color, run.alpha));
      }
      return S_OK;
    }
    catch (...)
    {
      return Keep(run);
    }
  }

  HRESULT __stdcall DrawUnderline(void* _context, FLOAT _baselineOriginX, FLOAT _baselineOriginY, const DWRITE_UNDERLINE* _underline,
                                  IUnknown* /*_effect*/) noexcept override
  {
    TextRun& run = *static_cast<TextRun*>(_context);
    try
    {
      AddLine(run, _baselineOriginX, _baselineOriginY, _underline->offset, _underline->width, _underline->thickness,
              _underline->readingDirection == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
      return S_OK;
    }
    catch (...)
    {
      return Keep(run);
    }
  }

  HRESULT __stdcall DrawStrikethrough(void* _context, FLOAT _baselineOriginX, FLOAT _baselineOriginY,
                                      const DWRITE_STRIKETHROUGH* _strikethrough, IUnknown* /*_effect*/) noexcept override
  {
    TextRun& run = *static_cast<TextRun*>(_context);
    try
    {
      AddLine(run, _baselineOriginX, _baselineOriginY, _strikethrough->offset, _strikethrough->width, _strikethrough->thickness,
              _strikethrough->readingDirection == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
      return S_OK;
    }
    catch (...)
    {
      return Keep(run);
    }
  }

  // The canvas lays out no inline objects.
  HRESULT __stdcall DrawInlineObject(void* /*_context*/, FLOAT /*_originX*/, FLOAT /*_originY*/, IDWriteInlineObject* /*_inlineObject*/,
                                     BOOL /*_isSideways*/, BOOL /*_isRightToLeft*/, IUnknown* /*_effect*/) noexcept override
  {
    return E_NOTIMPL;
  }
};

[[nodiscard]] winrt::com_ptr<IDWriteFactory2> CreateDirectWriteFactory()
{
  winrt::com_ptr<IUnknown> factory;
  winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), factory.put()));
  return factory.as<IDWriteFactory2>();
}

[[nodiscard]] TextExtent ExtentOf(IDWriteTextLayout* _layout)
{
  DWRITE_TEXT_METRICS metrics{};
  winrt::check_hresult(_layout->GetMetrics(&metrics));
  return {metrics.widthIncludingTrailingWhitespace, metrics.height};
}

} // namespace

Canvas::Canvas(const GraphicsDevice& _device, DescriptorHeap& _shaderHeap, DXGI_FORMAT _targetFormat, std::uint32_t _slots)
  : m_factory(CreateDirectWriteFactory()),
    m_atlas(_device, m_factory.get(), _shaderHeap, _slots),
    m_renderer(winrt::make_self<GlyphRunSink>().as<IDWriteTextRenderer>())
{
  m_quadRings.reserve(_slots);
  for (std::uint32_t slot = 0; slot < _slots; ++slot)
  {
    // A piece of an upload ring is aligned for a constant buffer; one piece more than the quads covers the alignment.
    m_quadRings.push_back(std::make_unique<UploadRing>(
      _device, std::uint64_t{MAX_QUADS} * sizeof(CanvasQuad) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, L"Canvas quads"));
  }

  const D3D12_DESCRIPTOR_RANGE atlasRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0};
  std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
  parameters[TargetSizeParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[TargetSizeParameter].Constants = {0, 0, 2};
  parameters[TargetSizeParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[QuadsParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[QuadsParameter].Descriptor = {0, 0};
  parameters[QuadsParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[AtlasParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[AtlasParameter].DescriptorTable = {1, &atlasRange};
  parameters[AtlasParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  const D3D12_ROOT_SIGNATURE_DESC rootSignature{static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
                                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                  D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                  D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};
  m_rootSignature = CreateRootSignature(_device, rootSignature, L"Canvas root signature");

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = DefaultGraphicsPipeline();
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = CanvasVertexShader();
  pipeline.PS = CanvasPixelShader();
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.DepthStencilState.DepthEnable = FALSE;
  pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  // Premultiplied alpha over the target: the shader writes the color already scaled by its alpha (CanvasBlend's twin).
  D3D12_RENDER_TARGET_BLEND_DESC& blend = pipeline.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_ONE;
  blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  pipeline.NumRenderTargets = 1;
  pipeline.RTVFormats[0] = _targetFormat;
  winrt::check_hresult(_device.Device()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));
  m_pipeline->SetName(L"Canvas");
}

void Canvas::FillRectangle(std::int32_t _xPixels, std::int32_t _yPixels, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                           NeuronCore::Float3 _color, float _alpha)
{
  AddQuad(m_quads, RectangleQuad(_xPixels, _yPixels, _widthPixels, _heightPixels, 0u, 0u, CanvasQuadKind::Fill, _color, _alpha));
}

void Canvas::DrawSegment(NeuronCore::Float2 _start, NeuronCore::Float2 _end, float _widthPixels, NeuronCore::Float3 _color, float _alpha)
{
  AddQuad(m_quads, {0, 0, 0u, 0u, 0u, 0u, static_cast<std::uint32_t>(CanvasQuadKind::Segment), _color, _alpha, _start.x, _start.y, _end.x,
                    _end.y, 0.5f * _widthPixels});
}

TextExtent Canvas::Print(std::wstring_view _text, float _xPixels, float _yPixels, const TextStyle& _style, NeuronCore::Float3 _color,
                         float _alpha)
{
  const winrt::com_ptr<IDWriteTextLayout> layout = Layout(_text, _style);
  TextRun run{&m_atlas, &m_quads, _color, _alpha, nullptr};
  const HRESULT drawn = layout->Draw(&run, m_renderer.get(), _xPixels, _yPixels);
  if (run.failure)
  {
    std::rethrow_exception(run.failure);
  }
  winrt::check_hresult(drawn);
  return ExtentOf(layout.get());
}

TextExtent Canvas::Measure(std::wstring_view _text, const TextStyle& _style)
{
  return ExtentOf(Layout(_text, _style).get());
}

void Canvas::Record(ID3D12GraphicsCommandList* _list, std::uint32_t _slot, std::uint32_t _widthPixels, std::uint32_t _heightPixels)
{
  m_atlas.Upload(_list, _slot);
  if (!m_quads.empty() && _widthPixels != 0 && _heightPixels != 0)
  {
    UploadRing& ring = *m_quadRings[_slot];
    ring.Reset();
    const D3D12_GPU_VIRTUAL_ADDRESS quads = ring.PushBytes(std::as_bytes(std::span<const CanvasQuad>(m_quads)));
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(_widthPixels), static_cast<float>(_heightPixels), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(_widthPixels), static_cast<LONG>(_heightPixels)};
    _list->RSSetViewports(1, &viewport);
    _list->RSSetScissorRects(1, &scissor);
    _list->SetGraphicsRootSignature(m_rootSignature.get());
    _list->SetPipelineState(m_pipeline.get());
    _list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    _list->SetGraphicsRoot32BitConstant(TargetSizeParameter, _widthPixels, 0);
    _list->SetGraphicsRoot32BitConstant(TargetSizeParameter, _heightPixels, 1);
    _list->SetGraphicsRootShaderResourceView(QuadsParameter, quads);
    _list->SetGraphicsRootDescriptorTable(AtlasParameter, m_atlas.Table());
    _list->DrawInstanced(4, static_cast<UINT>(m_quads.size()), 0, 0);
  }
  m_quads.clear();
  // The frame just recorded reads the atlas as uploaded above; starting over changes only what later frames upload.
  m_atlas.StartOverIfFull();
}

IDWriteTextFormat* Canvas::FormatOf(const TextStyle& _style)
{
  for (const Format& format : m_formats)
  {
    if (format.fontFamily == _style.fontFamily && format.sizePixels == _style.sizePixels && format.weight == _style.weight)
    {
      return format.format.get();
    }
  }
  winrt::com_ptr<IDWriteTextFormat> format;
  winrt::check_hresult(m_factory->CreateTextFormat(_style.fontFamily, nullptr, static_cast<DWRITE_FONT_WEIGHT>(_style.weight),
                                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, _style.sizePixels, L"en-us",
                                                   format.put()));
  winrt::check_hresult(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
  m_formats.push_back({_style.fontFamily, _style.sizePixels, _style.weight, format});
  return m_formats.back().format.get();
}

winrt::com_ptr<IDWriteTextLayout> Canvas::Layout(std::wstring_view _text, const TextStyle& _style)
{
  // GDI-compatible natural metrics at one pixel per DIP: whole-pixel advances, as the atlas rasterizes for.
  static_assert(GlyphAtlas::MEASURING_MODE == DWRITE_MEASURING_MODE_GDI_NATURAL);
  winrt::com_ptr<IDWriteTextLayout> layout;
  winrt::check_hresult(m_factory->CreateGdiCompatibleTextLayout(_text.empty() ? L"" : _text.data(), static_cast<UINT32>(_text.size()),
                                                                FormatOf(_style), LAYOUT_LIMIT_PIXELS, LAYOUT_LIMIT_PIXELS, 1.0f, nullptr,
                                                                TRUE, layout.put()));
  return layout;
}

} // namespace NeuronClient
