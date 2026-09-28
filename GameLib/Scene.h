#pragma once

#include "Explosion.h"
#include "Float3.h"
#include "Placement.h"
#include "Sphere.h"
#include "VoxModel.h"

#include <filesystem>
#include <vector>

namespace GameLib
{

// What the client shows: the model, the box around its voxels, which the detonation's envelope and so the sun's view are
// fitted around, and the sphere around that box, which the camera frames while the model is intact
// (Design/Archive/SampleRenderer.md §3, §10, Design/SpaceScene.md §5.5). The model is drawn through placements, one per
// part, each at its part's origin, so that the image and the voxel ids are what they were before placements
// (Design/SpaceScene.md §7).
struct Scene
{
  NeuronCore::VoxModel model;
  NeuronCore::Float3 lower;
  NeuronCore::Float3 upper;
  NeuronCore::Float3 center;
  float radius;
  std::vector<NeuronCore::Placement> placements; // whole, their ids assigned
  NeuronCore::ExplosionParameters explosion;     // the detonation's, in the world: ADR-013's defaults from the centroid
};

// Loads the model at _path, measures it and places it. Throws std::runtime_error naming the file and the reader's
// refusal.
[[nodiscard]] Scene LoadScene(const std::filesystem::path& _path);

// The scene's placements _timeSeconds after its detonation: whole at 0, and after it detonated, each part's voxels
// launched from the same blast origin, taken into the part's space (Design/SpaceScene.md §7.7).
[[nodiscard]] std::vector<NeuronCore::Placement> PlacementsAt(const Scene& _scene, float _timeSeconds);

// Where the detonation reaches, in the world: a sphere every part's envelope stays inside at every time, and the time
// from which every part has drifted to a stop (§5.5).
struct ExplosionReach
{
  NeuronCore::Sphere sphere;
  float stopSeconds;
};

[[nodiscard]] ExplosionReach BoundSceneExplosion(const Scene& _scene) noexcept;

} // namespace GameLib
