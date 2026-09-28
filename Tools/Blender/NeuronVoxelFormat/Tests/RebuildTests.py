"""Rebuild.py: what export writes for a scene, what it refuses, and when it will not write over a file (§7).

A scene here is Rebuild's records made straight from a model, as import places it, in NVF's axes; GeometryTests holds
the swap into Blender's axes and back to exactness, so nothing is lost between the two. No Blender is needed; the
Linux CI job runs them.
"""

import math
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

EXTENSION = Path(__file__).resolve().parents[1]  # the extension's folder, which holds the modules under test
if str(EXTENSION) not in sys.path:
  sys.path.insert(0, str(EXTENSION))

import NvfFormat  # noqa: E402  pylint: disable=wrong-import-position
import Rebuild  # noqa: E402
from NvfGolden import GOLDEN_NVF_PATH  # noqa: E402
from RepositoryFile import read_repository_file  # noqa: E402

GOLDEN = read_repository_file(GOLDEN_NVF_PATH)


def golden():
  return NvfFormat.parse_nvf_model(GOLDEN)


def scene_of(model):
  """The records of a scene that holds `model` as import placed it: (parts, hardpoints, pivots)."""
  parts = [Rebuild.ScenePart(f'part {part.path}', part.path,
                             None if part.parent == NvfFormat.NO_PARENT else model.parts[part.parent].path,
                             tuple(part.translation), False) for part in model.parts]
  hardpoints = [Rebuild.SceneHardpoint(f'empty {hardpoint.name}', hardpoint.name, model.parts[hardpoint.part].path,
                                       tuple(hardpoint.position), tuple(hardpoint.rotation), (1.0, 1.0, 1.0),
                                       hardpoint.from_vox) for hardpoint in model.hardpoints]
  pivots = [Rebuild.ScenePivot(f'pivot {part.path}', part.path, part.path, tuple(part.pivot)) for part in model.parts]
  return parts, hardpoints, pivots


class RebuildTests(unittest.TestCase):

  def rebuilt(self, stored, parts, hardpoints, pivots):
    model, problems = Rebuild.rebuild(stored, parts, hardpoints, pivots)
    self.assertEqual([], problems)
    return model

  def refused(self, stored, parts, hardpoints, pivots, *fragments):
    """Asserts that export refuses, and that each fragment is in a problem of its own."""
    model, problems = Rebuild.rebuild(stored, parts, hardpoints, pivots)
    self.assertIsNone(model)
    for fragment in fragments:
      self.assertTrue(any(fragment in problem for problem in problems), f'{fragment!r} in {problems}')
    return problems

  def hardpoint(self, model, name):
    return next(hardpoint for hardpoint in model.hardpoints if hardpoint.name == name)

  # N5: a scene left as import placed it writes the file back byte for byte, the golden file and the assets alike.
  def test_writes_an_untouched_scene_back_byte_for_byte(self):
    stored = golden()
    self.assertEqual(GOLDEN, NvfFormat.serialize_nvf_model(self.rebuilt(stored, *scene_of(stored))))
    for name in ('Frigate', 'CapitalShip', 'MilitaryStation'):
      data = read_repository_file(f'GameData/{name}.nvf')
      stored = NvfFormat.parse_nvf_model(data)
      self.assertEqual(data, NvfFormat.serialize_nvf_model(self.rebuilt(stored, *scene_of(stored))), name)

  # §6.2: FromVox stays while a hardpoint is as the marker left it, and goes when Blender touches it.
  def test_keeps_from_vox_only_while_a_hardpoint_is_untouched(self):
    stored = golden()
    self.assertTrue(self.hardpoint(stored, 'engine.main').from_vox, 'the golden engine comes from the .vox')

    def exported(change):
      parts, hardpoints, pivots = scene_of(stored)
      hardpoints = [change(hardpoint) if hardpoint.name == 'engine.main' else hardpoint for hardpoint in hardpoints]
      return self.rebuilt(stored, parts, hardpoints, pivots)

    self.assertTrue(self.hardpoint(exported(lambda h: h), 'engine.main').from_vox, 'untouched')
    moved = exported(lambda h: replace(h, position=(2.5, 1.5, 1.5)))
    self.assertFalse(self.hardpoint(moved, 'engine.main').from_vox, 'moved')
    self.assertEqual((2.5, 1.5, 1.5), self.hardpoint(moved, 'engine.main').position)
    turned = exported(lambda h: replace(h, rotation=(0.0, 0.0, 0.0, 1.0)))
    self.assertFalse(self.hardpoint(turned, 'engine.main').from_vox, 'turned')
    renamed = exported(lambda h: replace(h, name='engine.aft'))
    self.assertFalse(self.hardpoint(renamed, 'engine.aft').from_vox, 'renamed')
    reparented = exported(lambda h: replace(h, part_path='hull/turret'))
    self.assertFalse(self.hardpoint(reparented, 'engine.main').from_vox, 'moved to another part')
    self.assertEqual(1, self.hardpoint(reparented, 'engine.main').part)
    claimed = exported(lambda h: replace(h, from_vox=False))
    self.assertFalse(self.hardpoint(claimed, 'engine.main').from_vox, 'its property cleared')
    self.assertTrue(self.hardpoint(claimed, 'dock.aft').from_vox, 'the other marker keeps its flag')

  # A hardpoint Blender owns is written as single precision holds it, of unit length, spelled with w >= 0.
  def test_writes_what_blender_holds_as_nvf_stores_it(self):
    stored = golden()
    parts, hardpoints, pivots = scene_of(stored)
    hardpoints.append(Rebuild.SceneHardpoint('empty weapon.left', 'weapon.left', 'hull', (0.1, 0.2, -0.0),
                                             (0.0, 0.0, 0.0, -2.0), (1.0, 1.0, 1.0), False))
    hardpoints = [hardpoint for hardpoint in hardpoints if hardpoint.name != 'sensor.top.left']
    model = self.rebuilt(stored, parts, hardpoints, pivots)
    added = self.hardpoint(model, 'weapon.left')
    self.assertEqual(tuple(NvfFormat.as_single(value) for value in (0.1, 0.2, 0.0)), added.position)
    self.assertGreater(math.copysign(1.0, added.position[2]), 0.0, 'no negative zero is written')
    self.assertEqual((0.0, 0.0, 0.0, 1.0), added.rotation, 'normalized and spelled with w >= 0')
    self.assertFalse(added.from_vox)
    self.assertNotIn('sensor.top.left', [hardpoint.name for hardpoint in model.hardpoints], 'a deleted hardpoint goes')
    data = NvfFormat.serialize_nvf_model(model)
    self.assertEqual(['dock.aft', 'engine.main', 'weapon.left', 'weapon.main'],
                     [hardpoint.name for hardpoint in NvfFormat.parse_nvf_model(data).hardpoints], 'in name order')

  # §6.2: a pivot set in Blender is PivotAuthored, so that a re-import keeps it.
  def test_marks_a_moved_pivot_authored(self):
    stored = golden()
    parts, hardpoints, pivots = scene_of(stored)
    pivots = [replace(pivot, position=(1.0, 1.0, 1.0)) if pivot.part == 'hull' else pivot for pivot in pivots]
    model = self.rebuilt(stored, parts, hardpoints, pivots)
    self.assertEqual(((1.0, 1.0, 1.0), True), (model.parts[0].pivot, model.parts[0].pivot_authored), 'moved')
    self.assertEqual((stored.parts[1].pivot, True), (model.parts[1].pivot, model.parts[1].pivot_authored),
                     'the turret\'s, authored already and untouched')
    self.assertEqual((stored.parts[2].pivot, False), (model.parts[2].pivot, model.parts[2].pivot_authored),
                     'the barrel\'s, untouched')

  # §7: the part tree is the file's; export refuses a part added, deleted, renamed, reparented or moved.
  def test_refuses_changes_to_the_part_tree(self):
    stored = golden()
    parts, hardpoints, pivots = scene_of(stored)
    self.refused(stored, parts[:2], hardpoints, pivots, 'Part hull/turret/barrel has no object')
    self.refused(stored, parts + [replace(parts[1], object_name='part copy')], hardpoints, pivots,
                 'part copy are both part hull/turret')
    self.refused(stored, parts + [Rebuild.ScenePart('part new', 'hull/new', 'hull', (0, 0, 0), False)], hardpoints,
                 pivots, 'part hull/new, which the file does not hold')
    renamed = [replace(parts[2], path='hull/turret/gun')] + parts[:2]
    self.refused(stored, renamed, hardpoints, pivots, 'hull/turret/gun, which the file does not hold',
                 'Part hull/turret/barrel has no object')
    self.refused(stored, [parts[0], parts[1], replace(parts[2], parent_path='hull')], hardpoints, pivots,
                 'was reparented: its parent is hull/turret in the file')
    self.refused(stored, [replace(parts[0], parent_path='')] + parts[1:], hardpoints, pivots,
                 'its parent is no object in the file')
    self.refused(stored, [parts[0], replace(parts[1], translation=(1.0, 3.0, 2.001))], hardpoints, pivots,
                 'part hull/turret, was moved')
    self.refused(stored, [parts[0], parts[1], replace(parts[2], turned=True)], hardpoints, pivots,
                 'part hull/turret/barrel, was moved, turned or scaled')
    self.rebuilt(stored, [parts[0], replace(parts[1], translation=(1.0, 3.0, 2.00001))] + parts[2:], hardpoints, pivots)

  def test_refuses_hardpoints_it_cannot_write(self):
    stored = golden()
    parts, hardpoints, pivots = scene_of(stored)

    def with_changed(**changes):
      return [replace(hardpoints[0], **changes)] + hardpoints[1:]

    self.refused(stored, parts, with_changed(part_path=None), pivots, 'hardpoint dock.aft, is not parented to a part')
    self.refused(stored, parts, with_changed(part_path='elsewhere'), pivots, 'is not parented to a part')
    self.refused(stored, parts, with_changed(scale=(1.0, 2.0, 1.0)), pivots, 'is scaled')
    self.refused(stored, parts, with_changed(name='Dock.aft'), pivots, 'is named \'Dock.aft\'')
    self.refused(stored, parts, with_changed(name='pivot.aft'), pivots, 'which is not a hardpoint name')
    self.refused(stored, parts, with_changed(name='engine.main'), pivots, 'are all named engine.main')
    self.refused(stored, parts, with_changed(position=(math.nan, 0.0, 0.0)), pivots, 'not at a finite position')
    self.refused(stored, parts, with_changed(rotation=(0.0, 0.0, 0.0, math.inf)), pivots, 'or rotation')
    self.refused(stored, parts, with_changed(rotation=(0.0, 0.0, 0.0, 0.0)), pivots, 'a rotation of no length')
    many = [Rebuild.SceneHardpoint(f'empty {i}', f'light.l{i}', 'hull', (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0),
                                   (1.0, 1.0, 1.0), False) for i in range(NvfFormat.MAX_HARDPOINTS + 1)]
    self.refused(stored, parts, many, pivots, 'more than the 4096 a file holds')

  def test_refuses_pivots_it_cannot_write(self):
    stored = golden()
    parts, hardpoints, pivots = scene_of(stored)
    self.refused(stored, parts, hardpoints, pivots[1:], 'Part hull has no pivot')
    self.refused(stored, parts, hardpoints, pivots + [replace(pivots[0], object_name='pivot copy')],
                 'Part hull has two pivots, pivot hull and pivot copy')
    self.refused(stored, parts, hardpoints, [replace(pivots[0], part_path='hull/turret')] + pivots[1:],
                 'the pivot of part hull, is not parented to its part')
    odd = Rebuild.ScenePivot('pivot odd', 'hull/odd', 'hull/odd', (0, 0, 0))
    self.refused(stored, parts, hardpoints, pivots + [odd], 'the pivot of part hull/odd, which the file does not hold')
    self.refused(stored, parts, hardpoints, [replace(pivots[0], position=(0.0, math.inf, 0.0))] + pivots[1:],
                 'is not at a finite position')

  # §6.2's rule for the importer holds for the export too: every problem at once, so that one pass fixes them all.
  def test_lists_every_problem_at_once(self):
    stored = golden()
    parts, hardpoints, pivots = scene_of(stored)
    problems = self.refused(stored, parts[1:], [replace(hardpoints[1], scale=(2.0, 2.0, 2.0))] + hardpoints[2:],
                            pivots[:2], 'Part hull has no object', 'is scaled', 'Part hull/turret/barrel has no pivot')
    self.assertEqual(3, len(problems), problems)

  # §4.6: a tool that rewrites a file refuses one that holds chunks it does not know.
  def test_refuses_files_with_chunks_it_does_not_know(self):
    stored = golden()
    stored.unknown_chunks = [b'NOTE', b'\x01zz\x00']
    self.refused(stored, *scene_of(golden()), '(NOTE, \\x01zz\\x00)', 'rewriting it would lose them')

  # The owner's rule of 2026-09-28: export never writes over voxels NvfImport has changed since the import (N5).
  def test_refuses_to_write_over_other_voxels(self):
    stored = golden()
    with tempfile.TemporaryDirectory() as directory:
      path = Path(directory) / 'Golden.nvf'
      self.assertIsNone(Rebuild.target_refusal(stored, path), 'no file there')
      path.write_bytes(GOLDEN)
      self.assertIsNone(Rebuild.target_refusal(stored, path), 'the file imported')

      other = golden()
      other.hardpoints = other.hardpoints[1:]
      other.parts[0] = replace(other.parts[0], pivot=(0.0, 0.0, 0.0), pivot_authored=True)
      NvfFormat.save_nvf_model(other, path)
      self.assertIsNone(Rebuild.target_refusal(stored, path), 'other hardpoints and pivots, which export writes')

      for what, change in (
        ('a voxel', lambda model: model.records.__setitem__(0, NvfFormat.pack_voxel_record(0, 0, 1, 0))),
        ('a color', lambda model: setattr(model.palette[3], 'red', 1)),
        ('a translation', lambda model: model.parts.__setitem__(1, replace(model.parts[1], translation=(1, 3, 3)))),
      ):
        changed = golden()
        change(changed)
        NvfFormat.save_nvf_model(changed, path)
        refusal = Rebuild.target_refusal(stored, path)
        self.assertIsNotNone(refusal, what)
        self.assertIn('NvfImport has run since', refusal, what)

      path.write_bytes(b'not a model')
      self.assertIn('NotAnNvfFile', Rebuild.target_refusal(stored, path), 'a file this version cannot read')


if __name__ == '__main__':
  unittest.main()
