#pragma once

#include "Float3.h"
#include "VoxModel.h"

#include <filesystem>

namespace GameLib
{

// What the client shows: the model, the box around its voxels, which the sun's view is fitted to, and the sphere around
// that box, which the camera frames (Design/SampleRenderer.md §3, §10).
struct Scene
{
  NeuronCore::VoxModel model;
  NeuronCore::Float3 lower;
  NeuronCore::Float3 upper;
  NeuronCore::Float3 center;
  float radius;
};

// Loads the model at _path and measures it. Throws std::runtime_error naming the file and the reader's refusal.
[[nodiscard]] Scene LoadScene(const std::filesystem::path& _path);

} // namespace GameLib
