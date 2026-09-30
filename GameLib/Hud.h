#pragma once

#include "Commander.h"

#include "Interface.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace GameLib
{

// A row of the selection panel: the entity, and what the panel says of it.
struct SelectionRow
{
  std::uint32_t id;
  std::wstring text;
};

// What the top bar shows.
struct HudFigures
{
  std::wstring side;   // the side the session plays, by number and name, or that it observes
  std::size_t ships;   // the side's whole ships
  double worldSeconds; // the world's time, which stops while it is paused
  bool paused;
};

// What the player asked for through the HUD this frame.
struct HudRequest
{
  OrderAction action;
  std::optional<std::uint32_t> chosen; // a row of the selection panel, to select alone
};

// The HUD's first panels (Design/MvpPlan.md phase 4, Design/ADR/ADR-034), through _interface, on a target _widthPixels by
// _heightPixels:
// - a top bar across the top, with _figures;
// - a selection panel in the bottom-left corner, which lists _selection, the first rows of it that fit;
// - with _ordering, an order bar along the bottom. Its Move, Stop, Hold and Attack buttons order the side's selected
//   ships, and are disabled while _canOrder is false; the Move and Attack buttons say so while _armed has them armed
//   (Design/ADR/ADR-035). Its Mine button is disabled, with its reason, until mining is built (phase 6).
[[nodiscard]] HudRequest DrawHud(NeuronClient::Interface& _interface, float _widthPixels, float _heightPixels, const HudFigures& _figures,
                                 std::span<const SelectionRow> _selection, bool _ordering, bool _canOrder, ArmedOrder _armed);

} // namespace GameLib
