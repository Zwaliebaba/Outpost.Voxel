#pragma once

#include "SceneModels.h"
#include "SnapshotBuffer.h"

#include "Float3.h"
#include "OrthographicView.h"
#include "Placement.h"
#include "Sphere.h"
#include "VoxModel.h"

#include <optional>
#include <span>
#include <vector>

namespace GameLib
{

// How much the sun's view grows beyond what it must hold, when something reaches past it.
inline constexpr float SHADOW_VIEW_GROWTH = 1.25f;

// The tone map's exposure, a multiplier: the station file's _film _expo of 1, which the client keeps now that the rest
// of the lighting comes from the world (Design/SpaceScene.md §12.1).
inline constexpr float EXPOSURE = 1.0f;

// What the client shows of the world (Design/SpaceScene.md §6.1, §7): the placements its entities draw at a render time,
// and the sun's view they are lit in. Until S-M7's cascades the sun's view is one square fitted to the whole layout
// (§10): around every entity's reach, whole and as debris, as far as the client has seen it. It never shrinks, and it
// moves only when something reaches beyond it, then grown by SHADOW_VIEW_GROWTH, so that shadows hold still. It is never
// narrower than the station sample's, so that the one-station preset draws its shadows at the density M5 measured.
class Scene
{
public:
  // _toSun is the direction the sun's light comes from.
  Scene(std::span<const NeuronCore::VoxModel> _models, NeuronCore::Float3 _toSun);

  [[nodiscard]] const NeuronClient::SceneModels& Models() const noexcept
  {
    return m_models;
  }

  // The placements _sample's entities draw, in the order of their ids and then of their parts, with their ids (§7.3).
  // Throws std::runtime_error when their voxels would reach NO_VOXEL.
  [[nodiscard]] std::vector<NeuronCore::Placement> Place(const NeuronClient::WorldSample& _sample) const;

  // Fits the sun's view around _sample's entities, as far as it must: true when the view moved, as it does the first time.
  bool FitShadowView(const NeuronClient::WorldSample& _sample);

  // The sun's view, once FitShadowView has fitted it.
  [[nodiscard]] const NeuronCore::OrthographicView& ShadowView() const noexcept
  {
    return m_shadowView;
  }

private:
  NeuronClient::SceneModels m_models;
  NeuronCore::Float3 m_toSun;
  std::optional<NeuronCore::Sphere> m_shadowSphere; // what the sun's view holds
  NeuronCore::OrthographicView m_shadowView{};
};

} // namespace GameLib
