#include "pch.h"

#include "GlyphAtlas.h"

#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <functional>

namespace NeuronClient
{

GlyphAtlas::GlyphAtlas(const GraphicsDevice& _device, IDWriteFactory2* _factory, DescriptorHeap& _shaderHeap, std::uint32_t _slots)
  : m_shaderHeap(_shaderHeap),
    m_srv(_shaderHeap.Allocate()),
    m_texels(static_cast<std::size_t>(SIZE_PIXELS) * SIZE_PIXELS, 0)
{
  m_factory.copy_from(_factory);
  m_texture = CreateTexture2D(_device, FORMAT, SIZE_PIXELS, SIZE_PIXELS, D3D12_RESOURCE_FLAG_NONE, READABLE, nullptr, L"Glyph atlas");
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = FORMAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  _device.Device()->CreateShaderResourceView(m_texture.get(), &srv, m_shaderHeap.Cpu(m_srv));
  // A row of R8 texels is SIZE_PIXELS bytes, a multiple of the 256 a copy's row pitch needs, so the upload holds the atlas
  // rows as they are.
  static_assert(SIZE_PIXELS % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0);
  m_uploads.reserve(_slots);
  for (std::uint32_t slot = 0; slot < _slots; ++slot)
  {
    m_uploads.push_back(CreateBuffer(_device, D3D12_HEAP_TYPE_UPLOAD, m_texels.size(), L"Glyph atlas upload"));
  }
}

std::optional<GlyphAtlas::Glyph> GlyphAtlas::Find(IDWriteFontFace* _face, float _emSizePixels, std::uint16_t _index, bool _sideways)
{
  const Key key{_face, std::bit_cast<std::uint32_t>(_emSizePixels), _index, _sideways};
  if (const auto found = m_glyphs.find(key); found != m_glyphs.end())
  {
    return found->second;
  }
  std::optional<Glyph> glyph = Rasterize(key);
  if (glyph)
  {
    if (std::ranges::none_of(m_faces, [_face](const winrt::com_ptr<IDWriteFontFace>& _held) { return _held.get() == _face; }))
    {
      winrt::com_ptr<IDWriteFontFace> held;
      held.copy_from(_face);
      m_faces.push_back(held);
    }
    m_glyphs.emplace(key, *glyph);
  }
  return glyph;
}

void GlyphAtlas::Upload(ID3D12GraphicsCommandList* _list, std::uint32_t _slot)
{
  if (m_dirtyTop >= m_dirtyBottom)
  {
    return;
  }
  const std::uint32_t rows = m_dirtyBottom - m_dirtyTop;
  const std::size_t bytes = static_cast<std::size_t>(rows) * SIZE_PIXELS;
  ID3D12Resource* upload = m_uploads[_slot].get();
  void* mapped = nullptr;
  const D3D12_RANGE nothingRead{0, 0};
  winrt::check_hresult(upload->Map(0, &nothingRead, &mapped));
  std::memcpy(mapped, m_texels.data() + static_cast<std::size_t>(m_dirtyTop) * SIZE_PIXELS, bytes);
  const D3D12_RANGE written{0, bytes};
  upload->Unmap(0, &written);

  const D3D12_RESOURCE_BARRIER toCopy = Transition(m_texture.get(), READABLE, D3D12_RESOURCE_STATE_COPY_DEST);
  _list->ResourceBarrier(1, &toCopy);
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = m_texture.get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  destination.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = upload;
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = {0, {FORMAT, SIZE_PIXELS, rows, 1, SIZE_PIXELS}};
  _list->CopyTextureRegion(&destination, 0, m_dirtyTop, 0, &source, nullptr);
  const D3D12_RESOURCE_BARRIER toRead = Transition(m_texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, READABLE);
  _list->ResourceBarrier(1, &toRead);
  m_dirtyTop = SIZE_PIXELS;
  m_dirtyBottom = 0;
}

void GlyphAtlas::StartOverIfFull()
{
  if (!m_full)
  {
    return;
  }
  m_glyphs.clear();
  std::ranges::fill(m_texels, std::uint8_t{0});
  m_shelfX = 0;
  m_shelfY = 0;
  m_shelfHeight = 0;
  m_dirtyTop = 0;
  m_dirtyBottom = SIZE_PIXELS;
  m_full = false;
}

D3D12_GPU_DESCRIPTOR_HANDLE GlyphAtlas::Table() const noexcept
{
  return m_shaderHeap.Gpu(m_srv);
}

std::size_t GlyphAtlas::KeyHash::operator()(const Key& _key) const noexcept
{
  std::size_t hash = std::hash<const void*>{}(_key.face);
  for (const std::size_t part : {std::size_t{_key.emSizeBits}, std::size_t{_key.index}, std::size_t{_key.sideways ? 1u : 0u}})
  {
    hash ^= part + 0x9E3779B97F4A7C15u + (hash << 6u) + (hash >> 2u);
  }
  return hash;
}

std::optional<GlyphAtlas::Glyph> GlyphAtlas::Rasterize(const Key& _key)
{
  // One glyph at the origin. The analysis wants its advance and offset even for a single glyph: none of either.
  const std::uint16_t index = _key.index;
  const FLOAT advance = 0.0f;
  const DWRITE_GLYPH_OFFSET offset{0.0f, 0.0f};
  DWRITE_GLYPH_RUN run{};
  run.fontFace = _key.face;
  run.fontEmSize = std::bit_cast<float>(_key.emSizeBits);
  run.glyphCount = 1;
  run.glyphIndices = &index;
  run.glyphAdvances = &advance;
  run.glyphOffsets = &offset;
  run.isSideways = _key.sideways ? TRUE : FALSE;
  // Grayscale for now: ClearType's three coverages per pixel need an alpha each, which dual-source blending gives (ADR-010).
  winrt::com_ptr<IDWriteGlyphRunAnalysis> analysis;
  winrt::check_hresult(m_factory->CreateGlyphRunAnalysis(&run, nullptr, RENDERING_MODE, MEASURING_MODE, DWRITE_GRID_FIT_MODE_ENABLED,
                                                         DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0.0f, 0.0f, analysis.put()));
  // Grayscale coverage comes as one byte a pixel through the 1x1 texture type, which the documentation still calls
  // bi-level; asked for ClearType's 3x1 texture, a grayscale analysis has nothing to give.
  RECT bounds{};
  winrt::check_hresult(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds));
  if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
  {
    return Glyph{0, 0, 0, 0, 0, 0};
  }
  const auto width = static_cast<std::uint32_t>(bounds.right - bounds.left);
  const auto height = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  if (!Place(width, height, x, y))
  {
    m_full = true;
    return std::nullopt;
  }
  std::vector<BYTE> coverage(static_cast<std::size_t>(width) * height);
  winrt::check_hresult(
    analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds, coverage.data(), static_cast<UINT32>(coverage.size())));
  for (std::uint32_t row = 0; row < height; ++row)
  {
    std::memcpy(m_texels.data() + static_cast<std::size_t>(y + row) * SIZE_PIXELS + x,
                coverage.data() + static_cast<std::size_t>(row) * width, width);
  }
  m_dirtyTop = std::min(m_dirtyTop, y);
  m_dirtyBottom = std::max(m_dirtyBottom, y + height);
  return Glyph{x, y, width, height, static_cast<std::int32_t>(bounds.left), static_cast<std::int32_t>(bounds.top)};
}

bool GlyphAtlas::Place(std::uint32_t _widthPixels, std::uint32_t _heightPixels, std::uint32_t& _x, std::uint32_t& _y) noexcept
{
  const std::uint32_t paddedWidth = _widthPixels + PADDING_PIXELS;
  const std::uint32_t paddedHeight = _heightPixels + PADDING_PIXELS;
  if (paddedWidth > SIZE_PIXELS || paddedHeight > SIZE_PIXELS)
  {
    return false;
  }
  if (m_shelfX + paddedWidth > SIZE_PIXELS)
  {
    m_shelfY += m_shelfHeight;
    m_shelfX = 0;
    m_shelfHeight = 0;
  }
  if (m_shelfY + paddedHeight > SIZE_PIXELS)
  {
    return false;
  }
  _x = m_shelfX;
  _y = m_shelfY;
  m_shelfX += paddedWidth;
  m_shelfHeight = std::max(m_shelfHeight, paddedHeight);
  return true;
}

} // namespace NeuronClient
