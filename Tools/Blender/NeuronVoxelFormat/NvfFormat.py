"""The Neuron Voxel Format, read and written: the Python twin of NeuronCore/NvfModel.cpp.

Design/NeuronVoxelFormat.md §4 is the specification, and it has two implementations (AGENTS.md R20): this module,
for the Blender extension, and NvfModel.cpp, for the engine and NvfImport. Tools/Golden/Golden.nvf holds them to the
same bytes, and both check a file in the order Design/ADR/ADR-019 fixes, so that a file with two faults gets the same
name from each.

  model = load_nvf_model('Ship.nvf')     # an NvfModel, or NvfError naming the first fault
  data = serialize_nvf_model(model)      # the bytes, or NvfError for a model the reader would refuse
  save_nvf_model(model, 'Ship.nvf')      # through a temporary file, renamed over the target

It imports nothing from Blender, so the Linux CI job tests it with the system Python; it needs only Python 3.10+, and
Blender 4.2 bundles 3.11. It works in NVF's axes, Direct3D's: left-handed, +Y up, +Z forward (§4.1). The extension's
import and export operators swap y and z on the way in and on the way out (§7); nothing here does.
"""

import enum
import math
import os
import struct
from dataclasses import dataclass, field
from pathlib import Path

VERSION_MAJOR = 1
VERSION_MINOR = 0

# What a part's parent index holds for part 0, the root (§4.3).
NO_PARENT = 0xFFFFFFFF

# Limits, so that a bad file fails fast rather than allocating (§4.4), and the bound on a part's origin (ADR-019).
MAX_PARTS = 1024
MAX_HARDPOINTS = 4096
MAX_STRING_BYTES = 64 * 1024
MAX_PART_EXTENT = 256
MAX_PART_ORIGIN = 1 << 21
MAX_SEGMENT_CHARS = 31
PALETTE_ENTRY_COUNT = 16

# 1.0e-4f, the tolerance IsUnitRotation takes from Quaternion.h, as single precision holds it. The length itself is
# computed in double precision here, and in single there: the two can disagree only about a quaternion within rounding
# of the tolerance, which no tool writes (ADR-019).
UNIT_ROTATION_TOLERANCE = struct.unpack('<f', struct.pack('<f', 1.0e-4))[0]

# NeuronCore's NvfError enumerators, in their order (§4.4). The tests hold this tuple to NvfModel.h's enum.
ERROR_NAMES = (
  'FileNotFound',
  'ReadFailed',
  'WriteFailed',
  'NotAnNvfFile',
  'UnsupportedVersion',
  'Truncated',
  'MalformedChunk',
  'MissingChunk',
  'ChunkOutOfOrder',
  'BadString',
  'DuplicateName',
  'BadPartTree',
  'PartTooLarge',
  'TranslationOutOfRange',
  'BadVoxelRange',
  'VoxelOutOfBounds',
  'DuplicateVoxel',
  'ReservedBitsSet',
  'BadHardpointPart',
  'NotFinite',
  'NotUnitRotation',
)

MAGIC = b'NVF '
ALIGNMENT_BYTES = 16


# The five chunks of version 1.0, in the order they must appear (§4.2).
class KnownChunk(enum.IntEnum):
  Strings = 0
  Palette = 1
  Parts = 2
  Voxels = 3
  Hardpoints = 4


KNOWN_CHUNK_IDS = (b'STRS', b'PALT', b'PART', b'VOXL', b'HPNT')

# The records as they lie in the file, little-endian and unpadded: §4.2's and §4.3's tables, field by field.
FILE_HEADER = struct.Struct('<4sHHII')          # magic, versionMajor, versionMinor, chunkCount, reserved
CHUNK_HEADER = struct.Struct('<4sIII')          # id, sizeBytes, elementCount, reserved
PALETTE_RECORD = struct.Struct('<4BIff')        # red, green, blue, alpha, flags, emit, flux
PART_RECORD = struct.Struct('<II3HH3i3fII')     # nameOffset, parentIndex, sizeVoxels, flags, translation, pivot,
                                                # firstVoxel, voxelCount
HARDPOINT_RECORD = struct.Struct('<III3f4f2I')   # nameOffset, partIndex, flags, position, rotation, reserved
VOXEL_RECORD_BYTES = 4

PALETTE_EMISSIVE = 1
PART_PIVOT_AUTHORED = 1
HARDPOINT_FROM_VOX = 1

# A voxel record's bits 28-31, which R14 keeps zero, and its position, bits 0-23.
RESERVED_RECORD_BITS = 0xF0000000
POSITION_BITS = 0x00FFFFFF

# The reserved hardpoint type: `pivot` names a part's pivot marker in the .vox, never a hardpoint (§4.1, §5).
PIVOT_TYPE = 'pivot'

SEGMENT_CHARS = frozenset('abcdefghijklmnopqrstuvwxyz0123456789_')


class NvfError(Exception):
  """A refusal, named as NeuronCore's NvfError names it: `name` is one of ERROR_NAMES (§4.4)."""

  def __init__(self, name):
    if name not in ERROR_NAMES:
      raise ValueError(f'{name!r} is not one of NvfError\'s names')
    super().__init__(name)
    self.name = name


@dataclass
class PaletteEntry:
  """One of the palette's 16 colors: the file's sRGB bytes and alpha, and the material the .vox gave it (§4.3)."""
  red: int
  green: int
  blue: int
  alpha: int
  emissive: bool = False
  emit: float = 0.0
  flux: float = 0.0


@dataclass
class NvfPart:
  """One rigid part: a voxel grid, placed by a translation in its parent's space and never turned at rest (N2).

  Part space's origin is the minimum corner of voxel (0, 0, 0), with model space's axes (§4.1).
  """
  path: str                                  # segments joined by '/', its parent's path plus one, e.g. hull/turret
  parent: int                                # NO_PARENT for part 0, otherwise a part before this one
  size: tuple[int, int, int]                 # in voxels, 1 to 256 on each axis
  translation: tuple[int, int, int]          # this part's space in its parent's; part 0's in model space
  pivot: tuple[float, float, float]          # in part space: where the game turns the part about
  pivot_authored: bool                       # the pivot was set in Blender, and a re-import keeps it (§6.2)
  first_voxel: int                           # this part's records in NvfModel.records
  voxel_count: int                           # at least 1


@dataclass
class NvfHardpoint:
  """A named point with a free orientation where something is mounted (N3).

  Its frame's +Z points out of the mount, +Y is up and +X right (§4.1).
  """
  name: str                                  # two or more segments joined by '.', the first its type
  part: int                                  # the part it is mounted on
  position: tuple[float, float, float]       # in its part's space, in voxels
  rotation: tuple[float, float, float, float]  # x, y, z, w: from its frame to its part's space, unit, w >= 0
  from_vox: bool                             # it came from a marker in the .vox, which a re-import replaces (§6.2)


@dataclass
class NvfModel:
  """A whole model, as the reader returns it and the writer takes it."""
  palette: list[PaletteEntry] = field(default_factory=list)   # 16 entries
  parts: list[NvfPart] = field(default_factory=list)          # part 0 first, every parent before its children
  records: list[int] = field(default_factory=list)            # R14, part after part
  hardpoints: list[NvfHardpoint] = field(default_factory=list)  # the writer stores them in name order
  # The ids of the chunks the reader skipped, in file order. A tool that rewrites a file refuses a model that has any,
  # since a newer chunk may refer to parts or hardpoints by index (§4.6).
  unknown_chunks: list[bytes] = field(default_factory=list)


def as_single(value):
  """The value single precision holds for `value`: rounded to nearest, and a finite value beyond its range infinite.

  What the file stores for a float, so that a caller can compare a value with the one a written file will hold.
  """
  try:
    return struct.unpack('<f', struct.pack('<f', value))[0]
  except OverflowError:
    return math.copysign(math.inf, value)


def pack_voxel_record(x, y, z, color):
  """R14's record: x, y and z in bits 0-23, the color, the palette entry minus one, in bits 24-27."""
  return x | (y << 8) | (z << 16) | ((color & 0xF) << 24)


def unpack_voxel_record(record):
  """The (x, y, z, color) a record packs."""
  return record & 0xFF, (record >> 8) & 0xFF, (record >> 16) & 0xFF, (record >> 24) & 0xF


def hardpoint_type(hardpoint):
  """A hardpoint's type: its name's first segment (§4.3). It is not stored apart from the name, so the two agree."""
  return hardpoint.name.split('.', 1)[0]


def is_segment(segment):
  """One segment of a name: 1 to 31 of [a-z0-9_] (§4.1)."""
  return 1 <= len(segment) <= MAX_SEGMENT_CHARS and all(char in SEGMENT_CHARS for char in segment)


def is_nvf_part_path(path):
  """Whether `path` is a part path: one or more segments joined by '/' (§4.1)."""
  return all(is_segment(segment) for segment in path.split('/'))


def is_nvf_hardpoint_name(name):
  """Whether `name` is a hardpoint name: two or more segments joined by '.', the first not `pivot` (§4.1)."""
  segments = name.split('.')
  return len(segments) >= 2 and all(is_segment(segment) for segment in segments) and segments[0] != PIVOT_TYPE


def is_unit_rotation(rotation):
  """What NeuronCore's IsUnitRotation accepts: of unit length within the tolerance, and with w >= 0 (§4.3)."""
  x, y, z, w = rotation
  return abs(math.sqrt(x * x + y * y + z * z + w * w) - 1.0) <= UNIT_ROTATION_TOLERANCE and w >= 0.0


def padded(size_bytes):
  return (size_bytes + ALIGNMENT_BYTES - 1) // ALIGNMENT_BYTES * ALIGNMENT_BYTES


def holds_records(chunk, record_bytes):
  """Whether a chunk's content is its elementCount records of `record_bytes`."""
  return len(chunk.content) == chunk.element_count * record_bytes


def all_finite(values):
  return all(math.isfinite(value) for value in values)


@dataclass
class ChunkSpan:
  id: bytes
  element_count: int
  content: bytes                             # sizeBytes long, the padding excluded


def frame_chunks(data):
  """Frames the file: the header, then chunkCount chunks, each a header, its content and zero padding to 16 bytes, and
  nothing after the last. Raises the first fault, in file order (ADR-019, steps 1 and 2)."""
  if len(data) < len(MAGIC) or data[:len(MAGIC)] != MAGIC:
    raise NvfError('NotAnNvfFile')
  if len(data) < FILE_HEADER.size:
    raise NvfError('Truncated')
  _, version_major, _, chunk_count, reserved = FILE_HEADER.unpack_from(data, 0)
  if version_major != VERSION_MAJOR:
    raise NvfError('UnsupportedVersion')
  if reserved != 0:
    raise NvfError('MalformedChunk')

  chunks = []
  offset = FILE_HEADER.size
  for _ in range(chunk_count):
    if len(data) - offset < CHUNK_HEADER.size:
      raise NvfError('Truncated')
    chunk_id, size_bytes, element_count, chunk_reserved = CHUNK_HEADER.unpack_from(data, offset)
    offset += CHUNK_HEADER.size
    if len(data) - offset < padded(size_bytes):
      raise NvfError('Truncated')
    if chunk_reserved != 0:
      raise NvfError('MalformedChunk')
    if any(data[offset + size_bytes:offset + padded(size_bytes)]):
      raise NvfError('MalformedChunk')
    chunks.append(ChunkSpan(chunk_id, element_count, data[offset:offset + size_bytes]))
    offset += padded(size_bytes)
  if offset != len(data):
    raise NvfError('MalformedChunk')
  return chunks


def string_at(strings, offset):
  """The string at `offset` in STRS, whose content ends with a NUL: the offset must be a string's first byte."""
  if offset >= len(strings) or (offset > 0 and strings[offset - 1] != 0):
    return None
  return strings[offset:strings.index(0, offset)].decode('utf-8')


def parse_nvf_model(data):
  """Reads and validates a whole file, given as bytes, and returns its NvfModel.

  Accepts any minor version of major version 1 and skips chunks it does not know, wherever they lie. Refuses everything
  else §4.4 names by raising NvfError, checking in the order ADR-019 fixes and reporting the first fault.
  """
  data = bytes(data)
  chunks = frame_chunks(data)

  # The five known chunks, each once and in order; any other chunk is skipped, wherever it lies (§4.2, §4.6).
  model = NvfModel()
  known = [None] * len(KNOWN_CHUNK_IDS)
  next_index = 0
  for chunk in chunks:
    if chunk.id not in KNOWN_CHUNK_IDS:
      model.unknown_chunks.append(chunk.id)
      continue
    index = KNOWN_CHUNK_IDS.index(chunk.id)
    if index < next_index:
      raise NvfError('ChunkOutOfOrder')
    known[index] = chunk
    next_index = index + 1
  if any(chunk is None for chunk in known):
    raise NvfError('MissingChunk')

  # STRS: NUL-terminated UTF-8. Python's strict decoder draws the line the Unicode Standard's Table 3-7 draws, as
  # NvfModel.cpp's IsUtf8 does: no overlong form, no surrogate, nothing above U+10FFFF.
  string_chunk = known[KnownChunk.Strings]
  if string_chunk.element_count != len(string_chunk.content) or len(string_chunk.content) > MAX_STRING_BYTES:
    raise NvfError('MalformedChunk')
  strings = string_chunk.content
  if strings and strings[-1] != 0:
    raise NvfError('BadString')
  try:
    strings.decode('utf-8')
  except UnicodeDecodeError:
    raise NvfError('BadString') from None

  # PALT: sixteen entries.
  palette_chunk = known[KnownChunk.Palette]
  if palette_chunk.element_count != PALETTE_ENTRY_COUNT or not holds_records(palette_chunk, PALETTE_RECORD.size):
    raise NvfError('MalformedChunk')
  for red, green, blue, alpha, flags, emit, flux in PALETTE_RECORD.iter_unpack(palette_chunk.content):
    if flags & ~PALETTE_EMISSIVE:
      raise NvfError('MalformedChunk')
    if not all_finite((emit, flux)):
      raise NvfError('NotFinite')
    model.palette.append(PaletteEntry(red, green, blue, alpha, bool(flags & PALETTE_EMISSIVE), emit, flux))

  # PART: a tree whose parents come first, each part's voxels following the last part's.
  part_chunk = known[KnownChunk.Parts]
  if not holds_records(part_chunk, PART_RECORD.size) or part_chunk.element_count > MAX_PARTS:
    raise NvfError('MalformedChunk')
  if part_chunk.element_count == 0:
    raise NvfError('BadPartTree')
  origins = []
  paths = set()
  next_voxel = 0
  for i, record in enumerate(PART_RECORD.iter_unpack(part_chunk.content)):
    name_offset, parent_index = record[0:2]
    size, flags, translation, pivot = record[2:5], record[5], record[6:9], record[9:12]
    first_voxel, voxel_count = record[12:14]
    path = string_at(strings, name_offset)
    if path is None or not is_nvf_part_path(path):
      raise NvfError('BadString')
    last_slash = path.rfind('/')
    if i == 0:
      if parent_index != NO_PARENT or last_slash != -1:
        raise NvfError('BadPartTree')
    elif parent_index >= i or last_slash == -1 or path[:last_slash] != model.parts[parent_index].path:
      raise NvfError('BadPartTree')
    if path in paths:
      raise NvfError('DuplicateName')
    paths.add(path)
    if any(extent == 0 for extent in size):
      raise NvfError('MalformedChunk')
    if any(extent > MAX_PART_EXTENT for extent in size):
      raise NvfError('PartTooLarge')
    if flags & ~PART_PIVOT_AUTHORED:
      raise NvfError('MalformedChunk')
    origin = tuple((0 if i == 0 else origins[parent_index][axis]) + translation[axis] for axis in range(3))
    if any(value < -MAX_PART_ORIGIN or value > MAX_PART_ORIGIN for value in origin):
      raise NvfError('TranslationOutOfRange')
    origins.append(origin)
    if not all_finite(pivot):
      raise NvfError('NotFinite')
    if first_voxel != next_voxel or voxel_count == 0:
      raise NvfError('BadVoxelRange')
    next_voxel += voxel_count
    model.parts.append(NvfPart(path, parent_index, size, translation, pivot, bool(flags & PART_PIVOT_AUTHORED),
                               first_voxel, voxel_count))

  # VOXL: every part's records, inside its size, no position twice within a part.
  voxel_chunk = known[KnownChunk.Voxels]
  if not holds_records(voxel_chunk, VOXEL_RECORD_BYTES):
    raise NvfError('MalformedChunk')
  if voxel_chunk.element_count != next_voxel:
    raise NvfError('BadVoxelRange')
  model.records = list(struct.unpack(f'<{voxel_chunk.element_count}I', voxel_chunk.content))
  for part in model.parts:
    records = model.records[part.first_voxel:part.first_voxel + part.voxel_count]
    if any(record & RESERVED_RECORD_BITS for record in records):
      raise NvfError('ReservedBitsSet')
    width, height, depth = part.size
    if any((record & 0xFF) >= width or ((record >> 8) & 0xFF) >= height or ((record >> 16) & 0xFF) >= depth
           for record in records):
      raise NvfError('VoxelOutOfBounds')
    if len({record & POSITION_BITS for record in records}) != len(records):
      raise NvfError('DuplicateVoxel')

  # HPNT: named points on parts.
  hardpoint_chunk = known[KnownChunk.Hardpoints]
  if not holds_records(hardpoint_chunk, HARDPOINT_RECORD.size) or hardpoint_chunk.element_count > MAX_HARDPOINTS:
    raise NvfError('MalformedChunk')
  names = set()
  for record in HARDPOINT_RECORD.iter_unpack(hardpoint_chunk.content):
    name_offset, part_index, flags = record[0:3]
    position, rotation, reserved = record[3:6], record[6:10], record[10:12]
    name = string_at(strings, name_offset)
    if name is None or not is_nvf_hardpoint_name(name):
      raise NvfError('BadString')
    if name in names:
      raise NvfError('DuplicateName')
    names.add(name)
    if part_index >= len(model.parts):
      raise NvfError('BadHardpointPart')
    if flags & ~HARDPOINT_FROM_VOX or any(reserved):
      raise NvfError('MalformedChunk')
    if not all_finite(position) or not all_finite(rotation):
      raise NvfError('NotFinite')
    if not is_unit_rotation(rotation):
      raise NvfError('NotUnitRotation')
    model.hardpoints.append(NvfHardpoint(name, part_index, position, rotation, bool(flags & HARDPOINT_FROM_VOX)))
  return model


def load_nvf_model(path):
  """Reads and validates the file at `path`. A file that cannot be opened is FileNotFound, as NvfModel.cpp has it."""
  try:
    file = open(path, 'rb')  # pylint: disable=consider-using-with
  except OSError as error:
    raise NvfError('FileNotFound') from error
  with file:
    try:
      data = file.read()
    except OSError as error:
      raise NvfError('ReadFailed') from error
  return parse_nvf_model(data)


def append_chunk(data, chunk_id, element_count, content):
  data += CHUNK_HEADER.pack(chunk_id, len(content), element_count, 0)
  data += content
  data += bytes(padded(len(data)) - len(data))


def serialize_nvf_model(model):
  """The bytes of `model`, as version 1.0 lays them out, in the order NvfModel.cpp's SerializeNvfModel writes them.

  The five chunks in order, hardpoints in name order, and strings in first-use order, so that every writer produces the
  same bytes for the same model (§4.3). Floats are stored as as_single rounds them. Raises the reader's NvfError for a
  model the reader would refuse, so that nothing it writes fails to read back, and ignores unknown_chunks. A value its
  record's field cannot hold at all, such as a negative index, which C++'s types rule out, raises struct.error.
  """
  # A size the file's 16 bits cannot hold would wrap as it narrows; refuse it first, as the reader would refuse the
  # wider value.
  for part in model.parts:
    for extent in part.size:
      if extent < 1:
        raise NvfError('MalformedChunk')
      if extent > MAX_PART_EXTENT:
        raise NvfError('PartTooLarge')
  if (len(model.parts) > MAX_PARTS or len(model.hardpoints) > MAX_HARDPOINTS
      or len(model.records) > 0xFFFFFFFF // VOXEL_RECORD_BYTES):
    raise NvfError('MalformedChunk')

  # Names as the file stores them. A string no UTF-8 encoder would write keeps its surrogates, so that the reader, not
  # the encoder, refuses it, in its place in the order of checking.
  def encoded(name):
    return name.encode('utf-8', 'surrogatepass')

  # Hardpoints in name order, by the bytes of their names, so that whichever tool wrote the table, the bytes are the
  # same (§4.3, ADR-019). The sort is stable, as C++'s is.
  hardpoints = sorted(model.hardpoints, key=lambda hardpoint: encoded(hardpoint.name))

  # Strings in first-use order, each once: part paths in part order, then hardpoint names in hardpoint order.
  strings = bytearray()
  offsets = {}

  def offset_of(name):
    name = encoded(name)
    if name not in offsets:
      offsets[name] = len(strings)
      strings.extend(name + b'\0')
    return offsets[name]

  parts = b''.join(
    PART_RECORD.pack(offset_of(part.path), part.parent, *part.size, PART_PIVOT_AUTHORED if part.pivot_authored else 0,
                     *part.translation, *(as_single(value) for value in part.pivot), part.first_voxel,
                     part.voxel_count) for part in model.parts)
  hardpoint_bytes = b''.join(
    HARDPOINT_RECORD.pack(offset_of(hardpoint.name), hardpoint.part, HARDPOINT_FROM_VOX if hardpoint.from_vox else 0,
                          *(as_single(value) for value in hardpoint.position),
                          *(as_single(value) for value in hardpoint.rotation), 0, 0) for hardpoint in hardpoints)
  palette = b''.join(
    PALETTE_RECORD.pack(entry.red, entry.green, entry.blue, entry.alpha, PALETTE_EMISSIVE if entry.emissive else 0,
                        as_single(entry.emit), as_single(entry.flux)) for entry in model.palette)
  records = struct.pack(f'<{len(model.records)}I', *model.records)

  data = bytearray(FILE_HEADER.pack(MAGIC, VERSION_MAJOR, VERSION_MINOR, len(KNOWN_CHUNK_IDS), 0))
  append_chunk(data, KNOWN_CHUNK_IDS[KnownChunk.Strings], len(strings), strings)
  append_chunk(data, KNOWN_CHUNK_IDS[KnownChunk.Palette], len(model.palette), palette)
  append_chunk(data, KNOWN_CHUNK_IDS[KnownChunk.Parts], len(model.parts), parts)
  append_chunk(data, KNOWN_CHUNK_IDS[KnownChunk.Voxels], len(model.records), records)
  append_chunk(data, KNOWN_CHUNK_IDS[KnownChunk.Hardpoints], len(hardpoints), hardpoint_bytes)
  data = bytes(data)

  # Whatever the writer emits, the reader must accept; a model it would refuse is refused here, by the same name. A
  # name with a NUL in it passes the reader as the part before the NUL, so a name read back otherwise than written is
  # refused too, as a bad string.
  check = parse_nvf_model(data)
  if ([part.path for part in check.parts] != [part.path for part in model.parts]
      or [hardpoint.name for hardpoint in check.hardpoints] != [hardpoint.name for hardpoint in hardpoints]):
    raise NvfError('BadString')
  return data


def save_nvf_model(model, path):
  """Writes `model` to a temporary file beside `path` and renames it over `path`, so that a failure never leaves half a
  file behind."""
  data = serialize_nvf_model(model)
  path = Path(path)
  temporary = path.with_name(path.name + '.tmp')
  try:
    temporary.write_bytes(data)
    os.replace(temporary, path)
  except OSError as error:
    try:
      temporary.unlink()
    except OSError:
      pass
    raise NvfError('WriteFailed') from error
