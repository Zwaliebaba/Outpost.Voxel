"""The MVP's hulls, modules and asteroids, written as MagicaVoxel files (Design/MvpPlan.md, phase 1).

Every model the MVP draws that the owner has not authored comes from here, deterministically: the four ship hulls and
the station core with their mounts as markers (Design/Archive/NeuronVoxelFormat.md §5), the modules those mounts hold,
and three asteroids. NvfImport turns each .vox into the .nvf the game reads, and CI holds the two together. Run it from
the repository root to rewrite GameData's generated files; it rewrites nothing else:

  python Tools/DesignGenerator/DesignGenerator.py

A model is described in the engine's axes, +Y up and +Z forward, and VoxFile.py swaps them into MagicaVoxel's.

Conventions, which Design/ADR/ADR-025 records:
- A mount's marker is named hull@<type>.<size>.<label>: its type is the kind of module it holds, and its size, s or l,
  the box that module fills: 3 x 3 x 5 or 5 x 5 x 9, +Z the way the module faces. The marker is that box, centered on
  the mount, and the importer keeps its centre and its turn and drops its voxels.
- A module's model faces +Z, and its centre voxel is the mount's centre.
- Palette entries 1 and 2 are light armor, 3 and 4 heavy armor, 5 trim and 6 lights, both light; 16 is the side's
  color (G24). A module's palette is its own, and its entry 16 is the side's color too.
"""

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

from VoxFile import IDENTITY, Matrix, PaletteColor, VoxModel, VoxScene, serialize_vox

Cell = tuple[int, int, int]

# The box a mount of each size holds, in the mount's frame: x right, y up, z the way it faces. Odd on every axis, so
# that its centre is a voxel's.
MOUNT_BOXES: dict[str, Cell] = {'s': (3, 3, 5), 'l': (5, 5, 9)}

# The turns a mount may take, as matrices whose columns are the images of the mount's axes in the hull's (left-handed:
# +X right, +Y up, +Z forward, so port is -X).
FACINGS: dict[str, Matrix] = {
  'forward': IDENTITY,
  'aft': ((-1, 0, 0), (0, 1, 0), (0, 0, -1)),
  'up': ((1, 0, 0), (0, 0, 1), (0, -1, 0)),
  'down': ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
  'port': ((0, 0, -1), (0, 1, 0), (1, 0, 0)),
  'starboard': ((0, 0, 1), (0, 1, 0), (-1, 0, 0)),
}

# Mount types whose module works along a line out of the hull, which the hull must leave clear (G38).
LINE_TYPES = frozenset({'engine', 'weapon', 'mining', 'sensor'})

# The palette entry a mount's marker is drawn in: magenta, which no hull uses.
MARKER_ENTRY = 15

# Hull palette entries (the module docstring's conventions).
LIGHT = 1
LIGHT_PANEL = 2
HEAVY = 3
HEAVY_EDGE = 4
TRIM = 5
LIGHTS = 6
SIDE = 16

BLACK = PaletteColor(0, 0, 0)
SIDE_COLOR = PaletteColor(60, 120, 200)  # the default side's color; the renderer gives each side its own (G24)


def palette(colors: dict[int, PaletteColor]) -> list[PaletteColor]:
  """Sixteen entries: colors by entry, black elsewhere, and the side's color in entry 16 unless colors sets it."""
  entries = [colors.get(entry, BLACK) for entry in range(1, 17)]
  if SIDE not in colors:
    entries[SIDE - 1] = SIDE_COLOR
  return entries


HULL_PALETTE = palette({
  LIGHT: PaletteColor(150, 155, 160),
  LIGHT_PANEL: PaletteColor(178, 183, 188),
  HEAVY: PaletteColor(88, 93, 103),
  HEAVY_EDGE: PaletteColor(66, 69, 78),
  TRIM: PaletteColor(205, 160, 60),
  LIGHTS: PaletteColor(255, 230, 150, emit=0.6),
  MARKER_ENTRY: PaletteColor(255, 0, 255),
})


def pcg_hash(value: int) -> int:
  """NeuronCore's PcgHash (Hash.h), so that the asteroids' noise is an integer function and the same everywhere."""
  state = (value * 747796405 + 2891336453) & 0xFFFFFFFF
  word = (((state >> ((state >> 28) + 4)) ^ state) * 277803737) & 0xFFFFFFFF
  return ((word >> 22) ^ word) & 0xFFFFFFFF


def rotate(matrix: Matrix, vector: Cell) -> Cell:
  return tuple(sum(matrix[row][column] * vector[column] for column in range(3)) for row in range(3))  # type: ignore[return-value]


def add(first: Cell, second: Cell) -> Cell:
  return (first[0] + second[0], first[1] + second[1], first[2] + second[2])


class Voxels:
  """A sparse grid of palette entries, built up from boxes and carved out again."""

  def __init__(self) -> None:
    self.cells: dict[Cell, int] = {}

  def box(self, lower: Cell, upper: Cell, entry: int) -> None:
    """Fills every cell from lower to upper, upper excluded, with entry."""
    for x in range(lower[0], upper[0]):
      for y in range(lower[1], upper[1]):
        for z in range(lower[2], upper[2]):
          self.cells[(x, y, z)] = entry

  def carve(self, cells) -> None:
    for cell in cells:
      self.cells.pop(cell, None)

  def remove_where(self, predicate) -> None:
    self.carve([cell for cell in self.cells if predicate(cell)])

  def paint_where(self, predicate, entry: int) -> None:
    for cell in self.cells:
      if predicate(cell):
        self.cells[cell] = entry

  def bounds(self) -> tuple[Cell, Cell]:
    """The least cell, and one past the greatest, on each axis."""
    lower = tuple(min(cell[axis] for cell in self.cells) for axis in range(3))
    upper = tuple(max(cell[axis] for cell in self.cells) + 1 for axis in range(3))
    return lower, upper  # type: ignore[return-value]


@dataclass(frozen=True)
class Mount:
  """A place for a module: its hardpoint type, size and label, its box's centre cell in the hull, and its facing."""
  kind: str
  size: str
  label: str
  center: Cell
  facing: str
  inside: bool = False  # the box is carved out of the hull, rather than standing against it

  @property
  def name(self) -> str:
    return f'{self.kind}.{self.size}.{self.label}'

  def cells(self) -> list[Cell]:
    """The cells of its box in the hull's grid."""
    width, height, length = MOUNT_BOXES[self.size]
    matrix = FACINGS[self.facing]
    return [add(self.center, rotate(matrix, (i, j, k)))
            for i in range(-(width // 2), width // 2 + 1)
            for j in range(-(height // 2), height // 2 + 1)
            for k in range(-(length // 2), length // 2 + 1)]

  def direction(self) -> Cell:
    return rotate(FACINGS[self.facing], (0, 0, 1))


@dataclass
class Hull:
  """A design's hull and its mounts, before it becomes a scene."""
  name: str
  voxels: Voxels
  mounts: list[Mount]


def check_mounts(hull: Hull) -> None:
  """Refuses a hull whose mounts break the concept's rules (§5.5, G37, G38), by name, so that a generator mistake fails
  here and never reaches GameData. GameCore's validation checks the same things again, on the imported file."""
  cells = hull.voxels.cells
  lower, upper = hull.voxels.bounds()
  taken: dict[Cell, str] = {}
  commands = [mount for mount in hull.mounts if mount.kind == 'command']
  if len(commands) != 1:
    raise ValueError(f'{hull.name}: {len(commands)} command mounts, not exactly one')
  for mount in hull.mounts:
    box = mount.cells()
    for cell in box:
      if cell in cells:
        raise ValueError(f'{hull.name}: {mount.name}\'s box holds hull voxel {cell}')
      if cell in taken:
        raise ValueError(f'{hull.name}: {mount.name}\'s box overlaps {taken[cell]}\'s at {cell}')
      taken[cell] = mount.name
    neighbors = ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))
    if not any(add(cell, step) in cells for cell in box for step in neighbors):
      raise ValueError(f'{hull.name}: {mount.name}\'s box shares no face with the hull')
    if mount.kind in LINE_TYPES:
      step = mount.direction()
      cell = add(mount.center, tuple(axis * (MOUNT_BOXES[mount.size][2] // 2 + 1) for axis in step))  # type: ignore[arg-type]
      while all(lower[axis] - 1 <= cell[axis] <= upper[axis] for axis in range(3)):
        if cell in cells:
          raise ValueError(f'{hull.name}: {mount.name}\'s line out of the hull meets hull voxel {cell}')
        cell = add(cell, step)


def check_connected(hull: Hull) -> None:
  """Refuses a hull whose voxels are not one piece, face to face (§5.5)."""
  cells = hull.voxels.cells
  start = min(cells)
  seen = {start}
  frontier = [start]
  while frontier:
    cell = frontier.pop()
    for step in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
      neighbor = add(cell, step)
      if neighbor in cells and neighbor not in seen:
        seen.add(neighbor)
        frontier.append(neighbor)
  if len(seen) != len(cells):
    raise ValueError(f'{hull.name}: {len(cells) - len(seen)} voxels are cut off from the rest')


def hull_scene(hull: Hull) -> VoxScene:
  """The hull as a part named hull at the origin, and each mount as a marker model over its box."""
  for mount in hull.mounts:
    if mount.inside:
      hull.voxels.carve(mount.cells())
  check_mounts(hull)
  check_connected(hull)
  lower, upper = hull.voxels.bounds()
  if lower != (0, 0, 0):
    raise ValueError(f'{hull.name}: the hull\'s least cell is {lower}, not the origin')
  size = upper
  models = [VoxModel('hull', size, dict(hull.voxels.cells), (size[0] // 2, size[1] // 2, size[2] // 2))]
  for mount in hull.mounts:
    width, height, length = MOUNT_BOXES[mount.size]
    marker = {(x, y, z): MARKER_ENTRY for x in range(width) for y in range(height) for z in range(length)}
    models.append(VoxModel(f'hull@{mount.name}', (width, height, length), marker, mount.center, FACINGS[mount.facing]))
  return VoxScene(models, HULL_PALETTE)


# The hulls. Each is built in a grid whose least cell is the origin; mounts that face aft or outward stand beyond it.

def gunship() -> Hull:
  """A broad frigate with two mass drivers on its back: the brick of the three frigates."""
  voxels = Voxels()
  voxels.box((0, 0, 0), (9, 5, 27), LIGHT)
  voxels.remove_where(lambda c: c[2] == 24 and c[0] in (0, 8))
  voxels.remove_where(lambda c: c[2] == 25 and c[0] in (0, 1, 7, 8))
  voxels.remove_where(lambda c: c[2] == 26 and not (3 <= c[0] <= 5 and 1 <= c[1] <= 3))
  voxels.box((-2, 1, 7), (0, 3, 19), LIGHT_PANEL)
  voxels.box((9, 1, 7), (11, 3, 19), LIGHT_PANEL)
  voxels.paint_where(lambda c: c[2] >= 20, HEAVY)
  voxels.paint_where(lambda c: 6 <= c[2] <= 19 and 0 <= c[0] <= 8 and c[1] in (0, 4), HEAVY_EDGE)
  voxels.paint_where(lambda c: c[1] == 2 and (c[0] < 0 or c[0] > 8) and 8 <= c[2] <= 17, SIDE)
  voxels.paint_where(lambda c: c[1] == 4 and c[2] == 23 and 3 <= c[0] <= 5, LIGHTS)
  shifted = shift(voxels, (2, 0, 0))
  return Hull('Gunship', shifted, [
    Mount('command', 's', 'core', (6, 2, 12), 'forward', inside=True),
    Mount('reactor', 's', 'main', (6, 2, 4), 'forward', inside=True),
    Mount('engine', 's', 'port', (4, 2, -3), 'aft'),
    Mount('engine', 's', 'starboard', (8, 2, -3), 'aft'),
    Mount('weapon', 's', 'port', (4, 6, 16), 'forward'),
    Mount('weapon', 's', 'starboard', (8, 6, 16), 'forward'),
    Mount('sensor', 's', 'dorsal', (6, 7, 6), 'up'),
  ])


def lancer() -> Hull:
  """A long, narrow frigate with a laser on either flank: the needle."""
  voxels = Voxels()
  voxels.box((0, 0, 0), (7, 5, 36), LIGHT)
  voxels.remove_where(lambda c: c[2] in (33, 34) and not (1 <= c[0] <= 5 and 1 <= c[1] <= 3))
  voxels.remove_where(lambda c: c[2] == 35 and not (2 <= c[0] <= 4 and c[1] == 2))
  voxels.box((3, 5, 0), (4, 8, 7), LIGHT_PANEL)
  voxels.paint_where(lambda c: c[2] >= 27, HEAVY)
  voxels.paint_where(lambda c: 10 <= c[2] <= 20 and c[1] in (0, 4), HEAVY_EDGE)
  voxels.paint_where(lambda c: c[1] == 4 and c[0] == 3 and 21 <= c[2] <= 26, SIDE)
  voxels.paint_where(lambda c: c[1] == 4 and c[2] == 31 and c[0] in (2, 4), LIGHTS)
  return Hull('Lancer', voxels, [
    Mount('command', 's', 'core', (3, 2, 16), 'forward', inside=True),
    Mount('reactor', 's', 'main', (3, 2, 7), 'forward', inside=True),
    Mount('engine', 's', 'port', (1, 2, -3), 'aft'),
    Mount('engine', 's', 'starboard', (5, 2, -3), 'aft'),
    Mount('weapon', 's', 'port', (-2, 2, 26), 'forward'),
    Mount('weapon', 's', 'starboard', (8, 2, 26), 'forward'),
    Mount('sensor', 's', 'dorsal', (3, 7, 22), 'up'),
  ])


def miner() -> Hull:
  """A stubby frigate with a mining laser in its nose and a hold amidships."""
  voxels = Voxels()
  voxels.box((0, 0, 0), (9, 9, 17), LIGHT)
  voxels.remove_where(lambda c: c[2] == 16 and (c[0] in (0, 8) or c[1] in (0, 8)))
  voxels.paint_where(lambda c: c[2] >= 15, HEAVY)
  voxels.paint_where(lambda c: c[1] == 8 and 6 <= c[2] <= 12, TRIM)
  voxels.paint_where(lambda c: c[1] == 4 and c[0] in (0, 8) and 2 <= c[2] <= 13, SIDE)
  voxels.paint_where(lambda c: c[1] == 8 and c[2] == 14 and c[0] in (2, 6), LIGHTS)
  return Hull('Miner', voxels, [
    Mount('command', 's', 'core', (4, 2, 9), 'forward', inside=True),
    Mount('reactor', 's', 'main', (4, 2, 3), 'forward', inside=True),
    Mount('cargo', 's', 'hold', (4, 6, 9), 'forward', inside=True),
    Mount('mining', 's', 'front', (4, 4, 19), 'forward'),
    Mount('engine', 's', 'port', (2, 4, -3), 'aft'),
    Mount('engine', 's', 'starboard', (6, 4, -3), 'aft'),
    Mount('sensor', 's', 'dorsal', (4, 11, 3), 'up'),
  ])


def cruiser() -> Hull:
  """A capital ship in heavy armor: mass drivers on its back, a laser on each beam, and four engines."""
  voxels = Voxels()
  voxels.box((0, 0, 0), (17, 9, 61), LIGHT)
  for z, inset in ((56, 1), (57, 2), (58, 3), (59, 4), (60, 5)):
    voxels.remove_where(lambda c, z=z, inset=inset: c[2] == z and (c[0] < inset or c[0] > 16 - inset or c[1] > 8 - inset // 2))
  voxels.box((5, 9, 8), (12, 13, 18), LIGHT_PANEL)
  voxels.paint_where(lambda c: c[2] >= 46, HEAVY)
  voxels.paint_where(lambda c: (c[0] <= 3 or c[0] >= 13 or c[1] in (0, 8)) and 4 <= c[2] <= 45, HEAVY)
  voxels.paint_where(lambda c: (c[0] <= 1 or c[0] >= 15) and c[1] in (0, 8) and 4 <= c[2] <= 45, HEAVY_EDGE)
  voxels.paint_where(lambda c: c[1] == 4 and c[0] in (0, 16) and 20 <= c[2] <= 40, SIDE)
  voxels.paint_where(lambda c: c[1] == 12 and c[2] == 17 and 6 <= c[0] <= 10 and c[0] % 2 == 0, LIGHTS)
  return Hull('Cruiser', voxels, [
    Mount('command', 's', 'core', (8, 4, 30), 'forward', inside=True),
    Mount('reactor', 'l', 'main', (8, 4, 16), 'forward', inside=True),
    Mount('engine', 's', 'port_dorsal', (4, 6, -3), 'aft'),
    Mount('engine', 's', 'port_ventral', (4, 2, -3), 'aft'),
    Mount('engine', 's', 'starboard_dorsal', (12, 6, -3), 'aft'),
    Mount('engine', 's', 'starboard_ventral', (12, 2, -3), 'aft'),
    Mount('weapon', 's', 'dorsal_fore', (12, 10, 44), 'forward'),
    Mount('weapon', 's', 'dorsal_aft', (4, 10, 36), 'forward'),
    Mount('weapon', 's', 'port', (-3, 4, 30), 'port'),
    Mount('weapon', 's', 'starboard', (19, 4, 30), 'starboard'),
    Mount('sensor', 's', 'dorsal', (8, 15, 12), 'up'),
  ])


def station_core() -> Hull:
  """The skirmish's station core (G48): the shipyard, lab, refinery and sensor array around a deep command module, and a
  turret on each edge of its roof."""
  voxels = Voxels()
  voxels.box((0, 0, 0), (41, 17, 41), LIGHT)
  voxels.box((16, 17, 16), (25, 25, 25), LIGHT_PANEL)
  voxels.paint_where(lambda c: c[1] <= 16 and (c[0] <= 1 or c[0] >= 39 or c[2] <= 1 or c[2] >= 39), HEAVY)
  voxels.paint_where(lambda c: c[1] in (0, 16) and (c[0] in (0, 40) or c[2] in (0, 40)), HEAVY_EDGE)
  voxels.paint_where(lambda c: c[1] == 8 and (c[0] in (0, 40) or c[2] in (0, 40)) and (c[0] + c[2]) % 4 == 0, SIDE)
  voxels.paint_where(lambda c: c[1] == 22 and (c[0] in (16, 24) or c[2] in (16, 24)) and (c[0] + c[2]) % 2 == 0, LIGHTS)
  return Hull('StationCore', voxels, [
    Mount('command', 's', 'core', (20, 8, 20), 'forward', inside=True),
    Mount('reactor', 'l', 'main', (20, 8, 9), 'forward', inside=True),
    Mount('shipyard', 'l', 'dock', (20, 8, 45), 'forward'),
    Mount('lab', 'l', 'main', (-5, 8, 20), 'port'),
    Mount('refinery', 'l', 'main', (45, 8, 20), 'starboard'),
    Mount('sensor', 'l', 'array', (20, 29, 20), 'up'),
    Mount('weapon', 's', 'north', (20, 18, 37), 'forward'),
    Mount('weapon', 's', 'south', (20, 18, 3), 'aft'),
    Mount('weapon', 's', 'west', (3, 18, 20), 'port'),
    Mount('weapon', 's', 'east', (37, 18, 20), 'starboard'),
  ])


def shift(voxels: Voxels, offset: Cell) -> Voxels:
  """voxels moved by offset, so that a hull with wings or fins reaching below 0 starts at the origin."""
  moved = Voxels()
  moved.cells = {add(cell, offset): entry for cell, entry in voxels.cells.items()}
  return moved


HULLS = (gunship, lancer, miner, cruiser, station_core)


# The modules. Each fills at most its size's box, faces +Z, and is centered on its box, so that its centre voxel lands on
# its mount's centre.

@dataclass(frozen=True)
class Module:
  name: str
  size: Cell
  voxels: dict[Cell, int]
  colors: dict[int, PaletteColor]

  def scene(self) -> VoxScene:
    return VoxScene([VoxModel('', self.size, self.voxels, (self.size[0] // 2, self.size[1] // 2, self.size[2] // 2))],
                    palette(self.colors))


def solid(size: Cell, entry: int) -> dict[Cell, int]:
  return {(x, y, z): entry for x in range(size[0]) for y in range(size[1]) for z in range(size[2])}


def small_modules() -> list[Module]:
  body = PaletteColor(130, 132, 140)
  dark = PaletteColor(66, 68, 76)
  modules = []

  command = solid((3, 3, 3), 1)
  command[(1, 1, 2)] = 2
  command[(1, 2, 1)] = SIDE
  modules.append(Module('CommandModule', (3, 3, 3), command,
                        {1: PaletteColor(205, 205, 212), 2: PaletteColor(255, 215, 90, emit=0.8)}))

  reactor = solid((3, 3, 5), 1)
  for cell in [(x, y, 2) for x in range(3) for y in range(3) if (x, y) != (1, 1)]:
    reactor[cell] = 2
  reactor[(1, 2, 4)] = SIDE
  modules.append(Module('Reactor', (3, 3, 5), reactor, {1: body, 2: PaletteColor(90, 200, 255, emit=0.9)}))

  thruster = solid((3, 3, 3), 1)
  thruster.update({(x, y, 3): 2 for x in range(3) for y in range(3) if (x, y) != (1, 1)})
  thruster[(1, 1, 3)] = 3
  thruster.update({(x, y, 4): 2 for x in range(3) for y in range(3) if (x, y) != (1, 1)})
  thruster[(1, 1, 4)] = 3
  thruster[(1, 2, 0)] = SIDE
  modules.append(Module('Thruster', (3, 3, 5), thruster, {1: body, 2: dark, 3: PaletteColor(255, 140, 40, emit=1.0)}))

  mass_driver = solid((3, 3, 2), 1)
  mass_driver.update({(1, 1, z): 2 for z in range(2, 5)})
  mass_driver[(1, 2, 1)] = SIDE
  modules.append(Module('MassDriver', (3, 3, 5), mass_driver, {1: body, 2: dark}))

  laser = solid((3, 3, 2), 1)
  laser.update({(1, 1, z): 2 for z in range(2, 4)})
  laser[(1, 1, 4)] = 3
  laser[(1, 2, 1)] = SIDE
  modules.append(Module('Laser', (3, 3, 5), laser, {1: body, 2: dark, 3: PaletteColor(255, 70, 70, emit=1.0)}))

  mining = solid((3, 3, 2), 1)
  mining.update({(1, 1, z): 2 for z in range(2, 4)})
  mining[(1, 1, 4)] = 3
  mining[(1, 2, 1)] = SIDE
  modules.append(Module('MiningLaser', (3, 3, 5), mining,
                        {1: PaletteColor(170, 150, 90), 2: dark, 3: PaletteColor(120, 255, 120, emit=1.0)}))

  cargo = solid((3, 3, 5), 1)
  cargo.update({(x, y, z): 2 for x in range(3) for y in range(3) for z in (0, 2, 4) if (x, y) != (1, 1)})
  cargo[(1, 2, 2)] = SIDE
  modules.append(Module('CargoHold', (3, 3, 5), cargo, {1: PaletteColor(160, 130, 80), 2: PaletteColor(110, 90, 60)}))

  sensor = {(1, 1, z): 1 for z in range(3)}
  sensor.update({(x, y, 3): 2 for x in range(3) for y in range(3) if (x, y) != (1, 1)})
  sensor[(1, 1, 3)] = 2
  sensor[(1, 1, 4)] = 3
  sensor[(1, 1, 0)] = SIDE
  modules.append(Module('Sensor', (3, 3, 5), sensor,
                        {1: PaletteColor(170, 170, 176), 2: PaletteColor(220, 220, 230), 3: PaletteColor(120, 220, 255, emit=0.8)}))
  return modules


def large_modules() -> list[Module]:
  body = PaletteColor(130, 132, 140)
  dark = PaletteColor(66, 68, 76)
  modules = []

  reactor = solid((5, 5, 9), 1)
  reactor.update({(x, y, z): 2 for x in range(5) for y in range(5) for z in (3, 5) if x in (0, 4) or y in (0, 4)})
  reactor[(2, 4, 8)] = SIDE
  modules.append(Module('ReactorLarge', (5, 5, 9), reactor, {1: body, 2: PaletteColor(90, 200, 255, emit=0.9)}))

  shipyard = {(x, y, z): 1 for x in range(5) for y in range(5) for z in range(9)
              if sum((x in (0, 4), y in (0, 4), z in (0, 8))) >= 2}
  shipyard.update({(x, y, 8): 2 for x in (0, 4) for y in (0, 4)})
  shipyard[(2, 4, 0)] = SIDE
  modules.append(Module('Shipyard', (5, 5, 9), shipyard, {1: PaletteColor(140, 140, 150), 2: PaletteColor(255, 200, 80, emit=0.8)}))

  lab = solid((5, 5, 9), 1)
  lab.update({(x, 4, z): 2 for x in (1, 3) for z in range(1, 8, 2)})
  lab[(2, 4, 0)] = SIDE
  modules.append(Module('Lab', (5, 5, 9), lab, {1: PaletteColor(185, 185, 195), 2: PaletteColor(150, 110, 255, emit=0.8)}))

  refinery = solid((5, 5, 9), 1)
  refinery.update({(x, y, z): 2 for x in range(5) for y in range(5) for z in (2, 6) if x in (0, 4) or y in (0, 4)})
  refinery[(2, 2, 8)] = 3
  refinery[(2, 4, 0)] = SIDE
  modules.append(Module('Refinery', (5, 5, 9), refinery,
                        {1: PaletteColor(150, 120, 90), 2: PaletteColor(110, 88, 66), 3: PaletteColor(255, 120, 40, emit=0.9)}))

  array = {(2, 2, z): 1 for z in range(6)}
  array.update({(x, y, z): 2 for x in range(5) for y in range(5) for z in (6, 7) if (x - 2) ** 2 + (y - 2) ** 2 >= 3 - z + 6})
  array[(2, 2, 8)] = 3
  array[(2, 2, 0)] = SIDE
  modules.append(Module('SensorArray', (5, 5, 9), array,
                        {1: PaletteColor(170, 170, 176), 2: PaletteColor(220, 220, 230), 3: PaletteColor(120, 220, 255, emit=0.8)}))
  return modules


def largest_piece(voxels: dict[Cell, int]) -> dict[Cell, int]:
  """The largest set of voxels joined face to face, so that a bump near a corner never floats free of its rock. Ties go
  to the piece holding the least cell, so that the choice never depends on iteration order."""
  unseen = set(voxels)
  best: set[Cell] = set()
  for start in sorted(voxels):
    if start not in unseen:
      continue
    piece = {start}
    unseen.discard(start)
    frontier = [start]
    while frontier:
      cell = frontier.pop()
      for step in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
        neighbor = add(cell, step)
        if neighbor in unseen:
          unseen.discard(neighbor)
          piece.add(neighbor)
          frontier.append(neighbor)
    if len(piece) > len(best):
      best = piece
  return {cell: voxels[cell] for cell in best}


def asteroid(name: str, radii: tuple[float, float, float], seed: int) -> VoxScene:
  """A lumpy rock: an ellipsoid, with bumps and craters placed by PcgHash of seed, and flecks of lighter stone."""
  def noise(index: int) -> float:
    return pcg_hash(seed * 1000 + index) / 0xFFFFFFFF

  size = tuple(2 * int(radius) + 7 for radius in radii)
  center = tuple(extent / 2 for extent in size)
  bumps = [(tuple(center[a] + (noise(3 * b + a) - 0.5) * 2 * radii[a] for a in range(3)), 2.0 + 3.0 * noise(100 + b))
           for b in range(6)]
  craters = [(tuple(center[a] + (noise(200 + 3 * b + a) - 0.5) * 2.2 * radii[a] for a in range(3)), 2.0 + 2.5 * noise(300 + b))
             for b in range(3)]
  voxels: dict[Cell, int] = {}
  for x in range(size[0]):
    for y in range(size[1]):
      for z in range(size[2]):
        point = (x + 0.5, y + 0.5, z + 0.5)
        inside = sum(((point[a] - center[a]) / radii[a]) ** 2 for a in range(3)) <= 1.0
        inside = inside or any(sum((point[a] - c[a]) ** 2 for a in range(3)) <= r * r for c, r in bumps)
        inside = inside and not any(sum((point[a] - c[a]) ** 2 for a in range(3)) <= r * r for c, r in craters)
        if inside:
          fleck = pcg_hash(seed * 7919 + x * 73856093 ^ y * 19349663 ^ z * 83492791) % 10 == 0
          voxels[(x, y, z)] = 3 if fleck else (2 if y < center[1] - 1 else 1)
  voxels = largest_piece(voxels)
  lower = tuple(min(cell[a] for cell in voxels) for a in range(3))
  voxels = {add(cell, (-lower[0], -lower[1], -lower[2])): entry for cell, entry in voxels.items()}
  upper = tuple(max(cell[a] for cell in voxels) + 1 for a in range(3))
  colors = {1: PaletteColor(112, 102, 92), 2: PaletteColor(88, 80, 73), 3: PaletteColor(150, 138, 112), SIDE: BLACK}
  return VoxScene([VoxModel('', upper, voxels, (upper[0] // 2, upper[1] // 2, upper[2] // 2))], palette(colors))


ASTEROIDS = (('AsteroidA', (8.0, 6.0, 7.0), 1), ('AsteroidB', (12.0, 9.0, 10.0), 2), ('AsteroidC', (6.0, 5.0, 9.0), 3))


def build_all() -> dict[str, bytes]:
  """Every generated file's bytes, by its name without the extension."""
  files: dict[str, bytes] = {}
  for build in HULLS:
    hull = build()
    files[hull.name] = serialize_vox(hull_scene(hull))
  for module in small_modules() + large_modules():
    files[module.name] = serialize_vox(module.scene())
  for name, radii, seed in ASTEROIDS:
    files[name] = serialize_vox(asteroid(name, radii, seed))
  return files


def main(arguments: list[str]) -> int:
  parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
  parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[2] / 'GameData',
                      help='the folder to write the .vox files into (default: the repository\'s GameData)')
  parser.add_argument('--check', action='store_true', help='write nothing; exit 1 if any file there differs')
  options = parser.parse_args(arguments)
  stale = []
  for name, data in build_all().items():
    path = options.output / f'{name}.vox'
    if options.check:
      if not path.exists() or path.read_bytes() != data:
        stale.append(path.name)
    else:
      path.write_bytes(data)
  if stale:
    print('stale: ' + ', '.join(stale))
    return 1
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
