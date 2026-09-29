#pragma once

#include "Float3.h"
#include "VoxModel.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace NeuronCore
{

// How a model breaks when it detonates (Design/ADR/ADR-024): into fragments, each a face-connected piece of one part that
// flies as one rigid body. Near the blast almost every voxel is a fragment of its own; farther out, the pieces grow into
// chunks. The fragments are a property of the model, taken once when it loads, so that a detonation stays a pure function
// of the event and the time (D3): a detonation's seed moves the fragments, never their shapes.
struct FragmentationParameters
{
  Float3 blastOrigin;        // in the model's space: where the model shatters finest
  float shatterDistance;     // a voxel starts a fragment with chance 1 / (1 + (d / shatterDistance)^3), d from the origin
  std::uint32_t growthSteps; // a fragment grows at most this many face steps from the voxel that started it
};

// The defaults of ADR-024 for _model: about its voxels' centroid, where its detonation's blast comes from
// (Design/Archive/SpaceScene.md §5.5), with a shatter distance in proportion to its size, so that a frigate and a station
// both break into a shattered core and chunks.
[[nodiscard]] FragmentationParameters DefaultFragmentationParameters(const VoxModel& _model) noexcept;

// One fragment, as the oriented splat reads it from its structured buffer. R16: this struct is the truth,
// Shader/Fragment.hlsli mirrors it, and the layout echo in NeuronClientTests proves the two agree.
struct Fragment
{
  Float3 pivot;    // the mean of its voxels' centers, in its part's space: what it turns about
  float sizeScale; // its voxel count to the power -1/3: 1 for a lone voxel, smaller for a larger fragment
};

static_assert(sizeof(Fragment) == 16);
static_assert(offsetof(Fragment, pivot) == 0);
static_assert(offsetof(Fragment, sizeScale) == 12);

// A model's fragments. Fragment indices are the model's own, part after part, so that a model's debris does not depend on
// where it sits in the scene (ADR-014).
struct ModelFragments
{
  std::vector<std::uint32_t> fragmentOf; // for each record of the model, the index of its fragment
  std::vector<Fragment> fragments;
  std::vector<float> partRadius; // for each part, no voxel center of it is farther than this from its fragment's pivot
};

// _model broken as _parameters have it. The same model and parameters give the same fragments, on every client.
[[nodiscard]] ModelFragments FragmentModel(const VoxModel& _model, const FragmentationParameters& _parameters);

// What a detonated placement reads of its part's fragments: the twin of the fragment buffers the oriented splat reads
// (Shader/Splat.hlsli). The spans view a SceneFragments, which outlives the placement.
struct PartFragments
{
  std::span<const std::uint32_t> fragmentOf; // for each voxel of the placement, the model's index of its fragment
  std::span<const Fragment> fragments;       // the model's fragments, by the model's index
  std::uint32_t firstFragment;               // where the model's first fragment lies in the scene's fragment buffer
  float radius;                              // the part's: no voxel center is farther than this from its fragment's pivot
};

// Every model's fragments, model after model, as the scene's records are (SceneRecords): the fragment of each record of
// the scene, and every model's fragments in one buffer. Each model breaks under its defaults.
class SceneFragments
{
public:
  explicit SceneFragments(std::span<const VoxModel> _models);

  // For each record of the scene's record buffer, the index of its fragment within its model.
  [[nodiscard]] std::span<const std::uint32_t> FragmentOf() const noexcept
  {
    return m_fragmentOf;
  }

  // Every model's fragments, model after model.
  [[nodiscard]] std::span<const Fragment> Fragments() const noexcept
  {
    return m_fragments;
  }

  // What a placement of part _part of model _model reads.
  [[nodiscard]] PartFragments Part(std::uint32_t _model, std::uint32_t _part) const noexcept;

private:
  struct ModelSpan
  {
    std::uint32_t firstRecord;
    std::uint32_t firstFragment;
    std::uint32_t fragmentCount;
    std::uint32_t firstPart;
  };
  struct PartSpan
  {
    std::uint32_t firstRecord; // within its model
    std::uint32_t recordCount;
    float radius;
  };

  std::vector<std::uint32_t> m_fragmentOf;
  std::vector<Fragment> m_fragments;
  std::vector<ModelSpan> m_models;
  std::vector<PartSpan> m_parts;
};

} // namespace NeuronCore
