"""The extension inside Blender: its operators on the golden file and the assets, headless.

These need Blender's bpy module, which CI does not have, so they skip there (§9: CI has no Blender). Blender publishes
bpy as a Python package for the Python it bundles; with Python 3.11,

  pip install bpy==4.2.*        (or 4.5.*, the current LTS)
  python -m unittest discover -s Tools/Blender/NeuronVoxelFormat/Tests -p "*Tests.py"

runs them with the rest. They load the extension as a package, as Blender does, and register it as Blender would.
The by-hand checklist, Checklist.md, covers what a headless run cannot: the viewport, the panel and the dialogs.
"""

import base64
import importlib
import sys
import tempfile
import unittest
from pathlib import Path

try:
  import bpy
except ImportError:
  bpy = None

from RepositoryFile import find_repository_file, read_repository_file

GOLDEN_NVF_PATH = 'Tools/Golden/Golden.nvf'
ASSETS = ('Frigate', 'CapitalShip', 'MilitaryStation')


@unittest.skipIf(bpy is None, 'needs Blender\'s bpy module: pip install bpy==4.2.* on Python 3.11')
class ExtensionTests(unittest.TestCase):

  @classmethod
  def setUpClass(cls):
    tools = Path(__file__).resolve().parents[2]
    if str(tools) not in sys.path:
      sys.path.insert(0, str(tools))
    cls.extension = importlib.import_module('NeuronVoxelFormat')
    cls.NvfFormat = importlib.import_module('NeuronVoxelFormat.NvfFormat')
    cls.Geometry = importlib.import_module('NeuronVoxelFormat.Geometry')
    cls.ImportOperator = importlib.import_module('NeuronVoxelFormat.ImportOperator')
    cls.ExportOperator = importlib.import_module('NeuronVoxelFormat.ExportOperator')
    cls.extension.register()

  @classmethod
  def tearDownClass(cls):
    cls.extension.unregister()

  def setUp(self):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    self.directory = tempfile.TemporaryDirectory()
    self.addCleanup(self.directory.cleanup)

  def import_model(self, path):
    """Imports the file at `path` and makes its collection the active one; returns the collection."""
    names = set(bpy.data.collections.keys())
    self.assertEqual({'FINISHED'}, bpy.ops.import_scene.nvf(filepath=str(path)))
    collection = next(bpy.data.collections[name] for name in bpy.data.collections.keys() if name not in names)
    bpy.context.view_layer.active_layer_collection = bpy.context.view_layer.layer_collection.children[collection.name]
    return collection

  def export_model(self, name):
    path = Path(self.directory.name) / name
    self.assertEqual({'FINISHED'}, bpy.ops.export_scene.nvf(filepath=str(path)))
    return path

  def objects(self, collection):
    return {obj.name.split(': ', 1)[1]: obj for obj in collection.all_objects}

  def select(self, obj):
    for other in bpy.context.view_layer.objects:
      other.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

  # §7: parts parented as the tree and placed at their translations, pivots and hardpoints as empties, all in Blender's
  # axes: NVF's (x, y, z) is Blender's (x, z, y).
  def test_imports_the_golden_file_in_blenders_axes(self):
    collection = self.import_model(find_repository_file(GOLDEN_NVF_PATH))
    objects = self.objects(collection)
    hull, turret, barrel = objects['hull'], objects['hull/turret'], objects['hull/turret/barrel']
    self.assertEqual((None, hull, turret), (hull.parent, turret.parent, barrel.parent))
    self.assertEqual((-2.0, -3.0, 0.0), tuple(hull.location), 'the hull at NVF (-2, 0, -3)')
    self.assertEqual((1.0, 2.0, 3.0), tuple(turret.location), 'the turret at NVF (1, 3, 2)')
    for part in (hull, turret, barrel):
      self.assertTrue(part.hide_select, f'{part.name} cannot be selected')
      self.assertTrue(all(part.lock_location) and all(part.lock_rotation) and all(part.lock_scale), part.name)
    self.assertEqual('SPHERE', objects['hull pivot'].empty_display_type)
    self.assertEqual((1.5, 1.5, 0.0), tuple(objects['hull/turret pivot'].location),
                     'the turret\'s pivot, NVF (1.5, 0, 1.5)')

    dock = objects['dock.aft']
    self.assertEqual(('ARROWS', 'QUATERNION', hull), (dock.empty_display_type, dock.rotation_mode, dock.parent))
    self.assertEqual((2.5, 3.5, 3.0), tuple(dock.location), 'dock.aft at NVF (2.5, 3, 3.5)')
    self.assertEqual(('dock.aft', True), (dock['nvf_name'], dock['nvf_from_vox']))
    weapon = objects['weapon.main']
    self.assertFalse(weapon['nvf_from_vox'])
    forward = self.Geometry.rotate(tuple(weapon.rotation_quaternion)[1:] + (weapon.rotation_quaternion[0],), (0, 1, 0))
    self.assertAlmostEqual(0.5, forward[0], delta=1.0e-6, msg='turned 30 degrees, weapon.main faces toward +X')
    self.assertAlmostEqual(3.0 ** 0.5 / 2.0, forward[1], delta=1.0e-6)

    text = collection['nvf_bytes']
    self.assertEqual(read_repository_file(GOLDEN_NVF_PATH), base64.b64decode(text.as_string()), 'the file, kept')
    model = self.NvfFormat.parse_nvf_model(read_repository_file(GOLDEN_NVF_PATH))
    for part, obj in zip(model.parts, (hull, turret, barrel)):
      _, faces, colors = self.Geometry.surface(model.records[part.first_voxel:part.first_voxel + part.voxel_count])
      self.assertEqual(len(faces), len(obj.data.polygons), part.path)
      attribute = obj.data.color_attributes['nvf_color']
      entry = model.palette[colors[0]]
      self.assertEqual([round(channel * 255) for channel in attribute.data[0].color_srgb],
                       [entry.red, entry.green, entry.blue, entry.alpha], f'{part.path}: its first face\'s color')

  # N5: an untouched model exports byte for byte, the golden file and the three assets alike.
  def test_exports_an_untouched_model_byte_for_byte(self):
    for relative in (GOLDEN_NVF_PATH,) + tuple(f'GameData/{name}.nvf' for name in ASSETS):
      bpy.ops.wm.read_factory_settings(use_empty=True)
      self.import_model(find_repository_file(relative))
      exported = self.export_model(Path(relative).name)
      self.assertEqual(read_repository_file(relative), exported.read_bytes(), relative)

  # §7's panel: add a hardpoint at the cursor, snap it, turn it, rename it; export writes it and keeps the rest.
  def test_adds_snaps_and_renames_hardpoints(self):
    self.import_model(find_repository_file('GameData/Frigate.nvf'))
    bpy.context.scene.cursor.location = (3.2, 11.9, 4.1)
    self.assertEqual({'FINISHED'},
                     bpy.ops.nvf.add_hardpoint(part='main', hardpoint_type='engine', identifier='main'))
    engine = bpy.context.view_layer.objects.active
    self.assertEqual('engine.main', engine['nvf_name'])
    self.assertEqual({'FINISHED'}, bpy.ops.nvf.snap_position(target='FACE'))
    engine.rotation_quaternion = (0.9, 0.0, 0.0, 0.4)
    self.assertEqual({'FINISHED'}, bpy.ops.nvf.snap_rotation())
    self.assertEqual({'FINISHED'}, bpy.ops.nvf.add_hardpoint(part='main', hardpoint_type='weapon', identifier='left'))
    self.assertEqual({'FINISHED'}, bpy.ops.nvf.rename_hardpoint(name='weapon.front'))
    with self.assertRaises(RuntimeError, msg='a name that is taken'):
      bpy.ops.nvf.rename_hardpoint(name='engine.main')
    with self.assertRaises(RuntimeError, msg='a name that breaks §4.1'):
      bpy.ops.nvf.add_hardpoint(part='main', hardpoint_type='pivot', identifier='x')

    model = self.NvfFormat.load_nvf_model(self.export_model('Frigate.nvf'))
    engine, weapon = model.hardpoints
    self.assertEqual(('engine.main', 'weapon.front'), (engine.name, weapon.name))
    frigate = self.NvfFormat.load_nvf_model(find_repository_file('GameData/Frigate.nvf'))
    translation = frigate.parts[0].translation
    cursor = self.Geometry.blender_to_nvf_point((3.2, 11.9, 4.1))
    part_space = tuple(value - offset for value, offset in zip(cursor, translation))
    self.assertEqual(self.Geometry.snap_position(part_space, 'FACE'), engine.position, 'snapped to a face center')
    self.assertIn(engine.rotation, self.Geometry.CUBE_TURNS, 'turned by a quarter turn exactly')
    self.assertFalse(engine.from_vox or weapon.from_vox, 'authored in Blender')
    self.assertEqual(frigate.records, model.records, 'the voxels, untouched')

  # §7: export refuses rather than guesses, and names what to fix.
  def test_refuses_to_export_what_it_cannot_write(self):
    collection = self.import_model(find_repository_file(GOLDEN_NVF_PATH))
    objects = self.objects(collection)
    objects['hull/turret'].location = (1.0, 2.0, 4.0)
    objects['sensor.top.left'].scale = (2.0, 2.0, 2.0)
    objects['engine.main']['nvf_name'] = 'dock.aft'
    stored, model, problems = self.ExportOperator.check(collection)
    self.assertIsNone(model)
    self.assertEqual(3, len(problems), problems)
    with self.assertRaises(RuntimeError):
      bpy.ops.export_scene.nvf(filepath=str(Path(self.directory.name) / 'Refused.nvf'))
    self.assertFalse((Path(self.directory.name) / 'Refused.nvf').exists(), 'nothing is written')
    self.assertEqual({'FINISHED'}, bpy.ops.nvf.validate(), 'Validate lists the problems and changes nothing')

  # The owner's rule of 2026-09-28: export does not write over voxels NvfImport has changed since the import (N5).
  def test_refuses_to_write_over_newer_voxels(self):
    path = Path(self.directory.name) / 'Frigate.nvf'
    path.write_bytes(read_repository_file('GameData/Frigate.nvf'))
    self.import_model(path)
    newer = self.NvfFormat.load_nvf_model(path)
    x, y, z, color = self.NvfFormat.unpack_voxel_record(newer.records[0])
    newer.records[0] = self.NvfFormat.pack_voxel_record(x, y, z, (color + 1) % 16)
    self.NvfFormat.save_nvf_model(newer, path)
    written = path.read_bytes()
    with self.assertRaises(RuntimeError) as caught:
      bpy.ops.export_scene.nvf(filepath=str(path))
    self.assertIn('NvfImport has run since', str(caught.exception))
    self.assertEqual(written, path.read_bytes(), 'the newer file is left as it was')

  # A marker moved in Blender becomes Blender's (§6.2); one left alone stays the .vox's.
  def test_clears_from_vox_on_a_moved_marker_only(self):
    collection = self.import_model(find_repository_file(GOLDEN_NVF_PATH))
    objects = self.objects(collection)
    self.select(objects['engine.main'])
    self.assertEqual({'FINISHED'}, bpy.ops.nvf.snap_position(target='EDGE'))
    model = self.NvfFormat.load_nvf_model(self.export_model('Golden.nvf'))
    flags = {hardpoint.name: hardpoint.from_vox for hardpoint in model.hardpoints}
    self.assertEqual({'dock.aft': True, 'engine.main': False, 'sensor.top.left': False, 'weapon.main': False}, flags)

  def test_registers_and_unregisters_cleanly(self):
    self.extension.unregister()
    try:
      self.assertFalse(hasattr(bpy.types, 'VIEW3D_PT_nvf'), 'the panel is gone')
      with self.assertRaises((AttributeError, RuntimeError)):
        bpy.ops.import_scene.nvf(filepath=str(find_repository_file(GOLDEN_NVF_PATH)))
    finally:
      self.extension.register()
    self.assertTrue(hasattr(bpy.types, 'VIEW3D_PT_nvf'), 'the panel is back')


if __name__ == '__main__':
  unittest.main()
