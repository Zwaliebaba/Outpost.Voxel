"""What the Blender extension works out without Blender: NVF's axes against Blender's, the cube's 24 turns, snapping
to the voxel grid, and the surface the preview draws.

NVF is left-handed, +Y up and +Z forward (Design/NeuronVoxelFormat.md §4.1); Blender is right-handed and +Z up. A point
(x, y, z) in one is (x, z, y) in the other, and the swap is its own inverse. The import and export operators convert
through this module, and nothing else converts (§7). It imports nothing from Blender, so the Linux CI job tests it with
NvfFormat.py (Design/ADR/ADR-020).

Rotations here are quaternions spelled (x, y, z, w), as NVF stores them, except where a name says Blender's order,
(w, x, y, z).
"""

import itertools
import math

try:
  from . import NvfFormat
except ImportError:  # loaded on its own, as the tests load it
  import NvfFormat

# √½ as single precision holds it, the value NvfImport's table of turns spells HALF_SQRT2.
HALF_SQRT2 = NvfFormat.as_single(math.sqrt(0.5))


def nvf_to_blender_point(point):
  """A point, size or offset in NVF's axes as Blender's: (x, y, z) becomes (x, z, y)."""
  x, y, z = point
  return (x, z, y)


def blender_to_nvf_point(point):
  """A point in Blender's axes as NVF's; the swap is its own inverse."""
  x, y, z = point
  return (x, z, y)


def nvf_to_blender_rotation(rotation):
  """A rotation in NVF's axes, (x, y, z, w), as Blender's, in Blender's order (w, x, y, z).

  The matrix R becomes P R P, where P swaps y and z: conjugating by a reflection keeps the axis, swapped, and turns the
  other way. So the vector part is swapped and negated, and w is kept. Negation is exact, so the round trip is too.
  """
  x, y, z, w = rotation
  return (w, -x, -z, -y)


def blender_to_nvf_rotation(rotation):
  """A rotation in Blender's axes and order, (w, x, y, z), as NVF's (x, y, z, w)."""
  w, x, y, z = rotation
  return (-x, -z, -y, w)


def rotate(rotation, vector):
  """`vector` turned by the unit quaternion `rotation` (x, y, z, w), q v q*, in whichever axes both are in."""
  x, y, z, w = rotation
  vx, vy, vz = vector
  tx = 2.0 * (y * vz - z * vy)
  ty = 2.0 * (z * vx - x * vz)
  tz = 2.0 * (x * vy - y * vx)
  return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))


def canonical_rotation(rotation):
  """`rotation` of unit length and spelled as NVF stores it: w >= 0, and for a half turn, w = 0, the first nonzero of
  x, y and z positive. The length is taken in double precision and each component rounded to single; no component is
  a negative zero. Raises ValueError for a rotation of no length or one that is not finite."""
  if not all(math.isfinite(value) for value in rotation):
    raise ValueError('a rotation that is not finite')
  length = math.sqrt(sum(value * value for value in rotation))
  if length == 0.0:
    raise ValueError('a rotation of no length')
  x, y, z, w = (value / length for value in rotation)
  first = next((value for value in (x, y, z) if value != 0.0), 0.0)
  if w < 0.0 or (w == 0.0 and first < 0.0):
    x, y, z, w = -x, -y, -z, -w
  return tuple(NvfFormat.as_single(value) + 0.0 for value in (x, y, z, w))


def cube_turns():
  """The cube's 24 turns, spelled as NvfImport's table spells them (Design/ADR/ADR-020): the identity, the three half
  turns about an axis, the six quarter turns, the six half turns about a face diagonal, and the eight thirds of a turn
  about a body diagonal."""
  h = HALF_SQRT2
  turns = [(0.0, 0.0, 0.0, 1.0), (1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 1.0, 0.0)]
  for axis in range(3):
    for sign in (1.0, -1.0):
      quarter = [0.0, 0.0, 0.0, h]
      quarter[axis] = sign * h
      turns.append(tuple(quarter))
  turns += [(h, h, 0.0, 0.0), (h, -h, 0.0, 0.0), (h, 0.0, h, 0.0), (h, 0.0, -h, 0.0), (0.0, h, h, 0.0),
            (0.0, h, -h, 0.0)]
  turns += [(x, y, z, 0.5) for x, y, z in itertools.product((0.5, -0.5), repeat=3)]
  return tuple(turns)


CUBE_TURNS = cube_turns()


def snap_rotation(rotation):
  """The cube turn nearest `rotation`, (x, y, z, w) in NVF's axes: the one whose quaternion has the largest dot product
  with it, either sign. The 24 turns are the same set in either space's axes."""
  return max(CUBE_TURNS, key=lambda turn: abs(sum(a * b for a, b in zip(turn, rotation))))


# How many of a snap target's coordinates are half-integers, the rest being integers: a voxel's center lies inside it,
# a face center on one of its faces, an edge midpoint on one of its edges (§7).
SNAP_HALVES = {'CENTER': 3, 'FACE': 2, 'EDGE': 1}


def snap_position(position, target):
  """`position` moved to the nearest voxel center, face center or edge midpoint, as `target` says: one of SNAP_HALVES.
  The grid is the same in either space's axes. Of two targets equally near, the one reached first wins, which keeps a
  snap repeatable."""
  halves = SNAP_HALVES[target]
  candidates = []
  for half_axes in itertools.combinations(range(3), halves):
    candidates.append(tuple(math.floor(value) + 0.5 if axis in half_axes else float(math.floor(value + 0.5))
                            for axis, value in enumerate(position)))
  return min(candidates, key=lambda candidate: sum((a - b) ** 2 for a, b in zip(candidate, position)))


# A cell's six faces in Blender's axes: the neighbor each faces, and its corners counterclockwise seen from outside, so
# that the face's normal, by the right-hand rule Blender takes, points at the neighbor.
CELL_FACES = (
  ((1, 0, 0), ((1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1))),
  ((-1, 0, 0), ((0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0))),
  ((0, 1, 0), ((0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0))),
  ((0, -1, 0), ((0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1))),
  ((0, 0, 1), ((0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1))),
  ((0, 0, -1), ((0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0))),
)


def surface(records):
  """The preview's mesh of one part, in Blender's axes and in part space: every voxel face whose neighbor in the part
  is empty, and no face between two voxels, so that it grows with the surface rather than the volume (§7).

  Returns (vertices, faces, colors): the corners, each once; each face's four corner indices, counterclockwise seen
  from outside; and each face's palette index, 0-15. Faces follow the records' order, and each record's faces
  CELL_FACES' order.
  """
  cells = {}
  for record in records:
    x, y, z, color = NvfFormat.unpack_voxel_record(record)
    cells[(x, z, y)] = color
  vertices = []
  indices = {}
  faces = []
  colors = []
  for (x, y, z), color in cells.items():
    for (dx, dy, dz), corners in CELL_FACES:
      if (x + dx, y + dy, z + dz) in cells:
        continue
      face = []
      for cx, cy, cz in corners:
        corner = (x + cx, y + cy, z + cz)
        index = indices.get(corner)
        if index is None:
          index = indices[corner] = len(vertices)
          vertices.append(corner)
        face.append(index)
      faces.append(tuple(face))
      colors.append(color)
  return vertices, faces, colors
