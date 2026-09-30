#include "pch.h"

#include "Canvas.h"
#include "CanvasQuad.h"
#include "CanvasShading.h"
#include "DescriptorHeap.h"
#include "GlyphAtlas.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "TestSupport.h"

#include "Float3.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Float4;

constexpr DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr std::uint32_t COLOR_BYTES_PER_PIXEL = 16;
// The blend runs in single precision on both sides; the two agree to rounding.
constexpr float COLOR_TOLERANCE = 1.0e-5f;

constexpr NeuronClient::TextStyle MONOSPACED{L"Consolas", 15.0f, NeuronClient::REGULAR_WEIGHT};
constexpr NeuronClient::TextStyle PROPORTIONAL{L"Segoe UI", 13.0f, NeuronClient::BOLD_WEIGHT};

// A DirectWrite factory of the test's own: the shared one, which the canvas gets too.
[[nodiscard]] winrt::com_ptr<IDWriteFactory2> DirectWriteFactory()
{
  winrt::com_ptr<IUnknown> factory;
  winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), factory.put()));
  return factory.as<IDWriteFactory2>();
}

// The regular face of an installed family.
[[nodiscard]] winrt::com_ptr<IDWriteFontFace> SystemFontFace(IDWriteFactory* _factory, const wchar_t* _family)
{
  winrt::com_ptr<IDWriteFontCollection> collection;
  winrt::check_hresult(_factory->GetSystemFontCollection(collection.put(), FALSE));
  UINT32 index = 0;
  BOOL exists = FALSE;
  winrt::check_hresult(collection->FindFamilyName(_family, &index, &exists));
  Assert::IsTrue(exists != FALSE, std::format(L"{} is installed", _family).c_str());
  winrt::com_ptr<IDWriteFontFamily> family;
  winrt::check_hresult(collection->GetFontFamily(index, family.put()));
  winrt::com_ptr<IDWriteFont> font;
  winrt::check_hresult(
    family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.put()));
  winrt::com_ptr<IDWriteFontFace> face;
  winrt::check_hresult(font->CreateFontFace(face.put()));
  return face;
}

// A text renderer that only counts what DirectWrite hands it, the way the canvas's own receives it.
struct RunCounter : winrt::implements<RunCounter, IDWriteTextRenderer>
{
  std::uint32_t runs = 0;
  std::uint32_t glyphs = 0;

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

  HRESULT __stdcall DrawGlyphRun(void* /*_context*/, FLOAT /*_baselineOriginX*/, FLOAT /*_baselineOriginY*/,
                                 DWRITE_MEASURING_MODE /*_measuringMode*/, const DWRITE_GLYPH_RUN* _glyphRun,
                                 const DWRITE_GLYPH_RUN_DESCRIPTION* /*_description*/, IUnknown* /*_effect*/) noexcept override
  {
    ++runs;
    glyphs += _glyphRun->glyphCount;
    return S_OK;
  }

  HRESULT __stdcall DrawUnderline(void* /*_context*/, FLOAT /*_baselineOriginX*/, FLOAT /*_baselineOriginY*/,
                                  const DWRITE_UNDERLINE* /*_underline*/, IUnknown* /*_effect*/) noexcept override
  {
    return S_OK;
  }

  HRESULT __stdcall DrawStrikethrough(void* /*_context*/, FLOAT /*_baselineOriginX*/, FLOAT /*_baselineOriginY*/,
                                      const DWRITE_STRIKETHROUGH* /*_strikethrough*/, IUnknown* /*_effect*/) noexcept override
  {
    return S_OK;
  }

  HRESULT __stdcall DrawInlineObject(void* /*_context*/, FLOAT /*_originX*/, FLOAT /*_originY*/, IDWriteInlineObject* /*_inlineObject*/,
                                     BOOL /*_isSideways*/, BOOL /*_isRightToLeft*/, IUnknown* /*_effect*/) noexcept override
  {
    return E_NOTIMPL;
  }
};

[[nodiscard]] bool Near(Float4 _a, Float4 _b) noexcept
{
  return std::abs(_a.x - _b.x) <= COLOR_TOLERANCE && std::abs(_a.y - _b.y) <= COLOR_TOLERANCE && std::abs(_a.z - _b.z) <= COLOR_TOLERANCE &&
         std::abs(_a.w - _b.w) <= COLOR_TOLERANCE;
}

} // namespace

// The canvas (Design/Archive/SampleRenderer.md §13, Design/ADR/ADR-010): its twin, and what the GPU draws against the twin's
// composite of the same quads over the same atlas (R15).
TEST_CLASS(CanvasTests)
{
public:
  // Chosen so that every value is exact in binary: the corners are the rectangle's, in NDC with y up.
  TEST_METHOD(CornersSpanTheRectangle)
  {
    const std::array<NeuronCore::Float2, 4> expected{{{-0.75f, 0.75f}, {-0.25f, 0.75f}, {-0.75f, 0.25f}, {-0.25f, 0.25f}}};
    for (std::uint32_t corner = 0; corner < 4; ++corner)
    {
      const NeuronCore::Float2 ndc = NeuronClient::CanvasCornerNdc(16, 8, 32, 16, corner, 128, 64);
      Assert::AreEqual(expected[corner].x, ndc.x, std::format(L"corner {}", corner).c_str());
      Assert::AreEqual(expected[corner].y, ndc.y, std::format(L"corner {}", corner).c_str());
    }
    const NeuronCore::Float2 offTarget = NeuronClient::CanvasCornerNdc(-64, -32, 64, 32, 0, 128, 64);
    Assert::AreEqual(-2.0f, offTarget.x, L"a corner left of the target");
    Assert::AreEqual(2.0f, offTarget.y, L"a corner above the target");
  }

  // ADR-034: a segment's quad reaches half its width and a pixel more past its ends and to either side. Chosen so that
  // every value is exact: from (8, 16) to (24, 16), 2 wide, on a target of 64 by 32. A segment of no length lies along +x.
  TEST_METHOD(SlopesASegmentsQuadAlongIt)
  {
    const std::array<NeuronCore::Float2, 4> expected{{{-0.8125f, 0.125f}, {-0.1875f, 0.125f}, {-0.8125f, -0.125f}, {-0.1875f, -0.125f}}};
    for (std::uint32_t corner = 0; corner < 4; ++corner)
    {
      const NeuronCore::Float2 ndc = NeuronClient::CanvasSegmentCornerNdc({8.0f, 16.0f}, {24.0f, 16.0f}, 1.0f, corner, 64, 32);
      Assert::AreEqual(expected[corner].x, ndc.x, std::format(L"corner {}", corner).c_str());
      Assert::AreEqual(expected[corner].y, ndc.y, std::format(L"corner {}", corner).c_str());
    }
    const NeuronCore::Float2 dot = NeuronClient::CanvasSegmentCornerNdc({16.0f, 16.0f}, {16.0f, 16.0f}, 0.5f, 3, 64, 32);
    Assert::AreEqual(2.0f * 17.5f / 64.0f - 1.0f, dot.x, L"a dot's quad about it");
    Assert::AreEqual(1.0f - 2.0f * 17.5f / 32.0f, dot.y);
  }

  // All of a pixel within half a pixel less than half the width, none beyond half a pixel more, in proportion between,
  // with round ends.
  TEST_METHOD(CoversASegmentsPixels)
  {
    constexpr NeuronCore::Float2 START{8.0f, 16.0f};
    constexpr NeuronCore::Float2 END{24.0f, 16.0f};
    Assert::AreEqual(1.0f, NeuronClient::CanvasSegmentCoverage({16.0f, 16.0f}, START, END, 1.0f), L"on the segment");
    Assert::AreEqual(0.5f, NeuronClient::CanvasSegmentCoverage({16.0f, 17.0f}, START, END, 1.0f), L"at its edge");
    Assert::AreEqual(0.0f, NeuronClient::CanvasSegmentCoverage({16.0f, 17.5f}, START, END, 1.0f), L"half a pixel beyond");
    Assert::AreEqual(0.5f, NeuronClient::CanvasSegmentCoverage({25.0f, 16.0f}, START, END, 1.0f), L"at its round end");
    Assert::AreEqual(1.0f, NeuronClient::CanvasSegmentCoverage({10.5f, 10.0f}, {10.0f, 10.0f}, {10.0f, 10.0f}, 1.0f), L"a dot");
  }

  TEST_METHOD(GlyphPixelsMapToTheirTexels)
  {
    std::int32_t texelX = 0;
    std::int32_t texelY = 0;
    NeuronClient::CanvasAtlasTexel(10, 20, 10, 20, 300, 40, texelX, texelY);
    Assert::AreEqual(300, texelX, L"the bitmap's first texel at the glyph's corner");
    Assert::AreEqual(40, texelY);
    NeuronClient::CanvasAtlasTexel(12, 25, -3, 20, 300, 40, texelX, texelY);
    Assert::AreEqual(315, texelX, L"a glyph that starts left of the target");
    Assert::AreEqual(45, texelY);
  }

  TEST_METHOD(PremultipliesAndBlendsOver)
  {
    const Float4 premultiplied = NeuronClient::CanvasPremultiply({1.0f, 0.5f, 0.25f}, 0.5f, 0.5f);
    Assert::AreEqual(0.25f, premultiplied.x);
    Assert::AreEqual(0.125f, premultiplied.y);
    Assert::AreEqual(0.0625f, premultiplied.z);
    Assert::AreEqual(0.25f, premultiplied.w);
    const Float4 blended = NeuronClient::CanvasBlend(premultiplied, {0.5f, 0.5f, 0.5f, 1.0f});
    Assert::AreEqual(0.625f, blended.x, L"a quarter of the source's alpha lets three quarters of the target through");
    Assert::AreEqual(0.5f, blended.y);
    Assert::AreEqual(0.4375f, blended.z);
    Assert::AreEqual(1.0f, blended.w);
    Assert::AreEqual(0.0f, NeuronClient::AtlasCoverage(0));
    Assert::AreEqual(1.0f, NeuronClient::AtlasCoverage(255));
  }

  // DirectWrite lays the digits out as the canvas asks, GDI-compatible at a pixel per DIP, and hands every glyph to a
  // text renderer written with C++/WinRT, as the canvas's is.
  TEST_METHOD(HandsEveryGlyphToItsRenderer)
  {
    const winrt::com_ptr<IDWriteFactory2> factory = DirectWriteFactory();
    winrt::com_ptr<IDWriteTextFormat> format;
    winrt::check_hresult(factory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", format.put()));
    winrt::com_ptr<IDWriteTextLayout> layout;
    winrt::check_hresult(
      factory->CreateGdiCompatibleTextLayout(L"0123456789", 10, format.get(), 1.0e5f, 1.0e5f, 1.0f, nullptr, TRUE, layout.put()));
    DWRITE_TEXT_METRICS metrics{};
    winrt::check_hresult(layout->GetMetrics(&metrics));
    const winrt::com_ptr<RunCounter> counter = winrt::make_self<RunCounter>();
    winrt::check_hresult(layout->Draw(nullptr, counter.get(), 0.0f, 0.0f));
    Logger::WriteMessage(std::format(L"laid out {} x {} in {} lines; {} runs of {} glyphs drawn\n", metrics.width, metrics.height,
                                     metrics.lineCount, counter->runs, counter->glyphs)
                           .c_str());
    Assert::IsTrue(metrics.width > 0.0f, L"the digits take room");
    Assert::AreEqual(10u, counter->glyphs, L"every digit reaches the renderer");
  }

  // The atlas rasterizes a glyph of an installed face on its own, without a layout: it has ink, and a size.
  TEST_METHOD(RasterizesAGlyph)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const winrt::com_ptr<IDWriteFactory2> factory = DirectWriteFactory();
        const winrt::com_ptr<IDWriteFontFace> face = SystemFontFace(factory.get(), L"Consolas");
        const UINT32 zero = U'0';
        UINT16 index = 0;
        winrt::check_hresult(face->GetGlyphIndices(&zero, 1, &index));
        Assert::IsTrue(index != 0, L"Consolas has a zero");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, L"Test shader views");
        NeuronClient::GlyphAtlas atlas(_device, factory.get(), shaderHeap, 1);
        const std::optional<NeuronClient::GlyphAtlas::Glyph> glyph = atlas.Find(face.get(), 15.0f, index, false);
        Assert::IsTrue(glyph.has_value(), L"the zero fits the atlas");
        Logger::WriteMessage(std::format(L"a zero at 15 pixels: {} x {} at ({}, {}) from its origin, texel ({}, {})\n", glyph->widthPixels,
                                         glyph->heightPixels, glyph->offsetX, glyph->offsetY, glyph->atlasX, glyph->atlasY)
                               .c_str());
        Assert::IsTrue(glyph->widthPixels > 0 && glyph->heightPixels > 0, L"a zero has ink");
        std::uint32_t inked = 0;
        std::uint32_t partly = 0;
        for (std::uint32_t y = 0; y < glyph->heightPixels; ++y)
        {
          for (std::uint32_t x = 0; x < glyph->widthPixels; ++x)
          {
            const std::uint8_t texel =
              atlas.Texels()[static_cast<std::size_t>(glyph->atlasY + y) * NeuronClient::GlyphAtlas::SIZE_PIXELS + glyph->atlasX + x];
            inked += texel != 0 ? 1u : 0u;
            partly += texel != 0 && texel != 255 ? 1u : 0u;
          }
        }
        Logger::WriteMessage(std::format(L"{} texels with coverage, {} of them partly covered\n", inked, partly).c_str());
        Assert::IsTrue(inked > 0, L"the zero's texels have coverage");
        Assert::IsTrue(partly > 0, L"the edges are antialiased in grayscale, not bi-level");
        Assert::AreEqual(std::size_t{1}, atlas.GlyphCount());
      });
  }

  // A glyph is rasterized once per face and size, a space leaves no quad, and Measure agrees with Print.
  TEST_METHOD(RasterizesEachGlyphOnce)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, L"Test shader views");
        NeuronClient::Canvas canvas(_device, shaderHeap, COLOR_FORMAT, 1);
        const NeuronClient::TextExtent digits = canvas.Print(L"0123456789", 0.0f, 0.0f, MONOSPACED, {1.0f, 1.0f, 1.0f}, 1.0f);
        Logger::WriteMessage(std::format(L"the digits: {} x {} pixels, {} quads, {} glyphs in the atlas\n", digits.widthPixels,
                                         digits.heightPixels, canvas.Quads().size(), canvas.Atlas().GlyphCount())
                               .c_str());
        Assert::AreEqual(std::size_t{10}, canvas.Atlas().GlyphCount(), L"ten digits, ten glyphs");
        Assert::AreEqual(std::size_t{10}, canvas.Quads().size());
        const NeuronClient::TextExtent measured = canvas.Measure(L"0123456789", MONOSPACED);
        Assert::AreEqual(digits.widthPixels, measured.widthPixels, L"Measure lays out as Print does");
        Assert::AreEqual(digits.heightPixels, measured.heightPixels);
        Assert::IsTrue(digits.widthPixels > 0.0f && digits.heightPixels > 0.0f);

        canvas.Print(L"9876543210", 0.0f, 20.0f, MONOSPACED, {1.0f, 1.0f, 1.0f}, 1.0f);
        Assert::AreEqual(std::size_t{10}, canvas.Atlas().GlyphCount(), L"the same glyphs are found, not rasterized again");
        canvas.Print(L"0123456789", 0.0f, 40.0f, {L"Consolas", 30.0f, NeuronClient::REGULAR_WEIGHT}, {1.0f, 1.0f, 1.0f}, 1.0f);
        Assert::AreEqual(std::size_t{20}, canvas.Atlas().GlyphCount(), L"another size is another set of glyphs");

        const std::size_t before = canvas.Quads().size();
        canvas.Print(L"1 2", 0.0f, 80.0f, MONOSPACED, {1.0f, 1.0f, 1.0f}, 1.0f);
        Assert::AreEqual(before + 2, canvas.Quads().size(), L"a space is a glyph with no quad");
        Assert::AreEqual(std::size_t{21}, canvas.Atlas().GlyphCount(), L"the space is kept, empty, like any glyph");
      });
  }

  // §13, R15: panels and text in two faces, one run starting left of the target and one over another, drawn on WARP and
  // composited by the twin from the same quads and the atlas's CPU copy. Every pixel must agree, which also shows that
  // the atlas reached the GPU as the CPU holds it.
  TEST_METHOD(DrawsWhatTheTwinComposites)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        constexpr std::uint32_t WIDTH_PIXELS = 256;
        constexpr std::uint32_t HEIGHT_PIXELS = 96;
        constexpr Float4 BACKGROUND{0.1f, 0.2f, 0.3f, 1.0f};
        NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, L"Test render target views");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, L"Test shader views");
        NeuronClient::Canvas canvas(_device, shaderHeap, COLOR_FORMAT, 1);

        canvas.FillRectangle(4, 4, 200, 58, {0.0f, 0.0f, 0.0f}, 0.55f);
        canvas.Print(L"Frame 2.10 ms - GPU 1.23 ms", 10.0f, 8.0f, MONOSPACED, {1.0f, 1.0f, 1.0f}, 1.0f);
        canvas.Print(L"t 4.50 s \u00D7 2, \u00C5ngstr\u00F6m", 10.0f, 30.0f, PROPORTIONAL, {1.0f, 0.8f, 0.3f}, 0.85f);
        canvas.Print(L"left of the edge", -30.0f, 70.0f, MONOSPACED, {0.4f, 0.9f, 1.0f}, 1.0f);
        canvas.FillRectangle(180, 50, 60, 40, {0.9f, 0.1f, 0.1f}, 0.5f);
        canvas.Print(L"over", 190.0f, 60.0f, PROPORTIONAL, {1.0f, 1.0f, 1.0f}, 0.7f);
        const std::vector<NeuronClient::CanvasQuad> quads(canvas.Quads().begin(), canvas.Quads().end());
        const std::vector<std::uint8_t> texels(canvas.Atlas().Texels().begin(), canvas.Atlas().Texels().end());
        Assert::IsTrue(quads.size() > 40, std::format(L"{} quads", quads.size()).c_str());

        const D3D12_CLEAR_VALUE clear{COLOR_FORMAT, {{BACKGROUND.x, BACKGROUND.y, BACKGROUND.z, BACKGROUND.w}}};
        const winrt::com_ptr<ID3D12Resource> color =
          NeuronClient::CreateTexture2D(_device, COLOR_FORMAT, WIDTH_PIXELS, HEIGHT_PIXELS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, L"Test color");
        const std::uint32_t colorView = rtvHeap.Allocate();
        _device.Device()->CreateRenderTargetView(color.get(), nullptr, rtvHeap.Cpu(colorView));
        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            const D3D12_CPU_DESCRIPTOR_HANDLE target = rtvHeap.Cpu(colorView);
            _list->ClearRenderTargetView(target, clear.Color, 0, nullptr);
            _list->OMSetRenderTargets(1, &target, FALSE, nullptr);
            canvas.Record(_list, 0, WIDTH_PIXELS, HEIGHT_PIXELS);
          });
        Assert::IsTrue(canvas.Quads().empty(), L"Record starts the next collection");
        const std::vector<std::byte> bytes =
          NeuronClient::ReadTexture2D(_device, color.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, COLOR_BYTES_PER_PIXEL);
        std::vector<Float4> drawn(static_cast<std::size_t>(WIDTH_PIXELS) * HEIGHT_PIXELS);
        std::memcpy(static_cast<void*>(drawn.data()), bytes.data(), bytes.size());

        // The twin's composite: every quad in order, over the pixels whose centres it covers.
        std::vector<Float4> expected(drawn.size(), BACKGROUND);
        std::uint32_t inked = 0;
        for (const NeuronClient::CanvasQuad& quad : quads)
        {
          const std::int32_t top = std::max(quad.pixelY, 0);
          const std::int32_t bottom =
            std::min(quad.pixelY + static_cast<std::int32_t>(quad.heightPixels), static_cast<std::int32_t>(HEIGHT_PIXELS));
          const std::int32_t left = std::max(quad.pixelX, 0);
          const std::int32_t right =
            std::min(quad.pixelX + static_cast<std::int32_t>(quad.widthPixels), static_cast<std::int32_t>(WIDTH_PIXELS));
          for (std::int32_t y = top; y < bottom; ++y)
          {
            for (std::int32_t x = left; x < right; ++x)
            {
              float coverage = 1.0f;
              if (quad.kind == static_cast<std::uint32_t>(NeuronClient::CanvasQuadKind::Glyph))
              {
                std::int32_t texelX = 0;
                std::int32_t texelY = 0;
                NeuronClient::CanvasAtlasTexel(x, y, quad.pixelX, quad.pixelY, quad.atlasX, quad.atlasY, texelX, texelY);
                const std::uint8_t texel = texels[static_cast<std::size_t>(texelY) * NeuronClient::GlyphAtlas::SIZE_PIXELS + texelX];
                coverage = NeuronClient::AtlasCoverage(texel);
                inked += texel != 0 ? 1u : 0u;
              }
              Float4& pixel = expected[static_cast<std::size_t>(y) * WIDTH_PIXELS + x];
              pixel = NeuronClient::CanvasBlend(NeuronClient::CanvasPremultiply(quad.color, quad.alpha, coverage), pixel);
            }
          }
        }
        Assert::IsTrue(inked > 400, std::format(L"{} glyph pixels have ink", inked).c_str());

        std::uint32_t failures = 0;
        for (std::uint32_t y = 0; y < HEIGHT_PIXELS; ++y)
        {
          for (std::uint32_t x = 0; x < WIDTH_PIXELS; ++x)
          {
            const Float4 actual = drawn[static_cast<std::size_t>(y) * WIDTH_PIXELS + x];
            const Float4 twin = expected[static_cast<std::size_t>(y) * WIDTH_PIXELS + x];
            if (!Near(actual, twin) && failures++ < 10)
            {
              Logger::WriteMessage(std::format(L"({}, {}): ({}, {}, {}, {}), the twin's ({}, {}, {}, {})\n", x, y, actual.x, actual.y,
                                               actual.z, actual.w, twin.x, twin.y, twin.z, twin.w)
                                     .c_str());
            }
          }
        }
        Logger::WriteMessage(
          std::format(L"{} quads, {} glyphs in the atlas, {} glyph pixels with ink\n", quads.size(), canvas.Atlas().GlyphCount(), inked)
            .c_str());
        Assert::AreEqual(0u, failures, L"pixels that differ from the twin's composite");
      });
  }

  // ADR-034, R15: segments of every slope, width and alpha, a dot, one that leaves the target, and some over a fill and
  // over one another, drawn on WARP and composited by the twin: every pixel of the target, its coverage at its centre.
  // Every pixel must agree, which also shows that the sloped quad holds every pixel the segment covers.
  TEST_METHOD(DrawsSegmentsAsTheTwinDoes)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        constexpr std::uint32_t WIDTH_PIXELS = 128;
        constexpr std::uint32_t HEIGHT_PIXELS = 96;
        constexpr Float4 BACKGROUND{0.05f, 0.1f, 0.15f, 1.0f};
        NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, L"Test render target views");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, L"Test shader views");
        NeuronClient::Canvas canvas(_device, shaderHeap, COLOR_FORMAT, 1);

        canvas.FillRectangle(40, 30, 50, 30, {0.3f, 0.3f, 0.3f}, 0.6f);
        canvas.DrawSegment({10.0f, 10.0f}, {118.0f, 10.0f}, 1.0f, {1.0f, 1.0f, 1.0f}, 1.0f);
        canvas.DrawSegment({10.5f, 20.25f}, {10.5f, 86.0f}, 2.5f, {0.2f, 1.0f, 0.4f}, 0.8f);
        canvas.DrawSegment({20.0f, 80.0f}, {110.0f, 25.0f}, 3.0f, {1.0f, 0.6f, 0.1f}, 0.7f);
        canvas.DrawSegment({30.3f, 40.7f}, {97.9f, 71.1f}, 1.5f, {0.4f, 0.7f, 1.0f}, 1.0f);
        canvas.DrawSegment({64.0f, 48.0f}, {64.0f, 48.0f}, 6.0f, {1.0f, 0.2f, 0.2f}, 0.9f);
        canvas.DrawSegment({100.0f, 90.0f}, {150.0f, 120.0f}, 4.0f, {0.8f, 0.8f, 0.2f}, 1.0f);
        const std::vector<NeuronClient::CanvasQuad> quads(canvas.Quads().begin(), canvas.Quads().end());

        const D3D12_CLEAR_VALUE clear{COLOR_FORMAT, {{BACKGROUND.x, BACKGROUND.y, BACKGROUND.z, BACKGROUND.w}}};
        const winrt::com_ptr<ID3D12Resource> color =
          NeuronClient::CreateTexture2D(_device, COLOR_FORMAT, WIDTH_PIXELS, HEIGHT_PIXELS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, L"Test color");
        const std::uint32_t colorView = rtvHeap.Allocate();
        _device.Device()->CreateRenderTargetView(color.get(), nullptr, rtvHeap.Cpu(colorView));
        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            const D3D12_CPU_DESCRIPTOR_HANDLE target = rtvHeap.Cpu(colorView);
            _list->ClearRenderTargetView(target, clear.Color, 0, nullptr);
            _list->OMSetRenderTargets(1, &target, FALSE, nullptr);
            canvas.Record(_list, 0, WIDTH_PIXELS, HEIGHT_PIXELS);
          });
        const std::vector<std::byte> bytes =
          NeuronClient::ReadTexture2D(_device, color.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, COLOR_BYTES_PER_PIXEL);
        std::vector<Float4> drawn(static_cast<std::size_t>(WIDTH_PIXELS) * HEIGHT_PIXELS);
        std::memcpy(static_cast<void*>(drawn.data()), bytes.data(), bytes.size());

        // The twin's composite: a fill over its rectangle, and a segment over every pixel, by its coverage there.
        std::vector<Float4> expected(drawn.size(), BACKGROUND);
        std::uint32_t covered = 0;
        std::uint32_t partly = 0;
        for (const NeuronClient::CanvasQuad& quad : quads)
        {
          for (std::uint32_t y = 0; y < HEIGHT_PIXELS; ++y)
          {
            for (std::uint32_t x = 0; x < WIDTH_PIXELS; ++x)
            {
              float coverage = 0.0f;
              if (quad.kind == static_cast<std::uint32_t>(NeuronClient::CanvasQuadKind::Segment))
              {
                coverage = NeuronClient::CanvasSegmentCoverage({static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f},
                                                               {quad.startX, quad.startY}, {quad.endX, quad.endY}, quad.halfWidthPixels);
                covered += coverage > 0.0f ? 1u : 0u;
                partly += coverage > 0.0f && coverage < 1.0f ? 1u : 0u;
              }
              else
              {
                const bool inside = static_cast<std::int32_t>(x) >= quad.pixelX &&
                                    static_cast<std::int32_t>(x) < quad.pixelX + static_cast<std::int32_t>(quad.widthPixels) &&
                                    static_cast<std::int32_t>(y) >= quad.pixelY &&
                                    static_cast<std::int32_t>(y) < quad.pixelY + static_cast<std::int32_t>(quad.heightPixels);
                coverage = inside ? 1.0f : 0.0f;
              }
              if (coverage > 0.0f)
              {
                Float4& pixel = expected[static_cast<std::size_t>(y) * WIDTH_PIXELS + x];
                pixel = NeuronClient::CanvasBlend(NeuronClient::CanvasPremultiply(quad.color, quad.alpha, coverage), pixel);
              }
            }
          }
        }
        Assert::IsTrue(partly > 100, std::format(L"{} of {} covered pixels are partly covered", partly, covered).c_str());

        std::uint32_t failures = 0;
        for (std::uint32_t y = 0; y < HEIGHT_PIXELS; ++y)
        {
          for (std::uint32_t x = 0; x < WIDTH_PIXELS; ++x)
          {
            const Float4 actual = drawn[static_cast<std::size_t>(y) * WIDTH_PIXELS + x];
            const Float4 twin = expected[static_cast<std::size_t>(y) * WIDTH_PIXELS + x];
            if (!Near(actual, twin) && failures++ < 10)
            {
              Logger::WriteMessage(std::format(L"({}, {}): ({}, {}, {}, {}), the twin's ({}, {}, {}, {})\n", x, y, actual.x, actual.y,
                                               actual.z, actual.w, twin.x, twin.y, twin.z, twin.w)
                                     .c_str());
            }
          }
        }
        Logger::WriteMessage(std::format(L"{} segment pixels covered, {} partly\n", covered, partly).c_str());
        Assert::AreEqual(0u, failures, L"pixels that differ from the twin's composite");
      });
  }
};

} // namespace NeuronClientTests
