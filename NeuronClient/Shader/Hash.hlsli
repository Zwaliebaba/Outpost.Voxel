#pragma once

// The PCG hash of Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 9(3), 2020: the voxel index view's colors
// and the detonation's randomness (Design/Archive/SampleRenderer.md §11, Design/Archive/SpaceScene.md §5.5). The C++ twin is
// NeuronCore/Hash.h (R15).
uint PcgHash(uint _value)
{
  uint state = _value * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}
