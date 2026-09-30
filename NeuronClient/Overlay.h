#pragma once

#include "Surface.h"

#include "Float3.h"
#include "PerspectiveView.h"

#include <cstdint>

namespace NeuronClient
{

// The world's overlay (Design/ADR/ADR-034): marks anchored in the world, drawn on a surface where a view shows them, with
// segments and fills. What lies behind the near plane is left out, and a line that crosses it is cut there.

// How many segments a ring is drawn with.
inline constexpr std::uint32_t RING_SEGMENTS = 48;

// A ring: the level circle of _radius about _center, _widthPixels wide.
void DrawRing(Surface& _surface, const NeuronCore::PerspectiveView& _view, NeuronCore::Float3 _center, float _radius, float _widthPixels,
              NeuronCore::Float3 _color, float _alpha);

// A line from _from to _to, _widthPixels wide.
void DrawLine(Surface& _surface, const NeuronCore::PerspectiveView& _view, NeuronCore::Float3 _from, NeuronCore::Float3 _to,
              float _widthPixels, NeuronCore::Float3 _color, float _alpha);

// A marker: a diamond _sizePixels across about where _view shows _at, as large whatever its distance, its sides
// _widthPixels wide.
void DrawMarker(Surface& _surface, const NeuronCore::PerspectiveView& _view, NeuronCore::Float3 _at, float _sizePixels, float _widthPixels,
                NeuronCore::Float3 _color, float _alpha);

// A bar, _widthPixels by _heightPixels, centred _raisePixels above where _view shows _at: _fraction of it from the left in
// _color, and the rest in _backColor.
void DrawBar(Surface& _surface, const NeuronCore::PerspectiveView& _view, NeuronCore::Float3 _at, float _raisePixels, float _widthPixels,
             float _heightPixels, float _fraction, NeuronCore::Float3 _color, NeuronCore::Float3 _backColor, float _alpha);

} // namespace NeuronClient
