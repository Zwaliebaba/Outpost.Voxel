#pragma once

#include "SnapshotBuffer.h"

#include <cstdint>
#include <vector>

namespace NeuronClient
{

// What a side's client remembers of what it no longer sees (G21, Design/ADR/ADR-032): each entity of a composite the game
// marks, the structures in the MVP, as the frame last drew it, from the frame it leaves the sample until the sample holds
// it again. The server decides what a side sees; the client only remembers what it was shown. Nothing tells it that a
// remembered entity has gone, so it stays remembered until it is seen again.
class LastSeen
{
public:
  // _marked says, by the welcome's composites, which composites' entities are remembered out of sight.
  explicit LastSeen(std::vector<bool> _marked);

  // Takes what the frame draws: each entity of a marked composite in _sample is seen now, as it is.
  void See(const WorldSample& _sample);

  // Every entity of a marked composite seen before and absent from _sample, as it was last seen, in the order of their
  // ids.
  [[nodiscard]] std::vector<SampledEntity> Remembered(const WorldSample& _sample) const;

private:
  std::vector<bool> m_marked;
  std::vector<SampledEntity> m_seen; // in the order of their ids
};

} // namespace NeuronClient
