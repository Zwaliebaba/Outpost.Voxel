#include "pch.h"

#include "DeferredSurface.h"
#include "InputState.h"
#include "Interface.h"
#include "RecordingSurface.h"

#include "Float3.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronClient::MouseButton;

constexpr NeuronClient::InterfaceStyle STYLE{{L"Consolas", 15.0f, NeuronClient::REGULAR_WEIGHT},
                                             4.0f,
                                             {0.0f, 0.0f, 0.0f},
                                             0.6f,
                                             {0.2f, 0.2f, 0.2f},
                                             {0.3f, 0.3f, 0.3f},
                                             {0.4f, 0.4f, 0.4f},
                                             {1.0f, 1.0f, 1.0f},
                                             {0.5f, 0.5f, 0.5f},
                                             {0.1f, 0.3f, 0.6f},
                                             {0.1f, 0.1f, 0.1f}};

// The test's widgets: a panel, and a Stop button on it with S for its hotkey.
constexpr NeuronClient::PixelRect PANEL{0.0f, 0.0f, 200.0f, 100.0f};
constexpr NeuronClient::PixelRect BUTTON{20.0f, 20.0f, 80.0f, 24.0f};
constexpr std::uint32_t STOP_KEY = 'S';

struct FrameOutcome
{
  bool clicked;
  bool ownsPointer;
  bool tookKey;
};

// One frame of the test's widgets, the button enabled or with _disabledReason, then the end of the input's frame.
FrameOutcome RunFrame(NeuronClient::Interface& _interface, RecordingSurface& _surface, NeuronClient::InputState& _input,
                      std::wstring_view _disabledReason = {})
{
  _surface.Clear();
  _interface.Begin(_surface, _input);
  _interface.Panel(PANEL);
  const bool clicked = _interface.Button(BUTTON, L"Stop", NeuronClient::Hotkey{STOP_KEY, L"S"}, _disabledReason);
  _interface.End();
  const FrameOutcome outcome{clicked, _interface.OwnsPointer(), _interface.TookKey(STOP_KEY)};
  _input.EndFrame();
  return outcome;
}

void Point(NeuronClient::InputState& _input, std::int32_t _xPixels, std::int32_t _yPixels)
{
  _input.OnMouseMove(_xPixels, _yPixels);
}

} // namespace

// Design/ADR/ADR-034: the interface's widgets and the way input reaches them before the world, on the CPU.
TEST_CLASS(InterfaceTests)
{
public:
  // A button is clicked when a press and its release both land on it, even within one frame; not by a press alone, nor
  // by a release off it, nor by a release on it of a press that began in the world.
  TEST_METHOD(ClicksAButtonWhenAPressAndItsReleaseLandOnIt)
  {
    NeuronClient::Interface widgets(STYLE);
    RecordingSurface surface;
    NeuronClient::InputState input;
    Point(input, 50, 30);
    input.OnButton(MouseButton::Left, true);
    Assert::IsFalse(RunFrame(widgets, surface, input).clicked, L"a press alone");
    input.OnButton(MouseButton::Left, false);
    Assert::IsTrue(RunFrame(widgets, surface, input).clicked, L"and its release on the button");

    input.OnButton(MouseButton::Left, true);
    static_cast<void>(RunFrame(widgets, surface, input));
    Point(input, 150, 80);
    input.OnButton(MouseButton::Left, false);
    Assert::IsFalse(RunFrame(widgets, surface, input).clicked, L"a press on it released off it");

    Point(input, 300, 300);
    input.OnButton(MouseButton::Left, true);
    static_cast<void>(RunFrame(widgets, surface, input));
    Point(input, 50, 30);
    input.OnButton(MouseButton::Left, false);
    Assert::IsFalse(RunFrame(widgets, surface, input).clicked, L"a press of the world's released on it");

    static_cast<void>(RunFrame(widgets, surface, input));
    input.OnButton(MouseButton::Left, true);
    input.OnButton(MouseButton::Left, false);
    Assert::IsTrue(RunFrame(widgets, surface, input).clicked, L"a click shorter than a frame");
  }

  // The pointer is the interface's over its room while no button is held; a press is the interface's or the world's by
  // where it lands, until its release, wherever the pointer goes meanwhile.
  TEST_METHOD(RoutesThePointerToTheInterfaceFirst)
  {
    NeuronClient::Interface widgets(STYLE);
    RecordingSurface surface;
    NeuronClient::InputState input;
    Assert::IsFalse(RunFrame(widgets, surface, input).ownsPointer, L"a pointer not yet seen");
    Point(input, 150, 80);
    Assert::IsTrue(RunFrame(widgets, surface, input).ownsPointer, L"over the panel");
    Point(input, 300, 300);
    Assert::IsFalse(RunFrame(widgets, surface, input).ownsPointer, L"over the world");

    Point(input, 150, 80);
    input.OnButton(MouseButton::Right, true);
    Assert::IsTrue(RunFrame(widgets, surface, input).ownsPointer, L"a press on the panel");
    Point(input, 300, 300);
    Assert::IsTrue(RunFrame(widgets, surface, input).ownsPointer, L"held over the world");
    input.OnButton(MouseButton::Right, false);
    Assert::IsTrue(RunFrame(widgets, surface, input).ownsPointer, L"the frame of its release");
    Assert::IsFalse(RunFrame(widgets, surface, input).ownsPointer, L"and then the world's again");

    input.OnButton(MouseButton::Left, true);
    Assert::IsFalse(RunFrame(widgets, surface, input).ownsPointer, L"a press in the world");
    Point(input, 100, 50);
    Assert::IsFalse(RunFrame(widgets, surface, input).ownsPointer, L"dragged over the panel");
    input.OnButton(MouseButton::Left, false);
    Assert::IsFalse(RunFrame(widgets, surface, input).ownsPointer, L"released over it");
    Assert::IsTrue(RunFrame(widgets, surface, input).ownsPointer, L"and then over the panel, the interface's");
  }

  // A button's hotkey clicks it, unless Alt or Ctrl is held; a disabled button takes its hotkey without being clicked, is
  // dimmed, and says why while the pointer hovers it.
  TEST_METHOD(TakesItsHotkeyAndSaysWhyItIsDisabled)
  {
    NeuronClient::Interface widgets(STYLE);
    RecordingSurface surface;
    NeuronClient::InputState input;
    input.OnKey(STOP_KEY, true, false);
    FrameOutcome outcome = RunFrame(widgets, surface, input);
    Assert::IsTrue(outcome.clicked && outcome.tookKey, L"its hotkey");
    input.OnKey(STOP_KEY, false, false);

    input.OnKey(NeuronClient::ALT_KEY, true, false);
    input.OnKey(STOP_KEY, true, false);
    outcome = RunFrame(widgets, surface, input);
    Assert::IsFalse(outcome.clicked || outcome.tookKey, L"not with Alt held");
    input.OnKey(STOP_KEY, false, false);
    input.OnKey(NeuronClient::ALT_KEY, false, false);

    constexpr std::wstring_view REASON = L"Select ships to stop";
    input.OnKey(STOP_KEY, true, false);
    outcome = RunFrame(widgets, surface, input, REASON);
    Assert::IsTrue(!outcome.clicked && outcome.tookKey, L"a disabled button takes its hotkey and is not clicked");
    input.OnKey(STOP_KEY, false, false);
    Point(input, 50, 30);
    input.OnButton(MouseButton::Left, true);
    input.OnButton(MouseButton::Left, false);
    Assert::IsFalse(RunFrame(widgets, surface, input, REASON).clicked, L"nor by the pointer");
    const auto said = [REASON](const RecordingSurface::Text& _text) { return _text.text == REASON; };
    Assert::IsTrue(std::ranges::any_of(surface.texts, said), L"hovered, it says why");
    const auto label = std::ranges::find(surface.texts, std::wstring(L"Stop"), &RecordingSurface::Text::text);
    Assert::IsTrue(label != surface.texts.end() && label->color.x == STYLE.dimmedColor.x, L"dimmed");
    Point(input, 300, 300);
    static_cast<void>(RunFrame(widgets, surface, input, REASON));
    Assert::IsFalse(std::ranges::any_of(surface.texts, said), L"and not once the pointer leaves");
  }

  // A list shows the rows that fit, highlights the chosen ones, and says which row a click landed on.
  TEST_METHOD(ListsRowsAndSaysWhichIsClicked)
  {
    NeuronClient::Interface widgets(STYLE);
    RecordingSurface surface;
    NeuronClient::InputState input;
    const std::vector<std::wstring> rows{L"Miner", L"Gunship", L"Lancer"};
    const float rowHeight = widgets.RowHeightPixels();
    const NeuronClient::PixelRect rect{10.0f, 10.0f, 120.0f, 2.5f * rowHeight};
    const auto frame = [&]()
    {
      surface.Clear();
      widgets.Begin(surface, input);
      const std::optional<std::size_t> clicked = widgets.List(rect, rows, 1);
      widgets.End();
      input.EndFrame();
      return clicked;
    };

    Assert::IsFalse(frame().has_value(), L"no click");
    Assert::AreEqual(std::size_t{2}, surface.texts.size(), L"the two rows that fit");
    Assert::IsTrue(surface.texts[0].text == L"Miner" && surface.texts[1].text == L"Gunship", L"from the top");
    const auto highlighted =
      std::ranges::find_if(surface.fills, [](const RecordingSurface::Fill& _fill)
                           { return _fill.color.z == STYLE.selectedColor.z && _fill.color.x == STYLE.selectedColor.x; });
    Assert::IsTrue(highlighted != surface.fills.end() && highlighted->yPixels == static_cast<std::int32_t>(std::lround(10.0f + rowHeight)),
                   L"the chosen row highlighted");

    Point(input, 20, static_cast<std::int32_t>(10.0f + 0.5f * rowHeight));
    input.OnButton(MouseButton::Left, true);
    input.OnButton(MouseButton::Left, false);
    const std::optional<std::size_t> clicked = frame();
    Assert::IsTrue(clicked == std::size_t{0}, L"the first row clicked");
  }

  // A bar fills its fraction from the left, over what it is not full of.
  TEST_METHOD(FillsABarToItsFraction)
  {
    NeuronClient::Interface widgets(STYLE);
    RecordingSurface surface;
    NeuronClient::InputState input;
    widgets.Begin(surface, input);
    widgets.Bar({10.0f, 20.0f, 100.0f, 8.0f}, 0.25f, {0.0f, 1.0f, 0.0f});
    widgets.End();
    Assert::AreEqual(std::size_t{2}, surface.fills.size(), L"what it is not full of, then its fill");
    Assert::AreEqual(100u, surface.fills[0].widthPixels);
    Assert::AreEqual(25u, surface.fills[1].widthPixels, L"a quarter");
    Assert::AreEqual(1.0f, surface.fills[1].color.y);
  }

  // A deferred surface measures text on its target at once, draws nothing there until it replays, and then draws what
  // it kept, once, in the order it came: so that the interface is laid out first and drawn over the world's overlay.
  TEST_METHOD(DefersItsDrawsUntilItReplays)
  {
    RecordingSurface target;
    NeuronClient::DeferredSurface deferred(target);
    deferred.FillRectangle(1, 2, 3, 4, {1.0f, 0.0f, 0.0f}, 0.5f);
    const NeuronClient::TextExtent extent = deferred.Print(L"Hold", 5.0f, 6.0f, STYLE.text, {0.0f, 1.0f, 0.0f}, 1.0f);
    deferred.DrawSegment({0.0f, 0.0f}, {10.0f, 0.0f}, 2.0f, {0.0f, 0.0f, 1.0f}, 1.0f);
    Assert::AreEqual(4.0f * RecordingSurface::CHARACTER_PIXELS, extent.widthPixels, L"measured on its target");
    Assert::IsTrue(target.draws.empty(), L"nothing drawn yet");

    deferred.Replay();
    const std::vector<RecordingSurface::Draw> expected{RecordingSurface::Draw::Fill, RecordingSurface::Draw::Text,
                                                       RecordingSurface::Draw::Segment};
    Assert::IsTrue(target.draws == expected, L"in order");
    Assert::IsTrue(target.fills[0].xPixels == 1 && target.fills[0].heightPixels == 4 && target.fills[0].alpha == 0.5f,
                   L"the fill as given");
    Assert::IsTrue(target.texts[0].text == L"Hold" && target.texts[0].xPixels == 5.0f && target.texts[0].yPixels == 6.0f,
                   L"the text as given");
    Assert::IsTrue(target.segments[0].end.x == 10.0f && target.segments[0].widthPixels == 2.0f, L"the segment as given");
    deferred.Replay();
    Assert::AreEqual(std::size_t{3}, target.draws.size(), L"once");
  }
};

} // namespace NeuronClientTests
