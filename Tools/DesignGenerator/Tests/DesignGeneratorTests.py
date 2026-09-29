"""DesignGenerator.py and VoxFile.py against Design/ADR/ADR-025 and NeuronCore's reader.

The Linux CI job runs them with the system Python, from the repository root:

  python -m unittest discover -s Tools/DesignGenerator/Tests -p "*Tests.py"

GameCoreTests checks the imported designs against the concept's validation (Design/GameConcept.md §5.5); these check
the generator: that it writes what GameData holds, the same bytes every run, and refuses a design that breaks the rules
before NvfImport ever sees it.
"""

import re
import struct
import sys
import unittest
from pathlib import Path

GENERATOR = Path(__file__).resolve().parents[1]
REPOSITORY = GENERATOR.parents[1]
if str(GENERATOR) not in sys.path:
  sys.path.insert(0, str(GENERATOR))

import DesignGenerator  # noqa: E402  pylint: disable=wrong-import-position
from DesignGenerator import FACINGS, MOUNT_BOXES, Hull, Mount, Voxels, build_all, hull_scene  # noqa: E402
from VoxFile import IDENTITY, rotation_byte  # noqa: E402

# A hardpoint's name as NVF §4.1 spells it, after the part's path and the @ of its marker (§5).
MARKER_NAME = re.compile(r'^hull@[a-z0-9_]{1,31}(\.[a-z0-9_]{1,31})+$')

# The files the generator writes, and what each one is.
HULLS = ('Gunship', 'Lancer', 'Miner', 'Cruiser', 'StationCore')
MODULES = ('CommandModule', 'Reactor', 'ReactorLarge', 'Thruster', 'MassDriver', 'Laser', 'MiningLaser', 'CargoHold', 'Sensor',
           'SensorArray', 'Shipyard', 'Lab', 'Refinery')
ASTEROIDS = ('AsteroidA', 'AsteroidB', 'AsteroidC')


def parse_rotation(bits: int):
  """NeuronCore's ParseRotation (VoxModel.cpp), in Python: MagicaVoxel's _r bits, conjugated into the engine's axes."""
  columns = [bits & 3, (bits >> 2) & 3]
  columns.append(3 - columns[0] - columns[1])
  matrix = [[0, 0, 0] for _ in range(3)]
  for row in range(3):
    matrix[row][columns[row]] = -1 if (bits >> (4 + row)) & 1 else 1
  swap = (0, 2, 1)
  return tuple(tuple(matrix[swap[i]][swap[j]] for j in range(3)) for i in range(3))


def chunks(data: bytes):
  """The children of a .vox file's MAIN chunk, as (id, content) pairs."""
  assert data[:4] == b'VOX ', 'the magic'
  offset = 8 + 12  # the header, and MAIN's own header
  while offset < len(data):
    chunk_id = data[offset:offset + 4]
    content_bytes, children_bytes = struct.unpack_from('<ii', data, offset + 4)
    yield chunk_id, data[offset + 12:offset + 12 + content_bytes]
    offset += 12 + content_bytes + children_bytes


def a_hull(mounts, voxels=None) -> Hull:
  """A 7 x 5 x 9 block with the mounts given, for planting defects in."""
  if voxels is None:
    voxels = Voxels()
    voxels.box((0, 0, 0), (7, 5, 9), 1)
  return Hull('Planted', voxels, mounts)


COMMAND = Mount('command', 's', 'core', (3, 2, 4), 'forward', inside=True)


class DesignGeneratorTests(unittest.TestCase):

  def test_writes_what_game_data_holds(self):
    """GameData's generated files are this generator's output, so that no one edits a generated file by hand."""
    for name, data in build_all().items():
      with self.subTest(name=name):
        path = REPOSITORY / 'GameData' / f'{name}.vox'
        self.assertTrue(path.exists(), f'{path} is committed')
        self.assertEqual(data, path.read_bytes(), f'{name}.vox is what the generator writes')

  def test_writes_every_file_and_only_those(self):
    self.assertEqual(sorted(HULLS + MODULES + ASTEROIDS), sorted(build_all()))

  def test_writes_the_same_bytes_every_run(self):
    self.assertEqual(build_all(), build_all())

  def test_check_mode_finds_game_data_up_to_date(self):
    self.assertEqual(0, DesignGenerator.main(['--check', '--output', str(REPOSITORY / 'GameData')]))

  def test_rotation_bytes_are_what_the_reader_decodes(self):
    for facing, matrix in FACINGS.items():
      with self.subTest(facing=facing):
        self.assertEqual(matrix, parse_rotation(rotation_byte(matrix)))

  def test_refuses_a_reflection(self):
    with self.assertRaises(ValueError):
      rotation_byte(((-1, 0, 0), (0, 1, 0), (0, 0, 1)))

  def test_every_mount_box_is_odd_on_every_axis(self):
    for size, box in MOUNT_BOXES.items():
      with self.subTest(size=size):
        self.assertTrue(all(extent % 2 == 1 for extent in box))

  def test_every_facing_is_a_rotation(self):
    for facing, matrix in FACINGS.items():
      with self.subTest(facing=facing):
        determinant = (matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
                       matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
                       matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]))
        self.assertEqual(1, determinant)

  def test_names_every_marker_as_nvf_spells_a_hardpoint(self):
    for build in DesignGenerator.HULLS:
      scene = hull_scene(build())
      with self.subTest(hull=scene.models[0].name):
        self.assertEqual('hull', scene.models[0].name, 'the hull is the part named hull')
        for marker in scene.models[1:]:
          self.assertRegex(marker.name, MARKER_NAME)
          self.assertTrue(all(extent % 2 == 1 for extent in marker.size), f'{marker.name} is odd on every axis')

  def test_gives_every_hull_one_command_mount(self):
    for build in DesignGenerator.HULLS:
      hull = build()
      with self.subTest(hull=hull.name):
        self.assertEqual(1, sum(mount.kind == 'command' for mount in hull.mounts))

  def test_turns_only_markers(self):
    """The reader refuses a turned part (N2): the hull stands unturned, and only markers turn."""
    for name in HULLS:
      with self.subTest(hull=name):
        data = build_all()[name]
        transforms = [content for chunk_id, content in chunks(data) if chunk_id == b'nTRN']
        hull_transform = next(content for content in transforms if b'\x04\x00\x00\x00hull' in content and b'@' not in content)
        self.assertNotIn(b'_r', hull_transform)

  def test_refuses_two_command_mounts(self):
    with self.assertRaisesRegex(ValueError, 'command mounts'):
      hull_scene(a_hull([COMMAND, Mount('command', 's', 'spare', (3, 2, 4), 'aft', inside=True)]))

  def test_refuses_a_box_holding_a_hull_voxel(self):
    with self.assertRaisesRegex(ValueError, 'holds hull voxel'):
      hull_scene(a_hull([COMMAND, Mount('weapon', 's', 'buried', (3, 2, 1), 'forward')]))

  def test_refuses_boxes_that_overlap(self):
    with self.assertRaisesRegex(ValueError, 'overlaps'):
      hull_scene(a_hull([COMMAND, Mount('weapon', 's', 'a', (3, 6, 4), 'forward'), Mount('weapon', 's', 'b', (4, 6, 4), 'forward')]))

  def test_refuses_a_box_that_touches_no_hull(self):
    with self.assertRaisesRegex(ValueError, 'shares no face'):
      hull_scene(a_hull([COMMAND, Mount('weapon', 's', 'floating', (3, 9, 4), 'forward')]))

  def test_refuses_a_line_the_hull_blocks(self):
    """A weapon on the roof facing aft over a tower fires into the tower (G38)."""
    voxels = Voxels()
    voxels.box((0, 0, 0), (7, 5, 9), 1)
    voxels.box((2, 5, 0), (5, 9, 2), 1)
    with self.assertRaisesRegex(ValueError, 'line out of the hull'):
      hull_scene(a_hull([COMMAND, Mount('weapon', 's', 'aft', (3, 6, 6), 'aft')], voxels))

  def test_refuses_a_hull_in_two_pieces(self):
    voxels = Voxels()
    voxels.box((0, 0, 0), (7, 5, 9), 1)
    voxels.box((0, 0, 12), (2, 2, 14), 1)
    with self.assertRaisesRegex(ValueError, 'cut off'):
      hull_scene(a_hull([COMMAND], voxels))

  def test_identity_writes_no_rotation(self):
    self.assertEqual(IDENTITY, FACINGS['forward'])


if __name__ == '__main__':
  unittest.main()
