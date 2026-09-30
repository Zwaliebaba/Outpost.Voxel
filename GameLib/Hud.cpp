#include "pch.h"

#include "Hud.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <vector>

namespace GameLib
{
namespace
{

// The order bar's buttons and their hotkeys, the characters' own codes (Design/MvpPlan.md phase 4, the owner at phase 3's
// checkpoint: S stops and H holds).
constexpr std::uint32_t MOVE_KEY = 'M';
constexpr std::uint32_t STOP_KEY = 'S';
constexpr std::uint32_t HOLD_KEY = 'H';
constexpr std::uint32_t ATTACK_KEY = 'A';
constexpr std::uint32_t MINE_KEY = 'G';
constexpr std::size_t ORDER_BUTTONS = 5;

constexpr std::wstring_view SELECT_FIRST = L"Select ships of yours first";
constexpr std::wstring_view ATTACK_LATER = L"Attack comes with combat, in phase 5";
constexpr std::wstring_view MINE_LATER = L"Mining comes with the economy, in phase 6";

// Sizes in ems of the interface's text.
constexpr float BUTTON_EMS = 7.5f;
constexpr float PANEL_EMS = 18.0f;
constexpr float GAP_EMS = 0.5f;

// The most rows the selection panel shows.
constexpr std::size_t PANEL_ROWS = 8;

} // namespace

HudRequest DrawHud(NeuronClient::Interface& _interface, float _widthPixels, float _heightPixels, const HudFigures& _figures,
                   std::span<const SelectionRow> _selection, bool _ordering, bool _canOrder, bool _moveArmed)
{
  HudRequest request{OrderAction::None, std::nullopt};
  const float row = _interface.RowHeightPixels();
  const float em = _interface.Style().text.sizePixels;
  const float padding = _interface.Style().paddingPixels;
  const float gap = std::round(GAP_EMS * em);

  // The top bar.
  _interface.Panel({0.0f, 0.0f, _widthPixels, row});
  const auto seconds = static_cast<long long>(std::max(_figures.worldSeconds, 0.0));
  std::wstring bar = std::format(L"{}     {} ships     {:02}:{:02}", _figures.side, _figures.ships, seconds / 60, seconds % 60);
  if (_figures.paused)
  {
    bar += L"     paused";
  }
  static_cast<void>(_interface.Label(padding, padding, bar));

  // The selection panel: a title, and a row for each selected entity that fits, the last of which says how many more
  // there are when some do not.
  if (!_selection.empty())
  {
    const std::size_t shown = std::min(_selection.size(), PANEL_ROWS);
    const float width = std::round(PANEL_EMS * em);
    const float height = static_cast<float>(shown + 1) * row;
    const NeuronClient::PixelRect panel{gap, _heightPixels - gap - height, width, height};
    _interface.Panel(panel);
    static_cast<void>(_interface.Label(panel.x + padding, panel.y + padding, std::format(L"{} selected", _selection.size())));
    std::vector<std::wstring> rows;
    rows.reserve(shown);
    for (std::size_t index = 0; index < shown; ++index)
    {
      rows.push_back(_selection[index].text);
    }
    const bool more = _selection.size() > shown;
    if (more)
    {
      rows.back() = std::format(L"and {} more", _selection.size() - shown + 1);
    }
    const std::optional<std::size_t> clicked = _interface.List({panel.x, panel.y + row, width, static_cast<float>(shown) * row}, rows);
    if (clicked.has_value() && !(more && *clicked + 1 == shown))
    {
      request.chosen = _selection[*clicked].id;
    }
  }

  // The order bar, centred along the bottom.
  if (_ordering)
  {
    const float width = std::round(BUTTON_EMS * em);
    const float across = static_cast<float>(ORDER_BUTTONS) * width + static_cast<float>(ORDER_BUTTONS - 1) * gap;
    float x = std::round(0.5f * (_widthPixels - across));
    const float y = _heightPixels - gap - row;
    _interface.Panel({x - gap, y - gap, across + 2.0f * gap, row + 2.0f * gap});
    const std::wstring_view unless = _canOrder ? std::wstring_view{} : SELECT_FIRST;
    const auto button =
      [&_interface, &x, y, width, row, gap](std::wstring_view _text, std::uint32_t _key, std::wstring_view _name, std::wstring_view _reason)
    {
      const bool clicked = _interface.Button({x, y, width, row}, _text, NeuronClient::Hotkey{_key, _name}, _reason);
      x += width + gap;
      return clicked;
    };
    if (button(_moveArmed ? L"Move: click" : L"Move", MOVE_KEY, L"M", unless))
    {
      request.action = OrderAction::Move;
    }
    if (button(L"Stop", STOP_KEY, L"S", unless))
    {
      request.action = OrderAction::Stop;
    }
    if (button(L"Hold", HOLD_KEY, L"H", unless))
    {
      request.action = OrderAction::Hold;
    }
    static_cast<void>(button(L"Attack", ATTACK_KEY, L"A", ATTACK_LATER));
    static_cast<void>(button(L"Mine", MINE_KEY, L"G", MINE_LATER));
  }
  return request;
}

} // namespace GameLib
