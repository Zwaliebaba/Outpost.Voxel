"""The export's rules, without Blender: the model a scene says to write, or every reason to refuse it (§7).

The export operator reads the scene into ScenePart, SceneHardpoint and ScenePivot records, already in NVF's axes and
in each part's space, and hands them here with the model the .blend stores, the file as it was imported. The palette,
the part tree and the voxels come from the stored model, so they are written back byte for byte (N5). The scene gives
the pivots and the hardpoints. The panel's Validate runs the same rules and lists what they find. This module imports
nothing from Blender, so the Linux CI job tests it (Design/ADR/ADR-020).
"""

import math
from dataclasses import dataclass, replace

try:
  from . import Geometry, NvfFormat
except ImportError:  # loaded on its own, as the tests load it
  import Geometry
  import NvfFormat

# How far a part may sit from its translation, or a scale from 1, before export calls it moved or scaled: far below a
# voxel, and far above what float rounding leaves once a transform is composed and taken apart again.
TOLERANCE = 1.0e-4


@dataclass
class ScenePart:
  """A part's object."""
  object_name: str
  path: str               # its nvf_part property
  parent_path: str | None  # its parent object's nvf_part: None for no parent, '' for a parent that is not a part
  translation: tuple      # in its parent's space, in NVF's axes
  turned: bool            # rotated, scaled or sheared at all


@dataclass
class SceneHardpoint:
  """A hardpoint's empty."""
  object_name: str
  name: str               # its nvf_name property
  part_path: str | None   # the part its object is parented to; None when it is not parented to a part
  position: tuple         # in its part's space, in NVF's axes
  rotation: tuple         # (x, y, z, w) in NVF's axes, as Blender holds it: of any length and either sign
  scale: tuple
  from_vox: bool          # its nvf_from_vox property


@dataclass
class ScenePivot:
  """A part's pivot empty."""
  object_name: str
  part: str               # its nvf_pivot property: the part whose pivot it is
  part_path: str | None   # the part its object is parented to
  position: tuple         # in its part's space, in NVF's axes


def single(values):
  """Each value as the file stores it, without a negative zero."""
  return tuple(NvfFormat.as_single(value) + 0.0 for value in values)


def rebuild(stored, parts, hardpoints, pivots):
  """The model the scene says to write, and every reason to refuse it, as (model, problems).

  The model is None when there is a problem; each problem is a sentence that names the object and what to do. A
  hardpoint keeps its stored values, and FromVox with them, while its name, its part, its position and its rotation are
  those it was imported with, as single precision holds them; otherwise Blender owns it from now on (§6.2). A pivot
  moved from where it was imported becomes PivotAuthored.
  """
  problems = []
  if stored.unknown_chunks:
    ids = ', '.join(repr(chunk)[2:-1] for chunk in stored.unknown_chunks)
    problems.append(f'The file holds chunks this version does not know ({ids}); rewriting it would lose them (§4.6).')

  # The part tree comes from the file; the scene may only hold it as it was imported.
  part_index = {part.path: index for index, part in enumerate(stored.parts)}
  scene_parts = {}
  for part in parts:
    if part.path in scene_parts:
      problems.append(f'{scene_parts[part.path].object_name} and {part.object_name} are both part {part.path}: a part '
                      'was copied. Delete the copy; parts change in MagicaVoxel (N5).')
    elif part.path not in part_index:
      problems.append(f'{part.object_name} is part {part.path}, which the file does not hold: a part was added or '
                      'renamed. Parts change in MagicaVoxel (N5).')
    else:
      scene_parts[part.path] = part
  for stored_part in stored.parts:
    part = scene_parts.get(stored_part.path)
    if part is None:
      problems.append(f'Part {stored_part.path} has no object: it was deleted or renamed. Import the file again.')
      continue
    parent = None if stored_part.parent == NvfFormat.NO_PARENT else stored.parts[stored_part.parent].path
    if part.parent_path != parent:
      problems.append(f'{part.object_name}, part {stored_part.path}, was reparented: its parent is '
                      f'{parent or "no object"} in the file. Parts change in MagicaVoxel (N5).')
    if part.turned or any(abs(a - b) > TOLERANCE for a, b in zip(part.translation, stored_part.translation)):
      problems.append(f'{part.object_name}, part {stored_part.path}, was moved, turned or scaled. Parts are placed '
                      'in MagicaVoxel (N5).')

  # Pivots: one per part, parented to it.
  scene_pivots = {}
  for pivot in pivots:
    if pivot.part not in part_index:
      problems.append(f'{pivot.object_name} is the pivot of part {pivot.part}, which the file does not hold.')
    elif pivot.part in scene_pivots:
      problems.append(f'Part {pivot.part} has two pivots, {scene_pivots[pivot.part].object_name} and '
                      f'{pivot.object_name}. Delete one.')
    else:
      scene_pivots[pivot.part] = pivot
      if pivot.part_path != pivot.part:
        problems.append(f'{pivot.object_name}, the pivot of part {pivot.part}, is not parented to its part.')
      if not all(math.isfinite(value) for value in pivot.position):
        problems.append(f'{pivot.object_name}, the pivot of part {pivot.part}, is not at a finite position.')
  model_parts = []
  for stored_part in stored.parts:
    pivot = scene_pivots.get(stored_part.path)
    if pivot is None:
      problems.append(f'Part {stored_part.path} has no pivot: its pivot object was deleted. Import the file again.')
      continue
    position = single(pivot.position) if all(math.isfinite(value) for value in pivot.position) else None
    if position is None or position == tuple(stored_part.pivot):
      model_parts.append(replace(stored_part))
    else:
      model_parts.append(replace(stored_part, pivot=position, pivot_authored=True))

  # Hardpoints: named as §4.1 spells them, each name once, parented to a part, unscaled.
  stored_hardpoints = {hardpoint.name: hardpoint for hardpoint in stored.hardpoints}
  named = {}
  model_hardpoints = []
  for hardpoint in hardpoints:
    what = f'{hardpoint.object_name}, hardpoint {hardpoint.name}'
    if not NvfFormat.is_nvf_hardpoint_name(hardpoint.name):
      problems.append(f'{hardpoint.object_name} is named {hardpoint.name!r}, which is not a hardpoint name: two or '
                      'more segments of a-z, 0-9 and _ joined by dots, the first not "pivot" (§4.1).')
    named.setdefault(hardpoint.name, []).append(hardpoint.object_name)
    if hardpoint.part_path not in part_index:
      problems.append(f'{what}, is not parented to a part.')
    if any(abs(value - 1.0) > TOLERANCE for value in hardpoint.scale):
      problems.append(f'{what}, is scaled. A hardpoint has no scale; set it back to 1.')
    if not all(math.isfinite(value) for value in hardpoint.position + hardpoint.rotation):
      problems.append(f'{what}, is not at a finite position or rotation.')
      continue
    if not any(hardpoint.rotation):
      problems.append(f'{what}, has a rotation of no length.')
      continue
    if hardpoint.part_path not in part_index:
      continue
    part = part_index[hardpoint.part_path]
    position = single(hardpoint.position)
    original = stored_hardpoints.get(hardpoint.name)
    if (original is not None and original.part == part and position == tuple(original.position)
        and single(hardpoint.rotation) == tuple(original.rotation)):
      model_hardpoints.append(replace(original, from_vox=original.from_vox and hardpoint.from_vox))
    else:
      model_hardpoints.append(NvfFormat.NvfHardpoint(hardpoint.name, part, position,
                                                     Geometry.canonical_rotation(hardpoint.rotation), False))
  for name, objects in named.items():
    if len(objects) > 1:
      problems.append(f'{" and ".join(objects)} are all named {name}; a hardpoint\'s name is unique in the file '
                      '(§4.1).')
  if len(hardpoints) > NvfFormat.MAX_HARDPOINTS:
    problems.append(f'The model has {len(hardpoints)} hardpoints, more than the {NvfFormat.MAX_HARDPOINTS} a file '
                    'holds.')

  if problems:
    return None, problems
  model = NvfFormat.NvfModel(palette=list(stored.palette), parts=model_parts, records=stored.records,
                             hardpoints=model_hardpoints)
  try:
    NvfFormat.serialize_nvf_model(model)
  except NvfFormat.NvfError as error:
    return None, [f'The writer refuses the model: {error.name} (§4.4).']
  return model, []


def voxels_differ(stored, target):
  """Whether `target`, the file export would write over, holds other voxels than `stored`: another palette, part tree,
  size, translation or voxel record. Its pivots and hardpoints may differ, since they are what export writes; its
  voxels may not, since writing would put back the ones the model was imported with (N5)."""
  if len(stored.parts) != len(target.parts):
    return True
  for a, b in zip(stored.parts, target.parts):
    if ((a.path, a.parent, tuple(a.size), tuple(a.translation), a.first_voxel, a.voxel_count)
        != (b.path, b.parent, tuple(b.size), tuple(b.translation), b.first_voxel, b.voxel_count)):
      return True
  return stored.palette != target.palette or list(stored.records) != list(target.records)


def target_refusal(stored, path):
  """Why export must not write over the file at `path`, or None when it may: it may when there is no file there, or one
  that holds the voxels `stored` holds. Another file there is left alone, as NvfImport leaves one it cannot read, and
  so is an NVF file with other voxels, which NvfImport wrote since the model was imported: writing would put back the
  old voxels (N5)."""
  if not path.exists():
    return None
  try:
    current = NvfFormat.load_nvf_model(path)
  except NvfFormat.NvfError as error:
    return (f'{path.name} is there already and is not a model this version reads ({error.name}). Export elsewhere, or '
            'move it away first.')
  if voxels_differ(stored, current):
    return (f'{path.name} holds other voxels, colors or parts than this model was imported with: NvfImport has run '
            'since. Import it again and redo this session\'s changes there; exporting would put the old voxels back.')
  return None
