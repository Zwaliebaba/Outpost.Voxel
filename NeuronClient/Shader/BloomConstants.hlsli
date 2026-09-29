#pragma once

// The HLSL mirror of NeuronClient/BloomConstants.h (R16, Design/Archive/SpaceScene.md §12.2): one dispatch of bloom's chain, as
// root constants. The C++ struct is the truth, and the layout echo in NeuronClientTests proves that the two agree.
struct BloomConstants
{
  uint widthPixels; // of the level written
  uint heightPixels;
  uint karis; // 1 for Karis's average, 0 for none
  float belowShare;
};
