#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>
#include <dwrite_2.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace NeuronClient
{

class DescriptorHeap;
class GraphicsDevice;

// The canvas's glyphs (Design/Archive/SampleRenderer.md §13, Design/ADR/ADR-010). DirectWrite rasterizes each glyph once per font
// face and size on the CPU, hinted and with grayscale antialiasing, and the atlas packs its coverage into a square R8
// texture, shelf by shelf. The CPU keeps a copy of every texel; the rows that changed since the last upload reach the
// GPU in the command list of the frame that first draws them. A glyph that finds no room is left out of its frame, and
// the atlas starts over after it.
class GlyphAtlas
{
public:
  static constexpr std::uint32_t SIZE_PIXELS = 1024;
  static constexpr DXGI_FORMAT FORMAT = DXGI_FORMAT_R8_UNORM;
  static constexpr D3D12_RESOURCE_STATES READABLE = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  // Empty texels after each glyph, right and below, so that no glyph touches another.
  static constexpr std::uint32_t PADDING_PIXELS = 1;

  // GDI-compatible natural metrics put every glyph on a whole pixel, so that its bitmap lands on the target exactly as it
  // was rasterized, and the rendering mode is the one made for those metrics. The canvas lays text out with the same
  // metrics.
  static constexpr DWRITE_MEASURING_MODE MEASURING_MODE = DWRITE_MEASURING_MODE_GDI_NATURAL;
  static constexpr DWRITE_RENDERING_MODE RENDERING_MODE = DWRITE_RENDERING_MODE_GDI_NATURAL;

  // A glyph's bitmap in the atlas, and its top-left corner from the glyph's origin on the baseline, y down. A glyph with no
  // ink, such as a space, is zero by zero.
  struct Glyph
  {
    std::uint32_t atlasX;
    std::uint32_t atlasY;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::int32_t offsetX;
    std::int32_t offsetY;
  };

  // _slots upload buffers, one for each frame in flight.
  GlyphAtlas(const GraphicsDevice& _device, IDWriteFactory2* _factory, DescriptorHeap& _shaderHeap, std::uint32_t _slots);

  // The glyph at _emSizePixels, rasterized and packed now if it is new. Empty when the atlas has no room for it.
  [[nodiscard]] std::optional<Glyph> Find(IDWriteFontFace* _face, float _emSizePixels, std::uint16_t _index, bool _sideways);

  // Records the copy of the rows that changed since the last upload, through _slot's upload buffer, whose previous frame
  // the GPU must have finished. The texture is readable by pixel shaders before and after.
  void Upload(ID3D12GraphicsCommandList* _list, std::uint32_t _slot);

  // After a frame in which a glyph found no room: forgets every glyph, so that the next frame packs what it draws anew.
  void StartOverIfFull();

  // A descriptor table of one view: the atlas as Texture2D<float>.
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE Table() const noexcept;

  [[nodiscard]] ID3D12Resource* Texture() const noexcept
  {
    return m_texture.get();
  }

  // The CPU's copy, row by row: what the texture holds once the next Upload has run.
  [[nodiscard]] std::span<const std::uint8_t> Texels() const noexcept
  {
    return m_texels;
  }

  [[nodiscard]] std::size_t GlyphCount() const noexcept
  {
    return m_glyphs.size();
  }

private:
  struct Key
  {
    IDWriteFontFace* face;
    std::uint32_t emSizeBits;
    std::uint16_t index;
    bool sideways;

    [[nodiscard]] bool operator==(const Key& _other) const noexcept = default;
  };

  struct KeyHash
  {
    [[nodiscard]] std::size_t operator()(const Key& _key) const noexcept;
  };

  [[nodiscard]] std::optional<Glyph> Rasterize(const Key& _key);

  // A free rectangle on the current shelf or a new one below it; false when the atlas has no room.
  [[nodiscard]] bool Place(std::uint32_t _widthPixels, std::uint32_t _heightPixels, std::uint32_t& _x, std::uint32_t& _y) noexcept;

  winrt::com_ptr<IDWriteFactory2> m_factory;
  DescriptorHeap& m_shaderHeap;
  std::uint32_t m_srv;
  winrt::com_ptr<ID3D12Resource> m_texture;
  std::vector<winrt::com_ptr<ID3D12Resource>> m_uploads;
  std::vector<std::uint8_t> m_texels;
  std::unordered_map<Key, Glyph, KeyHash> m_glyphs;
  std::vector<winrt::com_ptr<IDWriteFontFace>> m_faces; // keeps alive every face a key names
  std::uint32_t m_shelfX = 0;
  std::uint32_t m_shelfY = 0;
  std::uint32_t m_shelfHeight = 0;
  // Rows [m_dirtyTop, m_dirtyBottom) changed since the last upload. At first that is all of them, which the first upload
  // clears.
  std::uint32_t m_dirtyTop = 0;
  std::uint32_t m_dirtyBottom = SIZE_PIXELS;
  bool m_full = false;
};

} // namespace NeuronClient
