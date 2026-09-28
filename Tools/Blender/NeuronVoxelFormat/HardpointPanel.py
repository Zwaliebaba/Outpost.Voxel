"""The sidebar's NVF panel, N › NVF: add a hardpoint to a part, rename one, snap hardpoints and pivots to the voxel
grid and hardpoints' rotations to quarter turns, and Validate, which lists every reason export would refuse (§7).

Snapping works in the part's space and through Geometry, so that it lands on the grid exactly, and is exact in the
file too.
"""

from pathlib import Path

import bpy
from bpy.props import EnumProperty, StringProperty
from mathutils import Matrix, Quaternion, Vector

from . import ExportOperator, Geometry, NvfFormat, Rebuild
from .ImportOperator import (FROM_VOX_PROPERTY, NAME_PROPERTY, PART_PROPERTY, PATH_PROPERTY, PIVOT_PROPERTY,
                             ImportOperator, display_name, hardpoint_objects, model_collection, new_hardpoint_object,
                             part_objects)

# The types a new hardpoint is offered besides those the model already has; any other can be typed (§4.1).
COMMON_TYPES = ('dock', 'engine', 'sensor', 'weapon')


def is_hardpoint(obj):
  return obj is not None and NAME_PROPERTY in obj


def is_pivot(obj):
  return obj is not None and PIVOT_PROPERTY in obj


def model_objects(context, test):
  """The selected objects of the active model that pass `test`, or the active object alone when none is selected."""
  collection = model_collection(context)
  if collection is None:
    return []
  members = set(collection.all_objects)
  chosen = [obj for obj in context.selected_objects if obj in members and test(obj)]
  if not chosen and context.active_object in members and test(context.active_object):
    chosen = [context.active_object]
  return chosen


def set_part_space(obj, location, rotation=None):
  """Places `obj` at `location`, and turns it to `rotation` (Blender's w, x, y, z) when one is given, in its parent's
  space and Blender's axes: through its channels when it is plain, so that the values land exactly, and through its
  matrices otherwise."""
  if ExportOperator.is_plain(obj):
    obj.location = location
    if rotation is not None:
      obj.rotation_quaternion = rotation
    return
  current_location, current_rotation, scale = (obj.matrix_parent_inverse @ obj.matrix_basis).decompose()
  wanted = Matrix.LocRotScale(Vector(location), Quaternion(rotation) if rotation is not None else current_rotation,
                              scale)
  obj.matrix_basis = obj.matrix_parent_inverse.inverted() @ wanted


def part_suggestions(_self, context, edit_text):
  collection = model_collection(context)
  paths = sorted(part_objects(collection)) if collection is not None else []
  return [path for path in paths if edit_text in path]


def type_suggestions(_self, context, edit_text):
  collection = model_collection(context)
  types = set(COMMON_TYPES)
  if collection is not None:
    types |= {str(obj[NAME_PROPERTY]).split('.', 1)[0] for obj in hardpoint_objects(collection)}
  return [kind for kind in sorted(types) if edit_text in kind]


class AddHardpointOperator(bpy.types.Operator):
  """Add a hardpoint to a part of the active model, at the 3D cursor, facing the part's forward"""
  bl_idname = 'nvf.add_hardpoint'
  bl_label = 'Add Hardpoint'
  bl_options = {'REGISTER', 'UNDO'}

  part: StringProperty(name='Part', search=part_suggestions)
  hardpoint_type: StringProperty(name='Type', default='engine', search=type_suggestions)
  identifier: StringProperty(name='Identifier', default='main', description='The rest of the name, after the type')

  @classmethod
  def poll(cls, context):
    return model_collection(context) is not None

  def invoke(self, context, _event):
    collection = model_collection(context)
    obj = context.active_object
    paths = part_objects(collection)
    if obj is not None and obj.parent is not None and obj.parent in paths.values():
      self.part = next(path for path, part in paths.items() if part == obj.parent)
    elif not self.part or self.part not in paths:
      self.part = min(paths, key=len, default='')
    return context.window_manager.invoke_props_dialog(self)

  def execute(self, context):
    collection = model_collection(context)
    part_object = part_objects(collection).get(self.part)
    if part_object is None:
      self.report({'ERROR'}, f'{collection.name} has no part named {self.part!r}')
      return {'CANCELLED'}
    name = f'{self.hardpoint_type}.{self.identifier}'
    if not NvfFormat.is_nvf_hardpoint_name(name):
      self.report({'ERROR'}, f'{name!r} is not a hardpoint name: two or more segments of a-z, 0-9 and _ joined by '
                             'dots, the first not "pivot"')
      return {'CANCELLED'}
    if any(obj[NAME_PROPERTY] == name for obj in hardpoint_objects(collection)):
      self.report({'ERROR'}, f'{collection.name} already has a hardpoint named {name}')
      return {'CANCELLED'}
    position = part_object.matrix_world.inverted() @ context.scene.cursor.location
    hardpoint = NvfFormat.NvfHardpoint(name, 0, Geometry.blender_to_nvf_point(tuple(position)), (0.0, 0.0, 0.0, 1.0),
                                       False)
    obj = new_hardpoint_object(collection, part_object, hardpoint)
    for selected in context.selected_objects:
      selected.select_set(False)
    obj.select_set(True)
    context.view_layer.objects.active = obj
    return {'FINISHED'}


class RenameHardpointOperator(bpy.types.Operator):
  """Rename the active hardpoint. A hardpoint from the .vox is Blender's once renamed"""
  bl_idname = 'nvf.rename_hardpoint'
  bl_label = 'Rename Hardpoint'
  bl_options = {'REGISTER', 'UNDO'}

  name: StringProperty(name='Name')

  @classmethod
  def poll(cls, context):
    return is_hardpoint(context.active_object) and model_collection(context) is not None

  def invoke(self, context, _event):
    self.name = str(context.active_object[NAME_PROPERTY])
    return context.window_manager.invoke_props_dialog(self)

  def execute(self, context):
    collection = model_collection(context)
    obj = context.active_object
    if not NvfFormat.is_nvf_hardpoint_name(self.name):
      self.report({'ERROR'}, f'{self.name!r} is not a hardpoint name: two or more segments of a-z, 0-9 and _ joined by '
                             'dots, the first not "pivot"')
      return {'CANCELLED'}
    if any(other != obj and other[NAME_PROPERTY] == self.name for other in hardpoint_objects(collection)):
      self.report({'ERROR'}, f'{collection.name} already has a hardpoint named {self.name}')
      return {'CANCELLED'}
    if self.name != obj[NAME_PROPERTY]:
      obj[NAME_PROPERTY] = self.name
      obj[FROM_VOX_PROPERTY] = False
      obj.name = display_name(collection, self.name)
    return {'FINISHED'}


class SnapPositionOperator(bpy.types.Operator):
  """Move the selected hardpoints and pivots to the nearest voxel center, face center or edge midpoint of their part"""
  bl_idname = 'nvf.snap_position'
  bl_label = 'Snap Position'
  bl_options = {'REGISTER', 'UNDO'}

  target: EnumProperty(name='Target', items=(
    ('CENTER', 'Voxel Center', 'The nearest voxel\'s center'),
    ('FACE', 'Face Center', 'The nearest center of a voxel\'s face'),
    ('EDGE', 'Edge Midpoint', 'The nearest midpoint of a voxel\'s edge'),
  ))

  @classmethod
  def poll(cls, context):
    return bool(model_objects(context, lambda obj: is_hardpoint(obj) or is_pivot(obj)))

  def execute(self, context):
    for obj in model_objects(context, lambda candidate: is_hardpoint(candidate) or is_pivot(candidate)):
      location = ExportOperator.part_space(obj)[0]
      snapped = Geometry.snap_position(Geometry.blender_to_nvf_point(location), self.target)
      set_part_space(obj, Geometry.nvf_to_blender_point(snapped))
    return {'FINISHED'}


class SnapRotationOperator(bpy.types.Operator):
  """Turn the selected hardpoints to the nearest quarter turns"""
  bl_idname = 'nvf.snap_rotation'
  bl_label = 'Snap Rotation'
  bl_options = {'REGISTER', 'UNDO'}

  @classmethod
  def poll(cls, context):
    return bool(model_objects(context, is_hardpoint))

  def execute(self, context):
    for obj in model_objects(context, is_hardpoint):
      location, rotation, _ = ExportOperator.part_space(obj)
      snapped = Geometry.snap_rotation(Geometry.blender_to_nvf_rotation(rotation))
      set_part_space(obj, location, Geometry.nvf_to_blender_rotation(snapped))
    return {'FINISHED'}


class ValidateOperator(bpy.types.Operator):
  """List every reason export would refuse the active model, and whether it may write over the file it came from"""
  bl_idname = 'nvf.validate'
  bl_label = 'Validate'

  @classmethod
  def poll(cls, context):
    return model_collection(context) is not None

  def execute(self, context):
    collection = model_collection(context)
    stored, _, problems = ExportOperator.check(collection)
    path = collection.get(PATH_PROPERTY)
    if stored is not None and path:
      refusal = Rebuild.target_refusal(stored, Path(path))
      if refusal:
        problems.append(f'Over the file it came from: {refusal}')
    for problem in problems:
      self.report({'WARNING'}, problem)
    # A popup needs Blender's interface; run headless, as the tests run it, the reports are the whole answer.
    if not bpy.app.background:
      def draw(menu, _context):
        for problem in problems or ['Nothing to fix: the model exports as it is.']:
          menu.layout.label(text=problem)
      title = f'{collection.name}: {len(problems)} problem(s)' if problems else f'{collection.name} is valid'
      context.window_manager.popup_menu(draw, title=title, icon='ERROR' if problems else 'CHECKMARK')
    if not problems:
      self.report({'INFO'}, f'{collection.name} is valid')
    return {'FINISHED'}


class HardpointPanel(bpy.types.Panel):
  """The active model's hardpoints and pivots"""
  bl_idname = 'VIEW3D_PT_nvf'
  bl_label = 'Neuron Voxel'
  bl_space_type = 'VIEW_3D'
  bl_region_type = 'UI'
  bl_category = 'NVF'

  def draw(self, context):
    layout = self.layout
    collection = model_collection(context)
    if collection is None:
      layout.label(text='Select an object of an NVF model,')
      layout.label(text='or make its collection active.')
      layout.operator(ImportOperator.bl_idname, text='Import .nvf', icon='IMPORT')
      return
    layout.label(text=collection.name, icon='OUTLINER_COLLECTION')

    obj = context.active_object
    box = layout.box()
    if is_hardpoint(obj):
      name = str(obj[NAME_PROPERTY])
      box.label(text=name, icon='EMPTY_ARROWS')
      box.label(text=f'Type: {name.split(".", 1)[0]}')
      part = obj.parent.get(PART_PROPERTY) if obj.parent is not None else None
      box.label(text=f'Part: {part or "none"}')
      box.label(text='From the .vox' if obj.get(FROM_VOX_PROPERTY) else 'Authored in Blender')
      box.operator(RenameHardpointOperator.bl_idname, icon='GREASEPENCIL')
    elif is_pivot(obj):
      box.label(text=f'Pivot of {obj[PIVOT_PROPERTY]}', icon='SPHERE')
    else:
      box.label(text='Select a hardpoint or a pivot')

    layout.operator(AddHardpointOperator.bl_idname, icon='ADD')
    layout.label(text='Snap position to')
    row = layout.row(align=True)
    for target, text in (('CENTER', 'Center'), ('FACE', 'Face'), ('EDGE', 'Edge')):
      row.operator(SnapPositionOperator.bl_idname, text=text).target = target
    layout.operator(SnapRotationOperator.bl_idname, text='Snap Rotation to 90°')
    layout.operator(ValidateOperator.bl_idname, icon='CHECKMARK')
    layout.operator(ExportOperator.ExportOperator.bl_idname, text='Export .nvf', icon='EXPORT')


CLASSES = (AddHardpointOperator, RenameHardpointOperator, SnapPositionOperator, SnapRotationOperator, ValidateOperator,
           HardpointPanel)


def register():
  for cls in CLASSES:
    bpy.utils.register_class(cls)


def unregister():
  for cls in reversed(CLASSES):
    bpy.utils.unregister_class(cls)
