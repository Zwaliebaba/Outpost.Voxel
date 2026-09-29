"""The golden model, built field by field: the Python twin of Tests/NeuronCoreTests/NvfGolden.cpp.

Tools/Golden/Golden.nvf is what both implementations write for it, byte for byte (Design/NeuronVoxelFormat.md §9). A
float is spelled as the C++ spells it, and passed through as_single where single precision does not hold the decimal.
"""

import sys
from pathlib import Path

EXTENSION = Path(__file__).resolve().parents[1]  # the extension's folder, which holds NvfFormat.py
if str(EXTENSION) not in sys.path:
  sys.path.insert(0, str(EXTENSION))

import NvfFormat  # noqa: E402  pylint: disable=wrong-import-position
from NvfFormat import NvfHardpoint, NvfModel, NvfPart, PaletteEntry, as_single, pack_voxel_record  # noqa: E402

GOLDEN_NVF_PATH = 'Tools/Golden/Golden.nvf'

# The EGA colors, as Tests/NeuronCoreTests/VoxFile.h spells them and the three assets hold them.
EGA_PALETTE = (
  (0, 0, 0, 255),
  (0, 0, 170, 255),
  (0, 170, 0, 255),
  (0, 170, 170, 255),
  (170, 0, 0, 255),
  (170, 0, 170, 255),
  (170, 85, 0, 255),
  (170, 170, 170, 255),
  (85, 85, 85, 255),
  (85, 85, 255, 255),
  (85, 255, 85, 255),
  (85, 255, 255, 255),
  (255, 85, 85, 255),
  (255, 85, 255, 255),
  (255, 255, 85, 255),
  (255, 255, 255, 255),
)


def golden_nvf_model():
  model = NvfModel()

  # Entry 9 is half transparent; 10, 15 and 16 glow, the last two with other materials than the assets'; entry 12 is
  # diffuse but keeps its _emit and _flux.
  model.palette = [PaletteEntry(*rgba) for rgba in EGA_PALETTE]
  model.palette[8].alpha = 128
  model.palette[9] = PaletteEntry(85, 85, 255, 255, True, as_single(0.6), 2.0)
  model.palette[11] = PaletteEntry(85, 255, 255, 255, False, 0.125, 1.0)
  model.palette[14] = PaletteEntry(255, 255, 85, 255, True, 0.25, 3.5)
  model.palette[15] = PaletteEntry(255, 255, 255, 255, True, 1.0, 0.0)

  # hull, and under it hull/turret, and under that hull/turret/barrel. Every voxel uses a palette entry of its own, and
  # each part has a voxel at its far corner.
  model.records = [
    pack_voxel_record(0, 0, 0, 0), pack_voxel_record(4, 2, 6, 1), pack_voxel_record(1, 0, 0, 2),
    pack_voxel_record(2, 1, 3, 3), pack_voxel_record(3, 0, 6, 4), pack_voxel_record(0, 2, 6, 5),
    pack_voxel_record(4, 0, 0, 6), pack_voxel_record(2, 2, 2, 7),  # hull
    pack_voxel_record(0, 0, 0, 8), pack_voxel_record(2, 1, 2, 9), pack_voxel_record(1, 0, 1, 10),
    pack_voxel_record(1, 1, 1, 11),  # hull/turret
    pack_voxel_record(0, 0, 0, 12), pack_voxel_record(0, 0, 1, 13), pack_voxel_record(0, 0, 2, 14),
    pack_voxel_record(0, 0, 3, 15),  # hull/turret/barrel
  ]
  model.parts = [
    NvfPart('hull', NvfFormat.NO_PARENT, (5, 3, 7), (-2, 0, -3), (2.5, 1.5, 3.5), False, 0, 8),
    NvfPart('hull/turret', 0, (3, 2, 3), (1, 3, 2), (1.5, 0.0, 1.5), True, 8, 4),
    NvfPart('hull/turret/barrel', 1, (1, 1, 4), (1, 1, 3), (0.5, 0.5, 2.0), False, 12, 4),
  ]

  # Out of name order on purpose. The main weapon is turned 30 degrees about +Y, the dock 90 degrees about +X and the
  # engine half a turn about +Y, so that it points aft; the sensor is not turned.
  half_root = as_single(0.70710677)
  model.hardpoints = [
    NvfHardpoint('weapon.main', 2, (0.5, 0.5, 4.0), (0.0, as_single(0.25881904), 0.0, as_single(0.9659258)), False),
    NvfHardpoint('engine.main', 0, (2.5, 1.5, 0.5), (0.0, 1.0, 0.0, 0.0), True),
    NvfHardpoint('sensor.top.left', 1, (0.0, 2.0, 1.5), (0.0, 0.0, 0.0, 1.0), False),
    NvfHardpoint('dock.aft', 0, (2.5, 3.0, 3.5), (half_root, 0.0, 0.0, half_root), True),
  ]
  return model
