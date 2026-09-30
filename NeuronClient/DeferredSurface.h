#pragma once

#include "Surface.h"

#include "Float3.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace NeuronClient
{

// A surface that measures text on its target at once and keeps everything else, to draw on the target later, in order
// (Design/ADR/ADR-034): so that the interface can be laid out, and take the input it owns, before the world's overlay is
// drawn under it.
class DeferredSurface final : public Surface
{
public:
  explicit DeferredSurface(Surface& _target) noexcept;
  ~DeferredSurface() override = default;
  DeferredSurface(const DeferredSurface&) = delete;
  DeferredSurface& operator=(const DeferredSurface&) = delete;
  DeferredSurface(DeferredSurface&&) = delete;
  DeferredSurface& operator=(DeferredSurface&&) = delete;

  void FillRectangle(std::int32_t _xPixels, std::int32_t _yPixels, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                     NeuronCore::Float3 _color, float _alpha) override;
  TextExtent Print(std::wstring_view _text, float _xPixels, float _yPixels, const TextStyle& _style, NeuronCore::Float3 _color,
                   float _alpha) override;
  [[nodiscard]] TextExtent Measure(std::wstring_view _text, const TextStyle& _style) override;
  void DrawSegment(NeuronCore::Float2 _start, NeuronCore::Float2 _end, float _widthPixels, NeuronCore::Float3 _color,
                   float _alpha) override;

  // Draws what was kept on the target, in the order it came, and forgets it.
  void Replay();

private:
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
    TextStyle style; // its font family's name is the caller's, which outlives the frame
    NeuronCore::Float3 color;
    float alpha;
  };

  struct Segment
  {
    NeuronCore::Float2 start;
    NeuronCore::Float2 end;
    float widthPixels;
    NeuronCore::Float3 color;
    float alpha;
  };

  Surface& m_target;
  std::vector<std::variant<Fill, Text, Segment>> m_draws;
};

} // namespace NeuronClient
