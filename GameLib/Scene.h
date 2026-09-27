#pragma once

#include "Float3.h"
#include "VoxModel.h"

#include <filesystem>

namespace GameLib
{

// What the client shows: the model, and the sphere around its voxels that the camera frames (Design/SampleRenderer.md
// §3).
struct Scene
{
  NeuronCore::VoxModel model;
  NeuronCore::Float3 center;
  float radius;
};

// Loads the model at _path and measures it. Throws std::runtime_error naming the file and the reader's refusal.
[[nodiscard]] Scene LoadScene(const std::filesystem::path& _path);

} // namespace GameLib
