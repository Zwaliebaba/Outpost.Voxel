#pragma once

#include "SnapshotBuffer.h"
#include "Surface.h"

#include "CombatProfile.h"
#include "Orders.h"

#include "Float3.h"
#include "Message.h"
#include "PerspectiveView.h"

#include <cstdint>
#include <span>
#include <vector>

namespace GameLib
{

// How the fight's marks in the world's overlay look (Design/ADR/ADR-035), linear, and their sizes, in pixels.
struct CombatLook
{
  float markPixels;      // an ownership mark's width
  float gapPixels;       // between an entity's sphere and its mark or its bars, and between the two bars
  float barWidthPixels;  // each of the two bars
  float barHeightPixels; // each of the two bars
  float shellPixels;     // a shell's streak's width
  float beamPixels;
  NeuronCore::Float3 hullColor;     // the hull's bar
  NeuronCore::Float3 barBackColor;  // what a bar has lost
  NeuronCore::Float3 healthyColor;  // the vital bar above WARNING_SHARE
  NeuronCore::Float3 warningColor;  // at or below it
  NeuronCore::Float3 criticalColor; // at or below CRITICAL_SHARE
};

// The shares of the vital bar at which it turns to the warning and the critical color.
inline constexpr float WARNING_SHARE = 0.5f;
inline constexpr float CRITICAL_SHARE = 0.25f;

// The welcome's sides' colors, linear, side n at n - 1.
[[nodiscard]] std::vector<NeuronCore::Float3> LinearSideColors(std::span<const NeuronCore::SideColor> _sides);

// Ownership at a distance (the concept's §9, the owner at phase 4's checkpoint): a mark in its side's color under every
// entity of a side in _sample that has not detonated, and a dimmer one under each of _remembered, as wide at every zoom,
// below where _view shows its sphere, whose radius _radii gives by composite. _sideColors are LinearSideColors'.
void DrawOwnership(NeuronClient::Surface& _surface, const NeuronCore::PerspectiveView& _view, const NeuronClient::WorldSample& _sample,
                   std::span<const NeuronClient::SampledEntity> _remembered, std::span<const float> _radii,
                   std::span<const NeuronCore::Float3> _sideColors, const CombatLook& _look);

// The condition readout's bars (G59): over each entity of a side in _sample that is selected, in _selection, or damaged,
// and has not detonated, one for its weakest vital module against the share at which it fails and one below it for its
// hull, from its mask and its composite's components, which _components gives by composite (GameCore::ComponentsOf).
void DrawConditions(NeuronClient::Surface& _surface, const NeuronCore::PerspectiveView& _view, const NeuronClient::WorldSample& _sample,
                    std::span<const std::uint32_t> _selection, std::span<const std::vector<GameCore::CombatComponent>> _components,
                    std::span<const float> _radii, const CombatLook& _look);

// The shots _payload shows (G54), in their sides' colors, lightened: each shell as a streak behind it, where it is
// _payloadSeconds after the payload's snapshot, since a shell flies straight at a constant velocity; and each beam as it
// fired.
void DrawShots(NeuronClient::Surface& _surface, const NeuronCore::PerspectiveView& _view, const GameCore::SnapshotPayload& _payload,
               float _payloadSeconds, std::span<const NeuronCore::Float3> _sideColors, const CombatLook& _look);

} // namespace GameLib
