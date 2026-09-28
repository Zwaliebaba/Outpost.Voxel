"""Geometry.py: the swap between NVF's axes and Blender's, the cube's turns, snapping and the preview's surface.

§9 asks that a round trip through the extension's swap returns its input exactly; these tests hold it to that, and hold
the table of turns to NvfImport's, read from NeuronCore/NvfImport.cpp. No Blender is needed; the Linux CI job runs them.
"""

import itertools
import math
import random
import re
import struct
import sys
import unittest
from pathlib import Path

EXTENSION = Path(__file__).resolve().parents[1]  # the extension's folder, which holds the modules under test
if str(EXTENSION) not in sys.path:
  sys.path.insert(0, str(EXTENSION))

import Geometry  # noqa: E402  pylint: disable=wrong-import-position
import NvfFormat  # noqa: E402
from RepositoryFile import read_repository_file  # noqa: E402

NVF_FORWARD = (0.0, 0.0, 1.0)
NVF_UP = (0.0, 1.0, 0.0)
BLENDER_FORWARD = (0.0, 1.0, 0.0)
BLENDER_UP = (0.0, 0.0, 1.0)


def bits(values):
  """The doubles' bits, so that a negative zero cannot pass for a zero."""
  return struct.pack(f'<{len(values)}d', *values)


def random_rotation(generator):
  values = [generator.gauss(0.0, 1.0) for _ in range(4)]
  length = math.sqrt(sum(value * value for value in values))
  return tuple(value / length for value in values)


def xyzw(rotation):
  """A rotation in Blender's order, (w, x, y, z), as (x, y, z, w)."""
  w, x, y, z = rotation
  return (x, y, z, w)


def importer_turns():
  """NvfImport.cpp's table of turns: each entry's three columns, the images of the frame's x, y and z, and its
  quaternion, with HALF_SQRT2 read as the single-precision √½ it names."""
  source = read_repository_file('NeuronCore/NvfImport.cpp').decode('utf-8')
  table = re.search(r'CUBE_ROTATIONS\{\{(.*?)\}\};', source, re.DOTALL)
  entries = []
  number = r'(-?(?:HALF_SQRT2|[0-9.]+f?))'
  pattern = (r'\{\{\{\{(-?\d+), (-?\d+), (-?\d+)\}, \{(-?\d+), (-?\d+), (-?\d+)\}, \{(-?\d+), (-?\d+), (-?\d+)\}\}\}, '
             rf'\{{{number}, {number}, {number}, {number}\}}\}}')
  for match in re.finditer(pattern, table.group(1)):
    columns = tuple(tuple(int(match.group(3 * column + row + 1)) for row in range(3)) for column in range(3))

    def value(token):
      sign = -1.0 if token.startswith('-') else 1.0
      token = token.lstrip('-')
      return sign * (Geometry.HALF_SQRT2 if token == 'HALF_SQRT2' else float(token.rstrip('f')))

    entries.append((columns, tuple(value(match.group(index)) for index in range(10, 14))))
  return entries


class GeometryTests(unittest.TestCase):

  def assert_close(self, expected, actual, what, tolerance=1.0e-9):
    self.assertEqual(len(expected), len(actual), what)
    for a, b in zip(expected, actual):
      self.assertAlmostEqual(a, b, delta=tolerance, msg=f'{what}: {expected} against {actual}')

  # §4.1: (x, y, z) in NVF is (x, z, y) in Blender, and the swap is its own inverse.
  def test_swaps_points_both_ways_exactly(self):
    self.assertEqual((1.0, 3.0, 2.0), Geometry.nvf_to_blender_point((1.0, 2.0, 3.0)))
    self.assertEqual((1.0, 3.0, 2.0), Geometry.blender_to_nvf_point((1.0, 2.0, 3.0)))
    generator = random.Random(2026)
    points = [(-0.0, 0.0, -0.0), (1.0e-45, -3.4e38, 2.5)]
    points += [tuple(generator.uniform(-1.0e6, 1.0e6) for _ in range(3)) for _ in range(1000)]
    for point in points:
      self.assertEqual(bits(point), bits(Geometry.blender_to_nvf_point(Geometry.nvf_to_blender_point(point))), point)

  # §9: a round trip through the swap returns the input exactly, signed zeros and all.
  def test_swaps_rotations_both_ways_exactly(self):
    generator = random.Random(2027)
    rotations = list(Geometry.CUBE_TURNS) + [random_rotation(generator) for _ in range(1000)]
    rotations.append((-0.0, 0.0, -0.0, 1.0))
    for rotation in rotations:
      there = Geometry.nvf_to_blender_rotation(rotation)
      self.assertEqual(bits(rotation), bits(Geometry.blender_to_nvf_rotation(there)), rotation)
      back = Geometry.blender_to_nvf_rotation(there)
      self.assertEqual(bits(there), bits(Geometry.nvf_to_blender_rotation(back)), rotation)

  # A rotation swapped into Blender's axes turns the swapped vector where the original turns the original: P R P.
  def test_swapped_rotations_turn_as_the_originals(self):
    generator = random.Random(2028)
    for _ in range(500):
      rotation = random_rotation(generator)
      vector = tuple(generator.uniform(-10.0, 10.0) for _ in range(3))
      turned = Geometry.nvf_to_blender_point(Geometry.rotate(rotation, vector))
      swapped = Geometry.rotate(xyzw(Geometry.nvf_to_blender_rotation(rotation)), Geometry.nvf_to_blender_point(vector))
      self.assert_close(turned, swapped, 'a turned vector', 1.0e-9)

  # §7: a hardpoint's forward shows as its empty's +Y arrow and its up as +Z, however it is turned.
  def test_shows_forward_as_the_arrows_y(self):
    self.assertEqual(BLENDER_FORWARD, Geometry.nvf_to_blender_point(NVF_FORWARD))
    self.assertEqual(BLENDER_UP, Geometry.nvf_to_blender_point(NVF_UP))
    for turn in Geometry.CUBE_TURNS:
      blender = xyzw(Geometry.nvf_to_blender_rotation(turn))
      self.assert_close(Geometry.nvf_to_blender_point(Geometry.rotate(turn, NVF_FORWARD)),
                        Geometry.rotate(blender, BLENDER_FORWARD), f'forward under {turn}', 1.0e-6)
      self.assert_close(Geometry.nvf_to_blender_point(Geometry.rotate(turn, NVF_UP)),
                        Geometry.rotate(blender, BLENDER_UP), f'up under {turn}', 1.0e-6)

  # The panel's quarter turns are the importer's, spelled alike, so that a snapped hardpoint and a marker's agree.
  def test_turns_are_the_importers(self):
    entries = importer_turns()
    self.assertEqual(24, len(entries), 'NvfImport.cpp\'s table, read')
    self.assertEqual(sorted(Geometry.CUBE_TURNS), sorted(quaternion for _, quaternion in entries))
    axes = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    for columns, quaternion in entries:
      for axis, column in zip(axes, columns):
        self.assert_close(tuple(float(value) for value in column), Geometry.rotate(quaternion, axis),
                          f'{quaternion} turning {axis}', 1.0e-6)

  def test_spells_turns_as_nvf_stores_them(self):
    self.assertEqual(24, len(set(Geometry.CUBE_TURNS)), 'the 24 turns are distinct')
    for turn in Geometry.CUBE_TURNS:
      self.assertLessEqual(abs(math.sqrt(sum(value * value for value in turn)) - 1.0), 1.0e-6, turn)
      x, y, z, w = turn
      first = next(value for value in (x, y, z, w) if value != 0.0)
      self.assertTrue(w > 0.0 or (w == 0.0 and first > 0.0), turn)
      self.assertEqual(turn, Geometry.canonical_rotation(turn), turn)
      self.assertEqual(turn, Geometry.canonical_rotation(tuple(-value for value in turn)), turn)
      self.assertTrue(NvfFormat.is_unit_rotation(turn), turn)

  def test_spells_any_rotation_canonically(self):
    self.assertEqual((0.0, 0.0, 0.0, 1.0), Geometry.canonical_rotation((0.0, 0.0, 0.0, 2.0)), 'a long identity')
    self.assertEqual((0.0, 0.0, 0.0, 1.0), Geometry.canonical_rotation((-0.0, 0.0, -0.0, -1.0)), 'w below zero')
    self.assertEqual((0.0, 1.0, 0.0, 0.0), Geometry.canonical_rotation((0.0, -3.0, 0.0, 0.0)),
                     'a half turn, spelled back')
    for rotation in (Geometry.canonical_rotation((-0.0, -0.0, -0.0, -1.0)),
                     Geometry.canonical_rotation((0.0, -1.0, -0.0, 0.0))):
      self.assertFalse(any(math.copysign(1.0, value) < 0.0 and value == 0.0 for value in rotation), rotation)
    generator = random.Random(2029)
    for _ in range(500):
      rotation = Geometry.canonical_rotation(tuple(generator.uniform(-2.0, 2.0) for _ in range(4)))
      self.assertTrue(NvfFormat.is_unit_rotation(rotation), rotation)
      self.assertEqual(rotation, tuple(NvfFormat.as_single(value) for value in rotation), 'single precision')
    for bad in ((0.0, 0.0, 0.0, 0.0), (math.nan, 0.0, 0.0, 1.0), (0.0, math.inf, 0.0, 1.0)):
      with self.assertRaises(ValueError, msg=str(bad)):
        Geometry.canonical_rotation(bad)

  def test_snaps_rotations_to_the_nearest_quarter_turn(self):
    generator = random.Random(2030)
    for turn in Geometry.CUBE_TURNS:
      self.assertEqual(turn, Geometry.snap_rotation(turn), turn)
      self.assertEqual(turn, Geometry.snap_rotation(tuple(-value for value in turn)), 'either sign')
      for _ in range(20):
        nudged = tuple(value + generator.uniform(-0.1, 0.1) for value in turn)
        self.assertEqual(turn, Geometry.snap_rotation(nudged), nudged)
    # 45 degrees about +Y lies between two turns; the snap picks one of them, and the same one every time.
    between = (0.0, math.sin(math.pi / 8.0), 0.0, math.cos(math.pi / 8.0))
    snapped = Geometry.snap_rotation(between)
    self.assertIn(snapped, ((0.0, 0.0, 0.0, 1.0), (0.0, Geometry.HALF_SQRT2, 0.0, Geometry.HALF_SQRT2)))
    self.assertEqual(snapped, Geometry.snap_rotation(between))

  # §7: the nearest voxel center, face center or edge midpoint, which the owner chose over the nearest point on an
  # edge.
  def test_snaps_positions_to_the_grid(self):
    position = (2.2, 3.9, -0.3)
    self.assertEqual((2.5, 3.5, -0.5), Geometry.snap_position(position, 'CENTER'))
    self.assertEqual((2.5, 4.0, -0.5), Geometry.snap_position(position, 'FACE'))
    self.assertEqual((2.0, 4.0, -0.5), Geometry.snap_position(position, 'EDGE'))
    self.assertEqual((-0.5, -1.5, 7.5), Geometry.snap_position((-0.9, -1.1, 7.0001), 'CENTER'))
    self.assertEqual((0.5, 1.0, 1.0), Geometry.snap_position((0.5, 0.5, 0.5), 'EDGE'), 'a tie goes to the first axis')
    generator = random.Random(2031)
    for target, halves in Geometry.SNAP_HALVES.items():
      for _ in range(300):
        point = tuple(generator.uniform(-50.0, 50.0) for _ in range(3))
        snapped = Geometry.snap_position(point, target)
        self.assertEqual(halves, sum(1 for value in snapped if value % 1.0 == 0.5), f'{target} {point} {snapped}')
        self.assertEqual(snapped, Geometry.snap_position(snapped, target), 'snapping twice changes nothing')
        self.assertLessEqual(math.dist(point, snapped), math.sqrt(3.0) / 2.0 + 1.0e-12, 'within half a voxel')
        # No other target of its kind is nearer: try the neighbors half a voxel away on every axis.
        for step in itertools.product((-1.0, 0.0, 1.0), repeat=3):
          other = tuple(value + delta for value, delta in zip(snapped, step))
          self.assertGreaterEqual(math.dist(point, other) + 1.0e-12, math.dist(point, snapped), f'{target} {point}')

  def test_draws_one_voxel_as_six_outward_faces(self):
    vertices, faces, colors = Geometry.surface([NvfFormat.pack_voxel_record(0, 0, 0, 5)])
    self.assertEqual(8, len(vertices), 'a cube\'s corners, each once')
    self.assertEqual(sorted(itertools.product((0, 1), repeat=3)), sorted(vertices))
    self.assertEqual(6, len(faces))
    self.assertEqual([5] * 6, colors)
    for face in faces:
      corners = [vertices[index] for index in face]
      a, b, c = corners[0], corners[1], corners[2]
      u = tuple(q - p for p, q in zip(a, b))
      v = tuple(q - p for p, q in zip(a, c))
      normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
      outward = tuple(sum(corner[axis] for corner in corners) / 4.0 - 0.5 for axis in range(3))
      self.assertGreater(sum(n * o for n, o in zip(normal, outward)), 0.0, f'face {corners} faces out')

  def test_draws_no_face_between_two_voxels(self):
    def faces_of(cells):
      return len(Geometry.surface([NvfFormat.pack_voxel_record(x, y, z, 0) for x, y, z in cells])[1])

    self.assertEqual(10, faces_of([(0, 0, 0), (1, 0, 0)]))
    self.assertEqual(24, faces_of(list(itertools.product((0, 1), repeat=3))))
    solid = list(itertools.product(range(3), repeat=3))
    self.assertEqual(54, faces_of(solid), 'a solid 3 x 3 x 3 shows its outside only')
    self.assertEqual(60, faces_of([cell for cell in solid if cell != (1, 1, 1)]), 'a hollow one shows its inside too')

  def test_draws_in_blenders_axes(self):
    vertices, _, _ = Geometry.surface([NvfFormat.pack_voxel_record(1, 2, 3, 0)])
    self.assertEqual((1, 3, 2), min(vertices), 'NVF voxel (1, 2, 3) is Blender cell (1, 3, 2)')
    self.assertEqual((2, 4, 3), max(vertices))

  # §7: the preview grows with the surface. An independent count: six faces a voxel, less two for each touching pair.
  def test_draws_the_assets_surfaces(self):
    for name in ('Frigate', 'CapitalShip', 'MilitaryStation'):
      model = NvfFormat.parse_nvf_model(read_repository_file(f'GameData/{name}.nvf'))
      part = model.parts[0]
      records = model.records[part.first_voxel:part.first_voxel + part.voxel_count]
      cells = {NvfFormat.unpack_voxel_record(record)[:3] for record in records}
      touching = sum(1 for x, y, z in cells for step in ((1, 0, 0), (0, 1, 0), (0, 0, 1))
                     if (x + step[0], y + step[1], z + step[2]) in cells)
      vertices, faces, colors = Geometry.surface(records)
      self.assertEqual(6 * len(cells) - 2 * touching, len(faces), name)
      self.assertEqual(len(faces), len(colors), name)
      self.assertEqual(len(vertices), len(set(vertices)), f'{name}: each corner once')


if __name__ == '__main__':
  unittest.main()
