#pragma once

#include "Float3.h"

#include <cstdint>
#include <string_view>

namespace NeuronClient
{

// How a surface sets text: a font family installed on the system, such as Consolas, its em size in pixels of the target,
// and its weight, from 100 to 900 as DirectWrite numbers weights: 400 is regular and 700 bold. A family that is not
// installed falls back as DirectWrite's system fallback decides.
struct TextStyle
{
  const wchar_t* fontFamily;
  float sizePixels;
  std::uint16_t weight;
};

inline constexpr std::uint16_t REGULAR_WEIGHT = 400;
inline constexpr std::uint16_t BOLD_WEIGHT = 700;

// The size of a text as laid out, in pixels.
struct TextExtent
{
  float widthPixels;
  float heightPixels;
};

// A rectangle of the target, in pixels, y down: its top-left corner and its size.
struct PixelRect
{
  float x;
  float y;
  float width;
  float height;

  // Whether _point lies within it: its left and top edges are, its right and bottom edges are not.
  [[nodiscard]] constexpr bool Contains(NeuronCore::Float2 _point) const noexcept
  {
    return _point.x >= x && _point.x < x + width && _point.y >= y && _point.y < y + height;
  }
};

// What the interface and the overlay draw on (Design/ADR/ADR-034): rectangles, text and segments, in pixels of the target,
// y down, each in linear color with straight alpha, over what was drawn before. The canvas draws them over the frame; a
// test's surface keeps them, so that what draws on a surface can be tested on the CPU.
class Surface
{
public:
  Surface() = default;
  virtual ~Surface() = default;
  Surface(const Surface&) = delete;
  Surface& operator=(const Surface&) = delete;
  Surface(Surface&&) = delete;
  Surface& operator=(Surface&&) = delete;

  // A rectangle from its top-left corner.
  virtual void FillRectangle(std::int32_t _xPixels, std::int32_t _yPixels, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                             NeuronCore::Float3 _color, float _alpha) = 0;

  // Lays _text out in _style with its top-left corner at (_xPixels, _yPixels) and draws it. Lines break at '\n' and nowhere
  // else. Returns the size of the text as laid out.
  virtual TextExtent Print(std::wstring_view _text, float _xPixels, float _yPixels, const TextStyle& _style, NeuronCore::Float3 _color,
                           float _alpha) = 0;

  // The size Print gives _text in _style, without drawing it.
  [[nodiscard]] virtual TextExtent Measure(std::wstring_view _text, const TextStyle& _style) = 0;

  // A segment from _start to _end, _widthPixels wide with round ends, its edge antialiased over a pixel.
  virtual void DrawSegment(NeuronCore::Float2 _start, NeuronCore::Float2 _end, float _widthPixels, NeuronCore::Float3 _color,
                           float _alpha) = 0;
};

} // namespace NeuronClient
