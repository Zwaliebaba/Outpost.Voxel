"""File › Export › Neuron Voxel (.nvf): the file the model was imported from, with the pivots and hardpoints the
scene holds now (§7).

The scene is read into Rebuild's records, swapped into NVF's axes by Geometry, and Rebuild decides what to write or
why not to. The voxels, palette and part tree come from the bytes the .blend stores, so they go back byte for byte
(N5).
"""

from pathlib import Path

import bpy
from bpy.props import StringProperty
from bpy_extras.io_utils import ExportHelper

from . import Geometry, NvfFormat, Rebuild
from .ImportOperator import (FROM_VOX_PROPERTY, NAME_PROPERTY, PART_PROPERTY, PATH_PROPERTY, PIVOT_PROPERTY,
                             model_collection, part_objects, stored_model)

IDENTITY = tuple(tuple(1.0 if row == column else 0.0 for column in range(4)) for row in range(4))


def is_identity(matrix):
  return tuple(tuple(row) for row in matrix) == IDENTITY


def is_plain(obj):
  """Whether nothing stands between the object's channels and its parent's space: no parent inverse, no deltas, and a
  rotation held as a quaternion, as import makes a hardpoint."""
  return (is_identity(obj.matrix_parent_inverse) and obj.rotation_mode == 'QUATERNION'
          and tuple(obj.delta_location) == (0.0, 0.0, 0.0)
          and tuple(obj.delta_rotation_quaternion) == (1.0, 0.0, 0.0, 0.0)
          and tuple(obj.delta_scale) == (1.0, 1.0, 1.0))


def part_space(obj):
  """The object's location, rotation (Blender's w, x, y, z) and scale in its parent's space, in Blender's axes.

  They are read from its channels when the object is plain, so that one left as import placed it gives back exactly
  the values import set; otherwise they are composed from its matrices, deltas and all.
  """
  if is_plain(obj):
    return tuple(obj.location), tuple(obj.rotation_quaternion), tuple(obj.scale)
  location, rotation, scale = (obj.matrix_parent_inverse @ obj.matrix_basis).decompose()
  return tuple(location), tuple(rotation), tuple(scale)


def snapshot(collection):
  """The model's objects as Rebuild's records, in NVF's axes: (parts, hardpoints, pivots)."""
  paths = {obj: path for path, obj in part_objects(collection).items()}

  def parent_path(obj):
    if obj.parent is None:
      return None
    return paths.get(obj.parent, '') if obj.parent_type == 'OBJECT' else ''

  parts = []
  hardpoints = []
  pivots = []
  for obj in collection.all_objects:
    location, rotation, scale = part_space(obj)
    if PART_PROPERTY in obj:
      unturned = min(sum((a - b) ** 2 for a, b in zip(rotation, identity))
                     for identity in ((1, 0, 0, 0), (-1, 0, 0, 0)))
      turned = unturned > Rebuild.TOLERANCE ** 2 or any(abs(value - 1.0) > Rebuild.TOLERANCE for value in scale)
      parts.append(Rebuild.ScenePart(obj.name, str(obj[PART_PROPERTY]), parent_path(obj),
                                     Geometry.blender_to_nvf_point(location), turned))
    elif NAME_PROPERTY in obj:
      hardpoints.append(Rebuild.SceneHardpoint(obj.name, str(obj[NAME_PROPERTY]), parent_path(obj) or None,
                                               Geometry.blender_to_nvf_point(location),
                                               Geometry.blender_to_nvf_rotation(rotation),
                                               Geometry.blender_to_nvf_point(scale),
                                               bool(obj.get(FROM_VOX_PROPERTY, False))))
    elif PIVOT_PROPERTY in obj:
      pivots.append(Rebuild.ScenePivot(obj.name, str(obj[PIVOT_PROPERTY]), parent_path(obj) or None,
                                       Geometry.blender_to_nvf_point(location)))
  return parts, hardpoints, pivots


def check(collection):
  """The model to write for the collection, and every reason not to, as Rebuild.rebuild gives them."""
  try:
    stored = stored_model(collection)
  except NvfFormat.NvfError as error:
    return None, None, [f'The file {collection.name} was imported from is not in the .blend, or is damaged '
                        f'({error.name}). Import it again.']
  model, problems = Rebuild.rebuild(stored, *snapshot(collection))
  return stored, model, problems


class ExportOperator(bpy.types.Operator, ExportHelper):
  """Export the active Neuron Voxel model: its voxels as imported, with the pivots and hardpoints the scene holds"""
  bl_idname = 'export_scene.nvf'
  bl_label = 'Export Neuron Voxel'

  filename_ext = '.nvf'
  filter_glob: StringProperty(default='*.nvf', options={'HIDDEN'})

  @classmethod
  def poll(cls, context):
    if model_collection(context) is None:
      cls.poll_message_set('Select an object of an NVF model, or make its collection active')
      return False
    return True

  def invoke(self, context, event):
    path = model_collection(context).get(PATH_PROPERTY)
    if path:
      self.filepath = path
    return ExportHelper.invoke(self, context, event)

  def execute(self, context):
    collection = model_collection(context)
    stored, model, problems = check(collection)
    if not problems:
      refusal = Rebuild.target_refusal(stored, Path(self.filepath))
      problems = [refusal] if refusal else []
    if problems:
      for problem in problems:
        self.report({'ERROR'}, problem)
      return {'CANCELLED'}
    try:
      NvfFormat.save_nvf_model(model, self.filepath)
    except NvfFormat.NvfError as error:
      self.report({'ERROR'}, f'Cannot write {self.filepath}: {error.name}')
      return {'CANCELLED'}
    self.report({'INFO'}, f'Exported {Path(self.filepath).name}: {len(model.hardpoints)} hardpoint(s)')
    return {'FINISHED'}


def menu_entry(self, _context):
  self.layout.operator(ExportOperator.bl_idname, text='Neuron Voxel (.nvf)')


def register():
  bpy.utils.register_class(ExportOperator)
  bpy.types.TOPBAR_MT_file_export.append(menu_entry)


def unregister():
  bpy.types.TOPBAR_MT_file_export.remove(menu_entry)
  bpy.utils.unregister_class(ExportOperator)
