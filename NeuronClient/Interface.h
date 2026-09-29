#pragma once

#include "InputState.h"
#include "Surface.h"

#include "Float3.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NeuronClient
{

// The interface's look (Design/ADR/ADR-034): its text, the padding inside a widget, and its colors, linear.
struct InterfaceStyle
{
  TextStyle text;
  float paddingPixels;
  NeuronCore::Float3 panelColor;
  float panelAlpha;
  NeuronCore::Float3 buttonColor;
  NeuronCore::Float3 hoverColor;    // a widget the pointer is over
  NeuronCore::Float3 pressedColor;  // a button pressed and held
  NeuronCore::Float3 textColor;     // text, and a bar's fill unless it names its own
  NeuronCore::Float3 dimmedColor;   // a disabled button's text, and every hotkey's
  NeuronCore::Float3 selectedColor; // a list's chosen rows
  NeuronCore::Float3 barBackColor;  // what a bar is not full of
};

// A widget's hotkey: a key, as InputState numbers keys, and its name as the widget shows it.
struct Hotkey
{
  std::uint32_t key;
  std::wstring_view name;
};

// The interface's widgets (Design/ADR/ADR-034): a panel, a label, a button, a bar and a list, drawn on a surface. They are
// immediate: each frame, Begin, then each widget in the order it draws, then End. A widget draws itself at once and says
// what the pointer and the keys did to it.
//
// The input goes to the interface first, and the world takes what it leaves.
// - A press is the interface's when it lands on a panel or a widget drawn that frame, and stays the interface's until
//   every button is released. Any other press is the world's until then.
// - With no button held, the pointer is the interface's while it is over a panel or a widget, wheel and all.
// - A button's hotkey is the interface's, whether or not the button is enabled.
class Interface
{
public:
  explicit Interface(const InterfaceStyle& _style) noexcept;

  // Starts a frame on _surface, with _input as the frame reads it.
  void Begin(Surface& _surface, const InputState& _input);

  // Ends the frame: a disabled button's reason, while the pointer hovers it, is drawn over everything, beside the pointer.
  void End();

  // A translucent panel: room of the interface's.
  void Panel(const PixelRect& _rect);

  // A line of _text, its top-left corner at (_xPixels, _yPixels), in _color or the style's. Returns its size.
  TextExtent Label(float _xPixels, float _yPixels, std::wstring_view _text, std::optional<NeuronCore::Float3> _color = std::nullopt);

  // A button that says _text, and its hotkey's name after it. It is clicked when a press and its release both land on it,
  // or when its hotkey is pressed with neither Alt nor Ctrl held. A button with a _disabledReason is dimmed and never
  // clicked, and shows the reason while the pointer hovers it. Returns whether it was clicked this frame.
  [[nodiscard]] bool Button(const PixelRect& _rect, std::wstring_view _text, std::optional<Hotkey> _hotkey,
                            std::wstring_view _disabledReason = {});

  // A bar _fraction full from its left, in _color.
  void Bar(const PixelRect& _rect, float _fraction, NeuronCore::Float3 _color);

  // _rows in _rect, a line each, from the top; the rows that do not fit are left out. The _chosen row is highlighted.
  // Returns the row a press and its release both landed on this frame.
  [[nodiscard]] std::optional<std::size_t> List(const PixelRect& _rect, std::span<const std::wstring> _rows,
                                                std::optional<std::size_t> _chosen = std::nullopt);

  // The height of a button or a list's row: a line of the style's text and the padding above and below it.
  [[nodiscard]] float RowHeightPixels() const noexcept;

  [[nodiscard]] const InterfaceStyle& Style() const noexcept
  {
    return m_style;
  }

  // Whether the pointer is the interface's, as the frame just ended leaves it: its press, or, with no button held, its
  // room under the pointer. The world takes the pointer only when it is not.
  [[nodiscard]] bool OwnsPointer() const noexcept;

  // Whether the interface took _key this frame, as a button's hotkey.
  [[nodiscard]] bool TookKey(std::uint32_t _key) const noexcept;

private:
  enum class Owner : std::uint8_t
  {
    Nobody,
    Interface,
    World
  };

  [[nodiscard]] NeuronCore::Float2 Pointer() const noexcept;

  // Whether a press and its release both landed in _rect this frame, with the left button.
  [[nodiscard]] bool Clicked(const PixelRect& _rect) const noexcept;

  void Fill(const PixelRect& _rect, NeuronCore::Float3 _color, float _alpha);

  InterfaceStyle m_style;
  Surface* m_surface = nullptr;
  const InputState* m_input = nullptr;
  std::vector<PixelRect> m_room;             // this frame's panels and widgets
  std::vector<std::uint32_t> m_keys;         // this frame's hotkeys
  std::optional<NeuronCore::Float2> m_press; // where the last press landed, while a button is held or on the frame it is released
  Owner m_owner = Owner::Nobody;             // whose the press is
  bool m_pressEnded = false;                 // no button was held at the end of the last frame
  std::wstring m_reason;                     // the hovered disabled button's
};

} // namespace NeuronClient
