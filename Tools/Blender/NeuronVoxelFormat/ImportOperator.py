"""File › Import › Neuron Voxel (.nvf): one collection per file (§7).

The collection holds one object per part, parented as the part tree is and placed at its translation, with a locked
surface preview; a sphere empty per part for its pivot; and an arrows empty per hardpoint, parented to its part. The
file's bytes stay in the .blend, base64 in a text datablock the collection points at, so that export needs nothing but
the .blend. Geometry swaps every coordinate from NVF's axes into Blender's, and one Blender unit is one voxel.

This module also says how a model lives in the scene, for the export and the panel: which collection is a model's,
and which of its objects are parts, pivots and hardpoints, by their custom properties.
"""

import base64
import binascii
from pathlib import Path

import bpy
from bpy.props import StringProperty
from bpy_extras.io_utils import ImportHelper

from . import Geometry, NvfFormat, Preview

# The custom properties that say what a collection or an object is to NVF.
BYTES_PROPERTY = 'nvf_bytes'        # a model's collection: the text datablock holding the file's bytes
PATH_PROPERTY = 'nvf_path'          # a model's collection: the file it was imported from, which export offers
PART_PROPERTY = 'nvf_part'          # a part's object: its path
PIVOT_PROPERTY = 'nvf_pivot'        # a pivot's empty: its part's path
NAME_PROPERTY = 'nvf_name'          # a hardpoint's empty: its name; the object's own name is for display only
FROM_VOX_PROPERTY = 'nvf_from_vox'  # a hardpoint's empty: it came from a marker in the .vox (§6.2)

HARDPOINT_DISPLAY_SIZE = 1.0
PIVOT_DISPLAY_SIZE = 0.25


def model_collection(context):
  """The collection of the model the user is working on: the active object's, or else the active collection, when it
  is a model's; None otherwise."""
  obj = context.active_object
  if obj is not None:
    for collection in obj.users_collection:
      if BYTES_PROPERTY in collection:
        return collection
  collection = context.view_layer.active_layer_collection.collection
  return collection if BYTES_PROPERTY in collection else None


def stored_model(collection):
  """The model as it was imported, from the bytes the collection stores. Raises NvfError when they are gone or damaged:
  FileNotFound for no text, ReadFailed for text that is not base64, and the reader's refusal for the rest."""
  text = collection.get(BYTES_PROPERTY)
  if not isinstance(text, bpy.types.Text):
    raise NvfFormat.NvfError('FileNotFound')
  try:
    data = base64.b64decode(text.as_string())
  except binascii.Error as error:
    raise NvfFormat.NvfError('ReadFailed') from error
  return NvfFormat.parse_nvf_model(data)


def part_objects(collection):
  """The collection's part objects, by path."""
  return {obj[PART_PROPERTY]: obj for obj in collection.all_objects if PART_PROPERTY in obj}


def hardpoint_objects(collection):
  return [obj for obj in collection.all_objects if NAME_PROPERTY in obj]


def display_name(collection, name):
  """An object's name in the outliner. It is for display only: object names are unique across a .blend, so two
  models' engine.main cannot share one."""
  return f'{collection.name}: {name}'


def new_hardpoint_object(collection, part_object, hardpoint):
  """An arrows empty for `hardpoint`, an NvfHardpoint, parented to `part_object` and placed in its space. The empty's
  +Y arrow is the hardpoint's forward and its +Z arrow its up (§7)."""
  obj = bpy.data.objects.new(display_name(collection, hardpoint.name), None)
  obj.empty_display_type = 'ARROWS'
  obj.empty_display_size = HARDPOINT_DISPLAY_SIZE
  obj.rotation_mode = 'QUATERNION'
  obj.parent = part_object
  obj.location = Geometry.nvf_to_blender_point(hardpoint.position)
  obj.rotation_quaternion = Geometry.nvf_to_blender_rotation(hardpoint.rotation)
  obj.lock_scale = (True, True, True)
  obj[NAME_PROPERTY] = hardpoint.name
  obj[FROM_VOX_PROPERTY] = hardpoint.from_vox
  collection.objects.link(obj)
  return obj


def build_model(name, path, data, model):
  """The collection for `model`, which `data`, the bytes of the file at `path`, holds."""
  collection = bpy.data.collections.new(name)
  text = bpy.data.texts.new(f'{collection.name}.nvf')
  text.from_string(base64.encodebytes(data).decode('ascii'))
  collection[BYTES_PROPERTY] = text
  collection[PATH_PROPERTY] = str(path)

  objects = []
  for part in model.parts:
    records = model.records[part.first_voxel:part.first_voxel + part.voxel_count]
    mesh = Preview.build_mesh(display_name(collection, part.path), records, model.palette)
    obj = bpy.data.objects.new(display_name(collection, part.path), mesh)
    if part.parent != NvfFormat.NO_PARENT:
      obj.parent = objects[part.parent]
    obj.location = Geometry.nvf_to_blender_point(part.translation)
    obj.lock_location = (True, True, True)
    obj.lock_rotation = (True, True, True)
    obj.lock_rotation_w = True
    obj.lock_scale = (True, True, True)
    obj.hide_select = True
    obj[PART_PROPERTY] = part.path
    collection.objects.link(obj)
    objects.append(obj)

    pivot = bpy.data.objects.new(display_name(collection, f'{part.path} pivot'), None)
    pivot.empty_display_type = 'SPHERE'
    pivot.empty_display_size = PIVOT_DISPLAY_SIZE
    pivot.parent = obj
    pivot.location = Geometry.nvf_to_blender_point(part.pivot)
    pivot.lock_rotation = (True, True, True)
    pivot.lock_rotation_w = True
    pivot.lock_scale = (True, True, True)
    pivot[PIVOT_PROPERTY] = part.path
    collection.objects.link(pivot)

  for hardpoint in model.hardpoints:
    new_hardpoint_object(collection, objects[hardpoint.part], hardpoint)
  return collection


class ImportOperator(bpy.types.Operator, ImportHelper):
  """Import a Neuron Voxel model: its voxels as a locked preview, its pivots and hardpoints as empties"""
  bl_idname = 'import_scene.nvf'
  bl_label = 'Import Neuron Voxel'
  bl_options = {'REGISTER', 'UNDO'}

  filename_ext = '.nvf'
  filter_glob: StringProperty(default='*.nvf', options={'HIDDEN'})

  def execute(self, context):
    path = Path(self.filepath)
    try:
      data = path.read_bytes()
    except OSError as error:
      self.report({'ERROR'}, f'Cannot read {path}: {error.strerror}')
      return {'CANCELLED'}
    try:
      model = NvfFormat.parse_nvf_model(data)
    except NvfFormat.NvfError as error:
      self.report({'ERROR'}, f'{path.name} is not a model this version reads: {error.name}')
      return {'CANCELLED'}
    collection = build_model(path.stem, path, data, model)
    context.scene.collection.children.link(collection)
    if model.unknown_chunks:
      self.report({'WARNING'}, f'{path.name} holds chunks this version does not know: it can be viewed, not exported')
    self.report({'INFO'}, f'Imported {path.name}: {len(model.parts)} part(s), {len(model.hardpoints)} hardpoint(s)')
    return {'FINISHED'}


def menu_entry(self, _context):
  self.layout.operator(ImportOperator.bl_idname, text='Neuron Voxel (.nvf)')


def register():
  bpy.utils.register_class(ImportOperator)
  bpy.types.TOPBAR_MT_file_import.append(menu_entry)


def unregister():
  bpy.types.TOPBAR_MT_file_import.remove(menu_entry)
  bpy.utils.unregister_class(ImportOperator)
