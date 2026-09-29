#pragma once

#include "Surface.h"

#include "Float3.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace NeuronClientTests
{

// A surface that keeps what is drawn on it, and lays text out as if each character were CHARACTER_PIXELS wide and a line
// LINE_PIXELS tall: what the interface's and the overlay's tests draw on, on the CPU (Design/ADR/ADR-034).
class RecordingSurface final : public NeuronClient::Surface
{
public:
  static constexpr float CHARACTER_PIXELS = 8.0f;
  static constexpr float LINE_PIXELS = 16.0f;

  struct Fill
  {
    std::int32_t xPixels;
    std::int32_t yPixels;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    NeuronCore::Float3 color;
    float alpha;
  };

  struct Text
  {
    std::wstring text;
    float xPixels;
    float yPixels;
    NeuronCore::Float3 color;
  };

  struct Segment
  {
    NeuronCore::Float2 start;
    NeuronCore::Float2 end;
    float widthPixels;
    NeuronCore::Float3 color;
    float alpha;
  };

  // What was drawn, in the order it was drawn.
  enum class Draw : std::uint8_t
  {
    Fill,
    Text,
    Segment
  };

  RecordingSurface() = default;
  ~RecordingSurface() override = default;
  RecordingSurface(const RecordingSurface&) = delete;
  RecordingSurface& operator=(const RecordingSurface&) = delete;
  RecordingSurface(RecordingSurface&&) = delete;
  RecordingSurface& operator=(RecordingSurface&&) = delete;

  void FillRectangle(std::int32_t _xPixels, std::int32_t _yPixels, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                     NeuronCore::Float3 _color, float _alpha) override
  {
    fills.push_back({_xPixels, _yPixels, _widthPixels, _heightPixels, _color, _alpha});
    draws.push_back(Draw::Fill);
  }

  NeuronClient::TextExtent Print(std::wstring_view _text, float _xPixels, float _yPixels, const NeuronClient::TextStyle& _style,
                                 NeuronCore::Float3 _color, float /*_alpha*/) override
  {
    texts.push_back({std::wstring(_text), _xPixels, _yPixels, _color});
    draws.push_back(Draw::Text);
    return Measure(_text, _style);
  }

  [[nodiscard]] NeuronClient::TextExtent Measure(std::wstring_view _text, const NeuronClient::TextStyle& /*_style*/) override
  {
    return {CHARACTER_PIXELS * static_cast<float>(_text.size()), LINE_PIXELS};
  }

  void DrawSegment(NeuronCore::Float2 _start, NeuronCore::Float2 _end, float _widthPixels, NeuronCore::Float3 _color, float _alpha) override
  {
    segments.push_back({_start, _end, _widthPixels, _color, _alpha});
    draws.push_back(Draw::Segment);
  }

  // Forgets what was drawn.
  void Clear()
  {
    fills.clear();
    texts.clear();
    segments.clear();
    draws.clear();
  }

  std::vector<Fill> fills;
  std::vector<Text> texts;
  std::vector<Segment> segments;
  std::vector<Draw> draws;
};

} // namespace NeuronClientTests
