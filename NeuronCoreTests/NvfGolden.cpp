#include "pch.h"

#include "NvfGolden.h"

#include "VoxFile.h"
#include "VoxelRecord.h"

#include <cstddef>

namespace NeuronCoreTests
{

NeuronCore::NvfModel GoldenNvfModel()
{
  NeuronCore::NvfModel model{};

  // The EGA colors, as the three assets hold them. Entry 9 is half transparent; 10, 15 and 16 glow, the last two with
  // other materials than the assets'; entry 12 is diffuse but keeps its _emit and _flux.
  for (std::size_t i = 0; i < model.palette.size(); ++i)
  {
    model.palette[i] = {EGA_PALETTE[i][0], EGA_PALETTE[i][1], EGA_PALETTE[i][2], EGA_PALETTE[i][3], false, 0.0f, 0.0f};
  }
  model.palette[8].alpha = 128;
  model.palette[9] = {85, 85, 255, 255, true, 0.6f, 2.0f};
  model.palette[11] = {85, 255, 255, 255, false, 0.125f, 1.0f};
  model.palette[14] = {255, 255, 85, 255, true, 0.25f, 3.5f};
  model.palette[15] = {255, 255, 255, 255, true, 1.0f, 0.0f};

  // hull, and under it hull/turret, and under that hull/turret/barrel. Every voxel uses a palette entry of its own, and
  // each part has a voxel at its far corner.
  const auto record = [](std::uint8_t _x, std::uint8_t _y, std::uint8_t _z, std::uint8_t _color)
  { return NeuronCore::PackVoxelRecord({_x, _y, _z, _color}); };
  model.records = {
    record(0, 0, 0, 0),  record(4, 2, 6, 1),  record(1, 0, 0, 2),  record(2, 1, 3, 3),
    record(3, 0, 6, 4),  record(0, 2, 6, 5),  record(4, 0, 0, 6),  record(2, 2, 2, 7),  // hull
    record(0, 0, 0, 8),  record(2, 1, 2, 9),  record(1, 0, 1, 10), record(1, 1, 1, 11), // hull/turret
    record(0, 0, 0, 12), record(0, 0, 1, 13), record(0, 0, 2, 14), record(0, 0, 3, 15), // hull/turret/barrel
  };
  model.parts = {
    {.path = "hull",
     .parent = NeuronCore::NVF_NO_PARENT,
     .size = {5, 3, 7},
     .translation = {-2, 0, -3},
     .pivot = {2.5f, 1.5f, 3.5f},
     .pivotAuthored = false,
     .firstVoxel = 0,
     .voxelCount = 8},
    {.path = "hull/turret",
     .parent = 0,
     .size = {3, 2, 3},
     .translation = {1, 3, 2},
     .pivot = {1.5f, 0.0f, 1.5f},
     .pivotAuthored = true,
     .firstVoxel = 8,
     .voxelCount = 4},
    {.path = "hull/turret/barrel",
     .parent = 1,
     .size = {1, 1, 4},
     .translation = {1, 1, 3},
     .pivot = {0.5f, 0.5f, 2.0f},
     .pivotAuthored = false,
     .firstVoxel = 12,
     .voxelCount = 4},
  };

  // Out of name order on purpose. The main weapon is turned 30 degrees about +Y, the dock 90 degrees about +X and the
  // engine half a turn about +Y, so that it points aft; the sensor is not turned.
  model.hardpoints = {
    {.name = "weapon.main", .part = 2, .position = {0.5f, 0.5f, 4.0f}, .rotation = {0.0f, 0.25881904f, 0.0f, 0.9659258f}, .fromVox = false},
    {.name = "engine.main", .part = 0, .position = {2.5f, 1.5f, 0.5f}, .rotation = {0.0f, 1.0f, 0.0f, 0.0f}, .fromVox = true},
    {.name = "sensor.top.left", .part = 1, .position = {0.0f, 2.0f, 1.5f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .fromVox = false},
    {.name = "dock.aft", .part = 0, .position = {2.5f, 3.0f, 3.5f}, .rotation = {0.70710677f, 0.0f, 0.0f, 0.70710677f}, .fromVox = true},
  };
  return model;
}

} // namespace NeuronCoreTests
