#include "pch.h"

#include "DeferredSurface.h"

#include <type_traits>

namespace NeuronClient
{

DeferredSurface::DeferredSurface(Surface& _target) noexcept
  : m_target(_target)
{
}

void DeferredSurface::FillRectangle(std::int32_t _xPixels, std::int32_t _yPixels, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                                    NeuronCore::Float3 _color, float _alpha)
{
  m_draws.emplace_back(Fill{_xPixels, _yPixels, _widthPixels, _heightPixels, _color, _alpha});
}

TextExtent DeferredSurface::Print(std::wstring_view _text, float _xPixels, float _yPixels, const TextStyle& _style,
                                  NeuronCore::Float3 _color, float _alpha)
{
  m_draws.emplace_back(Text{std::wstring(_text), _xPixels, _yPixels, _style, _color, _alpha});
  return m_target.Measure(_text, _style);
}

TextExtent DeferredSurface::Measure(std::wstring_view _text, const TextStyle& _style)
{
  return m_target.Measure(_text, _style);
}

void DeferredSurface::DrawSegment(NeuronCore::Float2 _start, NeuronCore::Float2 _end, float _widthPixels, NeuronCore::Float3 _color,
                                  float _alpha)
{
  m_draws.emplace_back(Segment{_start, _end, _widthPixels, _color, _alpha});
}

void DeferredSurface::Replay()
{
  for (const std::variant<Fill, Text, Segment>& draw : m_draws)
  {
    std::visit(
      [this](const auto& _draw)
      {
        using Draw = std::decay_t<decltype(_draw)>;
        if constexpr (std::is_same_v<Draw, Fill>)
        {
          m_target.FillRectangle(_draw.xPixels, _draw.yPixels, _draw.widthPixels, _draw.heightPixels, _draw.color, _draw.alpha);
        }
        else if constexpr (std::is_same_v<Draw, Text>)
        {
          static_cast<void>(m_target.Print(_draw.text, _draw.xPixels, _draw.yPixels, _draw.style, _draw.color, _draw.alpha));
        }
        else
        {
          m_target.DrawSegment(_draw.start, _draw.end, _draw.widthPixels, _draw.color, _draw.alpha);
        }
      },
      draw);
  }
  m_draws.clear();
}

} // namespace NeuronClient
