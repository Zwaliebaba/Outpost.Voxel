"""MagicaVoxel .vox files written from the engine's axes (Design/Archive/SampleRenderer.md §7.1).

The design generator describes every model in the engine's axes, Direct3D's: left-handed, +Y up and +Z forward
(Design/Archive/NeuronVoxelFormat.md §4.1). A .vox holds MagicaVoxel's, right-handed with +Z up, and NeuronCore's
reader swaps y and z as it reads (NeuronCore/VoxModel.cpp). This module is the other side of that swap: it swaps every
size, voxel, translation and rotation as it writes, so that what the reader returns is what the generator described.

The file holds what MagicaVoxel writes for a scene of several models (MagicaVoxel-file-format-vox.txt and
-vox-extension.txt): SIZE and XYZI for each model; a scene graph of a root transform, one group, and a named transform
over a shape for each model; eight layers; the palette; and a material for each emissive palette entry.
"""

import struct
from dataclasses import dataclass, field

VOX_VERSION = 200

# The engine draws sixteen palette entries (AGENTS.md R14); a voxel's color is its entry, 1 to 16.
PALETTE_ENTRY_COUNT = 16

# MagicaVoxel writes eight layers, and its editor expects them.
LAYER_COUNT = 8

# A rotation as a 3 x 3 matrix of 0 and +-1, in the engine's axes: row i, column j, where column j is the image of axis j.
Matrix = tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]

IDENTITY: Matrix = ((1, 0, 0), (0, 1, 0), (0, 0, 1))


@dataclass(frozen=True)
class PaletteColor:
  """One palette entry: sRGB bytes, and whether it glows (a material of _type _emit)."""
  red: int
  green: int
  blue: int
  emit: float = 0.0  # 0 for a surface that does not glow
  flux: float = 1.0


@dataclass
class VoxModel:
  """One model of the scene, in the engine's axes.

  Its voxels map a cell of the model's own grid, (0, 0, 0) to size - 1, to a palette entry, 1 to 16. The transform above
  it places the model's centre voxel, size // 2 on each axis, at center, and turns the model about that voxel by
  rotation, as NeuronCore's reader does (ModelInstance).
  """
  name: str
  size: tuple[int, int, int]
  voxels: dict[tuple[int, int, int], int]
  center: tuple[int, int, int]
  rotation: Matrix = IDENTITY


@dataclass
class VoxScene:
  """A whole file: its models, in order, and its palette's first sixteen entries."""
  models: list[VoxModel] = field(default_factory=list)
  palette: list[PaletteColor] = field(default_factory=list)


def _swap(vector: tuple[int, int, int]) -> tuple[int, int, int]:
  """Between the engine's axes and MagicaVoxel's, either way: y and z change places."""
  return (vector[0], vector[2], vector[1])


def _chunk(chunk_id: bytes, content: bytes, children: bytes = b'') -> bytes:
  return chunk_id + struct.pack('<ii', len(content), len(children)) + content + children


def _string(text: str) -> bytes:
  data = text.encode('utf-8')
  return struct.pack('<i', len(data)) + data


def _dictionary(attributes: dict[str, str]) -> bytes:
  data = struct.pack('<i', len(attributes))
  for key, value in attributes.items():
    data += _string(key) + _string(value)
  return data


def _format_number(value: float) -> str:
  """A float as MagicaVoxel writes one: its shortest round-tripping decimal, without a trailing .0."""
  text = repr(float(value))
  return text[:-2] if text.endswith('.0') else text


def rotation_byte(rotation: Matrix) -> int:
  """_r's bits for rotation, given in the engine's axes: the inverse of NeuronCore's ParseRotation.

  MagicaVoxel's matrix is P R P, where P swaps y and z. Its row r holds its one nonzero entry in the column that bits
  0-1 (row 0) and 2-3 (row 1) name, row 2 in the column left over, and bits 4, 5 and 6 set the three rows' signs negative.
  """
  swap = (0, 2, 1)
  matrix = [[rotation[swap[i]][swap[j]] for j in range(3)] for i in range(3)]
  columns = []
  bits = 0
  for row in range(3):
    nonzero = [column for column in range(3) if matrix[row][column] != 0]
    if len(nonzero) != 1 or abs(matrix[row][nonzero[0]]) != 1:
      raise ValueError(f'not a rotation of the cube: {rotation}')
    columns.append(nonzero[0])
    if matrix[row][nonzero[0]] < 0:
      bits |= 1 << (4 + row)
  if sorted(columns) != [0, 1, 2]:
    raise ValueError(f'not a rotation of the cube: {rotation}')
  determinant = (matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
                 matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
                 matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]))
  if determinant != 1:
    raise ValueError(f'a reflection, not a rotation: {rotation}')
  return bits | columns[0] | (columns[1] << 2)


def _size_chunk(model: VoxModel) -> bytes:
  return _chunk(b'SIZE', struct.pack('<iii', *_swap(model.size)))


def _voxels_chunk(model: VoxModel) -> bytes:
  cells = sorted((_swap(cell), entry) for cell, entry in model.voxels.items())
  content = struct.pack('<i', len(cells))
  for (x, y, z), entry in cells:
    content += struct.pack('<BBBB', x, y, z, entry)
  return _chunk(b'XYZI', content)


def _transform_chunk(node: int, attributes: dict[str, str], child: int, layer: int, frame: dict[str, str]) -> bytes:
  content = struct.pack('<i', node) + _dictionary(attributes) + struct.pack('<iiii', child, -1, layer, 1) + _dictionary(frame)
  return _chunk(b'nTRN', content)


def _group_chunk(node: int, children: list[int]) -> bytes:
  content = struct.pack('<i', node) + _dictionary({}) + struct.pack('<i', len(children))
  content += b''.join(struct.pack('<i', child) for child in children)
  return _chunk(b'nGRP', content)


def _shape_chunk(node: int, model: int) -> bytes:
  return _chunk(b'nSHP', struct.pack('<i', node) + _dictionary({}) + struct.pack('<ii', 1, model) + _dictionary({}))


def _check(model: VoxModel) -> None:
  """Refuses what the reader would refuse, so that a mistake in a design fails here, by name, and not in the importer."""
  if any(not 1 <= extent <= 256 for extent in model.size):
    raise ValueError(f'{model.name}: a model is 1 to 256 voxels on each axis, not {model.size}')
  if not model.voxels:
    raise ValueError(f'{model.name}: a model with no voxel')
  for cell, entry in model.voxels.items():
    if any(not 0 <= cell[axis] < model.size[axis] for axis in range(3)):
      raise ValueError(f'{model.name}: voxel {cell} lies outside its model of size {model.size}')
    if not 1 <= entry <= PALETTE_ENTRY_COUNT:
      raise ValueError(f'{model.name}: palette entry {entry} is not 1 to {PALETTE_ENTRY_COUNT}')
  if model.rotation != IDENTITY and any(extent % 2 == 0 for extent in model.size):
    raise ValueError(f'{model.name}: only a model odd on every axis may be turned')


def serialize_vox(scene: VoxScene) -> bytes:
  """The bytes of scene as a .vox file of version 200."""
  if len(scene.palette) != PALETTE_ENTRY_COUNT:
    raise ValueError(f'a palette of {PALETTE_ENTRY_COUNT} entries, not {len(scene.palette)}')
  children = b''
  for model in scene.models:
    _check(model)
    children += _size_chunk(model) + _voxels_chunk(model)

  # The scene graph: transform 0 over group 1, and under the group a transform and a shape for each model.
  shape_transforms = [2 + 2 * index for index in range(len(scene.models))]
  children += _transform_chunk(0, {}, 1, -1, {})
  children += _group_chunk(1, shape_transforms)
  for index, model in enumerate(scene.models):
    attributes = {'_name': model.name} if model.name else {}
    translation = _swap(model.center)
    frame = {'_t': f'{translation[0]} {translation[1]} {translation[2]}'}
    if model.rotation != IDENTITY:
      frame['_r'] = str(rotation_byte(model.rotation))
    children += _transform_chunk(shape_transforms[index], attributes, shape_transforms[index] + 1, 0, frame)
    children += _shape_chunk(shape_transforms[index] + 1, index)

  for layer in range(LAYER_COUNT):
    children += _chunk(b'LAYR', struct.pack('<i', layer) + _dictionary({}) + struct.pack('<i', -1))

  palette = b''
  for color in scene.palette:
    palette += struct.pack('<BBBB', color.red, color.green, color.blue, 255)
  palette += bytes(4 * (256 - PALETTE_ENTRY_COUNT))
  children += _chunk(b'RGBA', palette)

  for index, color in enumerate(scene.palette):
    if color.emit > 0.0:
      attributes = {'_type': '_emit', '_emit': _format_number(color.emit), '_flux': _format_number(color.flux)}
      children += _chunk(b'MATL', struct.pack('<i', index + 1) + _dictionary(attributes))

  return b'VOX ' + struct.pack('<i', VOX_VERSION) + _chunk(b'MAIN', b'', children)
