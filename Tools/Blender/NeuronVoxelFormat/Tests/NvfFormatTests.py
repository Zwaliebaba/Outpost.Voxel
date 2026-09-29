"""NvfFormat.py against Design/NeuronVoxelFormat.md §4: the Python twin of Tests/NeuronCoreTests/NvfModelTests.cpp.

A test here and its C++ twin are named alike, and corrupt the golden file the same way into the same refusal, so that
both implementations are held to the one order of checking ADR-019 fixes (AGENTS.md R20). The Linux CI job runs them
with the system Python, from the repository root:

  python -m unittest discover -s Tools/Blender/NeuronVoxelFormat/Tests -p "*Tests.py"
"""

import math
import re
import struct
import sys
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path

EXTENSION = Path(__file__).resolve().parents[1]  # the extension's folder, which holds NvfFormat.py
if str(EXTENSION) not in sys.path:
  sys.path.insert(0, str(EXTENSION))

import NvfFormat  # noqa: E402  pylint: disable=wrong-import-position
from NvfFormat import NvfError, NvfHardpoint, as_single, pack_voxel_record  # noqa: E402
from NvfGolden import GOLDEN_NVF_PATH, golden_nvf_model  # noqa: E402
from RepositoryFile import find_repository_file, read_repository_file  # noqa: E402

NOT_A_NUMBER = math.nan
POSITIVE_INFINITY = math.inf

# The golden file's strings, in first-use order: part paths in part order, then hardpoint names in name order.
GOLDEN_STRINGS = b'hull\0hull/turret\0hull/turret/barrel\0dock.aft\0engine.main\0sensor.top.left\0weapon.main\0'

# Where each chunk of the golden file begins: its header, then its content at the next 16 bytes.
GOLDEN_CHUNK_OFFSETS = (16, 128, 400, 560, 640)
GOLDEN_FILE_BYTES = 848

GOLDEN_PATHS = ('hull', 'hull/turret', 'hull/turret/barrel')
GOLDEN_NAMES = ('dock.aft', 'engine.main', 'sensor.top.left', 'weapon.main')

# Each record's fields in file order, and how many values each holds: §4.3's tables, for the tests' raw edits.
RECORD_FIELDS = {
  b'PALT': (NvfFormat.PALETTE_RECORD, (('rgba', 4), ('flags', 1), ('emit', 1), ('flux', 1))),
  b'PART': (NvfFormat.PART_RECORD, (('name_offset', 1), ('parent_index', 1), ('size', 3), ('flags', 1),
                                    ('translation', 3), ('pivot', 3), ('first_voxel', 1), ('voxel_count', 1))),
  b'HPNT': (NvfFormat.HARDPOINT_RECORD, (('name_offset', 1), ('part_index', 1), ('flags', 1), ('position', 3),
                                         ('rotation', 4), ('reserved', 2))),
}


@dataclass
class RawChunk:
  """One chunk as the file frames it, free to break any rule: the tests' way to build the files the writer refuses to
  write."""
  id: bytes
  element_count: int
  content: bytearray
  reserved: int = 0


@dataclass
class RawFile:
  magic: bytes
  version_major: int
  version_minor: int
  reserved: int
  chunks: list
  chunk_count: int = None  # the header's count, when it should disagree with the chunks
  padding: int = 0         # what each chunk's content is padded with


def golden_bytes():
  return NvfFormat.serialize_nvf_model(golden_nvf_model())


def split(data):
  magic, version_major, version_minor, chunk_count, reserved = NvfFormat.FILE_HEADER.unpack_from(data, 0)
  raw = RawFile(magic, version_major, version_minor, reserved, [])
  offset = NvfFormat.FILE_HEADER.size
  for _ in range(chunk_count):
    chunk_id, size_bytes, element_count, chunk_reserved = NvfFormat.CHUNK_HEADER.unpack_from(data, offset)
    offset += NvfFormat.CHUNK_HEADER.size
    raw.chunks.append(RawChunk(chunk_id, element_count, bytearray(data[offset:offset + size_bytes]), chunk_reserved))
    offset += (size_bytes + 15) // 16 * 16
  return raw


def assemble(raw):
  chunk_count = len(raw.chunks) if raw.chunk_count is None else raw.chunk_count
  data = bytearray(NvfFormat.FILE_HEADER.pack(raw.magic, raw.version_major, raw.version_minor, chunk_count,
                                              raw.reserved))
  for chunk in raw.chunks:
    data += NvfFormat.CHUNK_HEADER.pack(chunk.id, len(chunk.content), chunk.element_count, chunk.reserved)
    data += chunk.content
    data += bytes([raw.padding]) * ((len(data) + 15) // 16 * 16 - len(data))
  return bytes(data)


def golden_raw():
  return split(golden_bytes())


def chunk_named(raw, chunk_id):
  return next(chunk for chunk in raw.chunks if chunk.id == chunk_id)


def get_record(chunk, index):
  """Record `index` of a PALT, PART or HPNT chunk, as a dict of its fields; a field of several values is a list."""
  record_struct, fields = RECORD_FIELDS[chunk.id]
  values = list(record_struct.unpack_from(chunk.content, index * record_struct.size))
  record = {}
  for name, count in fields:
    record[name] = values[0] if count == 1 else values[:count]
    del values[:count]
  return record


def set_record(chunk, index, record):
  record_struct, fields = RECORD_FIELDS[chunk.id]
  values = []
  for name, count in fields:
    values += [record[name]] if count == 1 else list(record[name])
  record_struct.pack_into(chunk.content, index * record_struct.size, *values)


def with_record(chunk_id, index, changes, raw=None):
  """The golden file, or `raw`, with record `index` of chunk `chunk_id` changed. A change to a field of several values
  is either all of them or a dict from index to value, so that {1: 0} sets the second and keeps the others."""
  raw = golden_raw() if raw is None else raw
  chunk = chunk_named(raw, chunk_id)
  record = get_record(chunk, index)
  for name, value in changes.items():
    if isinstance(value, dict):
      for component, component_value in value.items():
        record[name][component] = component_value
    else:
      record[name] = value
  set_record(chunk, index, record)
  return assemble(raw)


def with_part(index, **changes):
  return with_record(b'PART', index, changes)


def with_hardpoint(index, **changes):
  return with_record(b'HPNT', index, changes)


def with_palette(index, **changes):
  return with_record(b'PALT', index, changes)


def with_voxel(index, record):
  """The golden file with voxel record `index` replaced."""
  raw = golden_raw()
  struct.pack_into('<I', chunk_named(raw, b'VOXL').content, index * 4, record)
  return assemble(raw)


def with_names(part_paths, hardpoint_names):
  """The golden file with its strings rebuilt from `part_paths` and `hardpoint_names`, in first-use order, and every
  record's offset pointing at its own."""
  raw = golden_raw()
  strings = bytearray()
  offsets = {}

  def offset_of(name):
    name = name.encode('utf-8')
    if name not in offsets:
      offsets[name] = len(strings)
      strings.extend(name + b'\0')
    return offsets[name]

  for chunk_id, names in ((b'PART', part_paths), (b'HPNT', hardpoint_names)):
    chunk = chunk_named(raw, chunk_id)
    for i, name in enumerate(names):
      record = get_record(chunk, i)
      record['name_offset'] = offset_of(name)
      set_record(chunk, i, record)
  string_chunk = chunk_named(raw, b'STRS')
  string_chunk.content = strings
  string_chunk.element_count = len(strings)
  return assemble(raw)


def with_strings(content):
  """The golden file with STRS's content replaced by `content`, its count agreeing."""
  raw = golden_raw()
  string_chunk = chunk_named(raw, b'STRS')
  string_chunk.content = bytearray(content)
  string_chunk.element_count = len(content)
  return assemble(raw)


def read(fields, data, offset):
  """One value at `offset`, read by struct's `fields` without the reader: §4's tables, checked on their own."""
  return struct.unpack_from('<' + fields, data, offset)[0]


def single_bits(value):
  return struct.pack('<f', value)


class NvfFormatTests(unittest.TestCase):

  def expect_refusal(self, expected, data, case):
    with self.assertRaises(NvfError, msg=f'{case}: the file was accepted') as caught:
      NvfFormat.parse_nvf_model(data)
    self.assertEqual(expected, caught.exception.name, case)

  def expect_accepted(self, data, case):
    try:
      return NvfFormat.parse_nvf_model(data)
    except NvfError as error:
      self.fail(f'{case}: refused with {error.name}')

  def expect_write_refusal(self, expected, model, case):
    with self.assertRaises(NvfError, msg=f'{case}: the model was written') as caught:
      NvfFormat.serialize_nvf_model(model)
    self.assertEqual(expected, caught.exception.name, case)

  def assert_same_float(self, expected, actual, what):
    """Bit for bit, so that a NaN or a negative zero cannot pass for what the file holds."""
    self.assertEqual(single_bits(expected), single_bits(actual), f'{what}: {expected} against {actual}')

  def assert_same_floats(self, expected, actual, what):
    self.assertEqual(len(expected), len(actual), what)
    for expected_value, actual_value in zip(expected, actual):
      self.assert_same_float(expected_value, actual_value, what)

  def assert_equal_models(self, expected, actual):
    self.assertEqual(len(expected.palette), len(actual.palette), 'palette')
    for i, (expected_entry, actual_entry) in enumerate(zip(expected.palette, actual.palette)):
      what = f'palette entry {i}'
      self.assertEqual((expected_entry.red, expected_entry.green, expected_entry.blue, expected_entry.alpha,
                        expected_entry.emissive),
                       (actual_entry.red, actual_entry.green, actual_entry.blue, actual_entry.alpha,
                        actual_entry.emissive), what)
      self.assert_same_float(expected_entry.emit, actual_entry.emit, f'{what} emit')
      self.assert_same_float(expected_entry.flux, actual_entry.flux, f'{what} flux')

    self.assertEqual(len(expected.parts), len(actual.parts), 'parts')
    for i, (expected_part, actual_part) in enumerate(zip(expected.parts, actual.parts)):
      what = f'part {i}'
      self.assertEqual((expected_part.path, expected_part.parent, tuple(expected_part.size),
                        tuple(expected_part.translation), expected_part.pivot_authored, expected_part.first_voxel,
                        expected_part.voxel_count),
                       (actual_part.path, actual_part.parent, tuple(actual_part.size), tuple(actual_part.translation),
                        actual_part.pivot_authored, actual_part.first_voxel, actual_part.voxel_count), what)
      self.assert_same_floats(expected_part.pivot, actual_part.pivot, f'{what} pivot')
    self.assertEqual(list(expected.records), list(actual.records), 'records')

    # The file holds hardpoints in name order, whatever order the model listed them in.
    expected_hardpoints = sorted(expected.hardpoints, key=lambda hardpoint: hardpoint.name)
    self.assertEqual(len(expected_hardpoints), len(actual.hardpoints), 'hardpoints')
    for i, (expected_hardpoint, actual_hardpoint) in enumerate(zip(expected_hardpoints, actual.hardpoints)):
      what = f'hardpoint {i}'
      self.assertEqual((expected_hardpoint.name, expected_hardpoint.part, expected_hardpoint.from_vox),
                       (actual_hardpoint.name, actual_hardpoint.part, actual_hardpoint.from_vox), what)
      self.assert_same_floats(expected_hardpoint.position, actual_hardpoint.position, f'{what} position')
      self.assert_same_floats(expected_hardpoint.rotation, actual_hardpoint.rotation, f'{what} rotation')

  # §9: the model built in code serializes to the committed golden file, byte for byte. NvfModelTests.cpp writes and
  # reads the same file, so if either implementation drifts, its test fails.
  def test_writes_the_golden_file(self):
    self.assertEqual(read_repository_file(GOLDEN_NVF_PATH), golden_bytes())

  def test_reads_the_golden_file(self):
    model = self.expect_accepted(read_repository_file(GOLDEN_NVF_PATH), 'the golden file')
    self.assert_equal_models(golden_nvf_model(), model)
    self.assertEqual([], model.unknown_chunks, 'no unknown chunk')
    types = [NvfFormat.hardpoint_type(hardpoint) for hardpoint in model.hardpoints]
    self.assertEqual(['dock', 'engine', 'sensor', 'weapon'], types, 'the type is the name\'s first segment')

  # The golden file against §4's tables, read at the offsets they give rather than through the reader.
  def test_lays_the_golden_file_out_as_specified(self):
    data = read_repository_file(GOLDEN_NVF_PATH)
    self.assertEqual(GOLDEN_FILE_BYTES, len(data), 'file size')
    self.assertEqual(b'NVF ', data[0:4], 'magic')
    self.assertEqual(1, read('H', data, 4), 'versionMajor')
    self.assertEqual(0, read('H', data, 6), 'versionMinor')
    self.assertEqual(5, read('I', data, 8), 'chunkCount')
    self.assertEqual(0, read('I', data, 12), 'reserved')

    ids = (b'STRS', b'PALT', b'PART', b'VOXL', b'HPNT')
    sizes = (85, 256, 144, 64, 192)
    counts = (85, 16, 3, 16, 4)
    for i, chunk_id in enumerate(ids):
      offset = GOLDEN_CHUNK_OFFSETS[i]
      what = chunk_id.decode('ascii')
      self.assertEqual(chunk_id, data[offset:offset + 4], what)
      self.assertEqual(sizes[i], read('I', data, offset + 4), f'{what} sizeBytes')
      self.assertEqual(counts[i], read('I', data, offset + 8), f'{what} elementCount')
      self.assertEqual(0, read('I', data, offset + 12), f'{what} reserved')
      self.assertEqual(0, (offset + 16) % 16, f'{what} content is 16-byte aligned')
      end = offset + 16 + sizes[i]
      following = GOLDEN_CHUNK_OFFSETS[i + 1] if i + 1 < len(ids) else GOLDEN_FILE_BYTES
      self.assertEqual((end + 15) // 16 * 16, following, f'{what} is padded to the next 16 bytes')
      self.assertFalse(any(data[end:following]), f'{what} padding is zero')

    strings = GOLDEN_CHUNK_OFFSETS[0] + 16
    self.assertEqual(GOLDEN_STRINGS, data[strings:strings + len(GOLDEN_STRINGS)], 'STRS in first-use order')

    # PALT entry 10, the light blue that glows: bytes, flags, emit and flux.
    light_blue = GOLDEN_CHUNK_OFFSETS[1] + 16 + 9 * 16
    self.assertEqual(bytes((85, 85, 255, 255)), data[light_blue:light_blue + 4], 'entry 10\'s color')
    self.assertEqual(1, read('I', data, light_blue + 4), 'entry 10 is emissive')
    self.assertEqual(as_single(0.6), read('f', data, light_blue + 8), 'entry 10\'s emit')
    self.assertEqual(2.0, read('f', data, light_blue + 12), 'entry 10\'s flux')

    # PART record 1, hull/turret, field by field at §4.3's offsets.
    turret = GOLDEN_CHUNK_OFFSETS[2] + 16 + 48
    self.assertEqual(5, read('I', data, turret + 0), 'nameOffset')
    self.assertEqual(0, read('I', data, turret + 4), 'parentIndex')
    self.assertEqual((3, 2, 3), struct.unpack_from('<3H', data, turret + 8), 'sizeVoxels')
    self.assertEqual(1, read('H', data, turret + 14), 'flags: PivotAuthored')
    self.assertEqual((1, 3, 2), struct.unpack_from('<3i', data, turret + 16), 'translation')
    self.assertEqual((1.5, 0.0, 1.5), struct.unpack_from('<3f', data, turret + 28), 'pivot')
    self.assertEqual(8, read('I', data, turret + 40), 'firstVoxel')
    self.assertEqual(4, read('I', data, turret + 44), 'voxelCount')
    self.assertEqual(0xFFFFFFFF, read('I', data, GOLDEN_CHUNK_OFFSETS[2] + 16 + 4), 'part 0 has no parent')

    # VOXL's last record: the barrel's (0, 0, 3) in entry 16.
    self.assertEqual(pack_voxel_record(0, 0, 3, 15), read('I', data, GOLDEN_CHUNK_OFFSETS[3] + 16 + 15 * 4),
                     'the last record')

    # HPNT in name order; record 3 is weapon.main, field by field.
    hardpoints = GOLDEN_CHUNK_OFFSETS[4] + 16
    for i, name_offset in enumerate((36, 45, 57, 73)):
      self.assertEqual(name_offset, read('I', data, hardpoints + i * 48), 'hardpoints in name order')
    weapon = hardpoints + 3 * 48
    self.assertEqual(2, read('I', data, weapon + 4), 'partIndex')
    self.assertEqual(0, read('I', data, weapon + 8), 'flags: not from the .vox')
    self.assertEqual(4.0, read('f', data, weapon + 20), 'position z')
    self.assertEqual(as_single(0.25881904), read('f', data, weapon + 28), 'rotation y')
    self.assertEqual(as_single(0.9659258), read('f', data, weapon + 36), 'rotation w')
    self.assertEqual((0, 0), struct.unpack_from('<2I', data, weapon + 40), 'reserved')
    self.assertEqual(1, read('I', data, hardpoints + 48 + 8), 'engine.main comes from the .vox')

  def test_round_trips_its_own_bytes(self):
    golden = read_repository_file(GOLDEN_NVF_PATH)
    model = self.expect_accepted(golden, 'the golden file')
    self.assertEqual(golden, NvfFormat.serialize_nvf_model(model), 'read, then written')

  # §4.3: two writers produce the same bytes. Strings follow first use, and hardpoints are written in name order, so
  # the order a tool listed them in cannot change the file.
  def test_writes_hardpoints_in_name_order(self):
    model = golden_nvf_model()
    model.hardpoints.reverse()
    self.assertEqual(golden_bytes(), NvfFormat.serialize_nvf_model(model), 'hardpoints listed in reverse')
    model.hardpoints.sort(key=lambda hardpoint: hardpoint.part)
    self.assertEqual(golden_bytes(), NvfFormat.serialize_nvf_model(model), 'hardpoints listed by part')

  # §4.2, §4.6: a reader of 1.0 skips a chunk it does not know, wherever it lies, reads any minor version, and says
  # what it skipped so that a tool that rewrites the file can refuse it.
  def test_skips_unknown_chunks(self):
    raw = golden_raw()
    raw.version_minor = 7
    raw.chunks.insert(0, RawChunk(b'NOTE', 3, bytearray(b'\x01\x02\x03')))
    raw.chunks.insert(4, RawChunk(b'XTRA', 0, bytearray()))
    raw.chunks.append(RawChunk(b'\0\x01zz', 99, bytearray(b'\xab' * 40)))
    model = self.expect_accepted(assemble(raw), 'three unknown chunks')
    self.assert_equal_models(golden_nvf_model(), model)
    self.assertEqual([b'NOTE', b'XTRA', b'\0\x01zz'], model.unknown_chunks, 'the chunks skipped')

    # The framing of a chunk the reader does not know is still checked.
    reserved = golden_raw()
    reserved.chunks.append(RawChunk(b'XTRA', 0, bytearray(), 1))
    self.expect_refusal('MalformedChunk', assemble(reserved), 'an unknown chunk\'s reserved field')
    padded = golden_raw()
    padded.chunks.append(RawChunk(b'XTRA', 0, bytearray(b'\x07')))
    data = bytearray(assemble(padded))
    data[-1] = 1
    self.expect_refusal('MalformedChunk', data, 'an unknown chunk\'s padding')

  def test_refuses_what_is_not_an_nvf_file(self):
    self.expect_refusal('NotAnNvfFile', b'', 'empty')
    self.expect_refusal('NotAnNvfFile', b'NVF', 'three bytes')
    lower = bytearray(golden_bytes())
    lower[0] = ord('n')
    self.expect_refusal('NotAnNvfFile', lower, 'nvf in lower case')
    no_space = bytearray(golden_bytes())
    no_space[3] = 0
    self.expect_refusal('NotAnNvfFile', no_space, 'a NUL for the space')
    vox = b'VOX ' + struct.pack('<I', 200) + b'MAIN' + struct.pack('<II', 0, 0)
    self.expect_refusal('NotAnNvfFile', vox, 'a .vox file')

  def test_refuses_unsupported_versions(self):
    for major in (0, 2, 0xFFFF):
      raw = golden_raw()
      raw.version_major = major
      self.expect_refusal('UnsupportedVersion', assemble(raw), f'version {major}.0')
    for minor in (1, 0xFFFF):
      raw = golden_raw()
      raw.version_minor = minor
      self.assert_equal_models(golden_nvf_model(), self.expect_accepted(assemble(raw), f'version 1.{minor}'))

  def test_refuses_every_truncation(self):
    data = golden_bytes()
    for length in range(len(data)):
      # Four bytes hold the magic; after it, every cut leaves the header or a chunk short.
      expected = 'NotAnNvfFile' if length < 4 else 'Truncated'
      self.expect_refusal(expected, data[:length], f'cut to {length} of {len(data)} bytes')

  def test_refuses_malformed_chunks(self):
    raw = golden_raw()
    raw.reserved = 1
    self.expect_refusal('MalformedChunk', assemble(raw), 'the header\'s reserved field')

    raw = golden_raw()
    chunk_named(raw, b'PALT').reserved = 1
    self.expect_refusal('MalformedChunk', assemble(raw), 'a chunk\'s reserved field')

    padding = bytearray(golden_bytes())
    padding[GOLDEN_CHUNK_OFFSETS[0] + 16 + 85] = 1
    self.expect_refusal('MalformedChunk', padding, 'a padding byte after STRS')

    self.expect_refusal('MalformedChunk', golden_bytes() + b'\0', 'a byte after the last chunk')

    raw = golden_raw()
    raw.chunk_count = 4
    self.expect_refusal('MalformedChunk', assemble(raw), 'a chunk count one short')

    raw = golden_raw()
    chunk_named(raw, b'STRS').element_count = 84
    self.expect_refusal('MalformedChunk', assemble(raw), 'STRS counting other than its bytes')

    large = GOLDEN_STRINGS + b'a' * (NvfFormat.MAX_STRING_BYTES - len(GOLDEN_STRINGS)) + b'\0'
    self.expect_refusal('MalformedChunk', with_strings(large), 'STRS of 64 KiB and one byte')
    self.expect_accepted(with_strings(large[:NvfFormat.MAX_STRING_BYTES - 1] + b'\0'), 'STRS of exactly 64 KiB')

    raw = golden_raw()
    palette = chunk_named(raw, b'PALT')
    del palette.content[15 * 16:]
    palette.element_count = 15
    self.expect_refusal('MalformedChunk', assemble(raw), 'a palette of 15 entries')
    palette.element_count = 16
    self.expect_refusal('MalformedChunk', assemble(raw), 'a palette counting 16 in the bytes of 15')

    raw = golden_raw()
    chunk_named(raw, b'PART').element_count = 2
    self.expect_refusal('MalformedChunk', assemble(raw), 'PART counting two of three records')

    raw = golden_raw()
    chunk_named(raw, b'VOXL').content.pop()
    self.expect_refusal('MalformedChunk', assemble(raw), 'VOXL a byte short of its count')

    raw = golden_raw()
    chunk_named(raw, b'HPNT').content.append(0)
    self.expect_refusal('MalformedChunk', assemble(raw), 'HPNT a byte over its count')

    # The limits are checked before anything in the chunk is read, so these records need not be valid.
    raw = golden_raw()
    parts = chunk_named(raw, b'PART')
    parts.element_count = NvfFormat.MAX_PARTS + 1
    parts.content = bytearray(parts.element_count * 48)
    self.expect_refusal('MalformedChunk', assemble(raw), '1,025 parts')

    raw = golden_raw()
    hardpoints = chunk_named(raw, b'HPNT')
    hardpoints.element_count = NvfFormat.MAX_HARDPOINTS + 1
    hardpoints.content = bytearray(hardpoints.element_count * 48)
    self.expect_refusal('MalformedChunk', assemble(raw), '4,097 hardpoints')

    self.expect_refusal('MalformedChunk', with_palette(3, flags=2), 'a palette flag bit 1')
    self.expect_refusal('MalformedChunk', with_palette(9, flags=0x80000001), 'a palette flag bit 31')
    self.expect_refusal('MalformedChunk', with_part(1, flags=3), 'a part flag bit 1')
    self.expect_refusal('MalformedChunk', with_part(2, size={1: 0}), 'a part 0 voxels tall')
    self.expect_refusal('MalformedChunk', with_hardpoint(0, flags=2), 'a hardpoint flag bit 1')
    self.expect_refusal('MalformedChunk', with_hardpoint(1, reserved={0: 1}), 'a hardpoint\'s first reserved word')
    self.expect_refusal('MalformedChunk', with_hardpoint(3, reserved={1: 1}), 'a hardpoint\'s second reserved word')

  def test_refuses_missing_and_misordered_chunks(self):
    for chunk_id in NvfFormat.KNOWN_CHUNK_IDS:
      raw = golden_raw()
      raw.chunks = [chunk for chunk in raw.chunks if chunk.id != chunk_id]
      self.expect_refusal('MissingChunk', assemble(raw), f'without {chunk_id}')

    swapped = golden_raw()
    swapped.chunks[0], swapped.chunks[1] = swapped.chunks[1], swapped.chunks[0]
    self.expect_refusal('ChunkOutOfOrder', assemble(swapped), 'PALT before STRS')

    late = golden_raw()
    late.chunks[3], late.chunks[4] = late.chunks[4], late.chunks[3]
    self.expect_refusal('ChunkOutOfOrder', assemble(late), 'HPNT before VOXL')

    twice = golden_raw()
    twice.chunks.append(twice.chunks[1])
    self.expect_refusal('ChunkOutOfOrder', assemble(twice), 'a second PALT')

    repeated = golden_raw()
    repeated.chunks.insert(1, repeated.chunks[0])
    self.expect_refusal('ChunkOutOfOrder', assemble(repeated), 'STRS twice in a row')

  def test_refuses_bad_strings(self):
    self.expect_refusal('BadString', with_part(1, name_offset=6), 'an offset inside a string')
    self.expect_refusal('BadString', with_part(0, name_offset=85), 'an offset past STRS')
    self.expect_refusal('BadString', with_hardpoint(2, name_offset=0xFFFFFFFF), 'a hardpoint\'s offset far past STRS')
    self.expect_refusal('BadString', with_hardpoint(0, name_offset=4), 'an offset at the NUL that ends a string')
    empty = split(with_strings(GOLDEN_STRINGS + b'\0'))
    self.expect_refusal('BadString', with_record(b'HPNT', 0, {'name_offset': 85}, empty), 'the empty name')

    self.expect_refusal('BadString', with_strings(GOLDEN_STRINGS[:-1] + b'x'), 'STRS whose last string has no NUL')

    # Strings nothing refers to are read, but must still be UTF-8, which Table 3-7 of the Unicode Standard defines.
    invalid = (
      ('a lone continuation byte', b'\x80'),
      ('an overlong slash', b'\xc0\xaf'),
      ('an overlong three-byte form', b'\xe0\x80\xaf'),
      ('a surrogate', b'\xed\xa0\x80'),
      ('beyond U+10FFFF', b'\xf4\x90\x80\x80'),
      ('a lead byte cut short', b'\xe2\x82'),
      ('a byte UTF-8 never uses', b'\xff'),
    )
    for what, sequence in invalid:
      self.expect_refusal('BadString', with_strings(GOLDEN_STRINGS + sequence + b'\0'), what)
    accented = GOLDEN_STRINGS + b'\xc3\xa9\xe2\x82\xac\xf0\x9f\x9a\x80\0'
    self.expect_accepted(with_strings(accented), 'well-formed UTF-8 nothing refers to')

    longest = 'a' * NvfFormat.MAX_SEGMENT_CHARS
    self.expect_accepted(with_names(('hull', f'hull/{longest}', f'hull/{longest}/barrel'), GOLDEN_NAMES),
                         'a segment of 31 characters')
    bad_paths = (
      ('Hull', 'Hull/turret', 'Hull/turret/barrel'),
      ('hull', 'hull/turret', 'hull/turret/barrel-2'),
      ('hull', 'hull/turret', 'hull/turret/'),
      ('hull', 'hull//turret', 'hull//turret/barrel'),
      ('hull', 'hull/turret', 'hull/turret/barrel.main'),
      ('hull', f'hull/{longest}a', f'hull/{longest}a/barrel'),
      ('hull', 'hull/turét', 'hull/turét/barrel'),
    )
    for paths in bad_paths:
      self.expect_refusal('BadString', with_names(paths, GOLDEN_NAMES), f'part path {paths[1]} {paths[2]}')
    bad_names = ('weapon', 'pivot.main', 'weapon..main', 'weapon.main.', '.main', 'Weapon.main', f'weapon.m{longest}')
    for name in bad_names:
      self.expect_refusal('BadString', with_names(GOLDEN_PATHS, GOLDEN_NAMES[:3] + (name,)), f'hardpoint name {name}')
    self.expect_accepted(with_names(GOLDEN_PATHS, GOLDEN_NAMES[:3] + ('pivots.main',)), 'a type that begins pivot')
    self.expect_accepted(with_names(('pivot', 'pivot/turret', 'pivot/turret/barrel'), GOLDEN_NAMES),
                         'a part named pivot')

  def test_refuses_duplicate_names(self):
    self.expect_refusal('DuplicateName', with_names(GOLDEN_PATHS, GOLDEN_NAMES[:3] + ('dock.aft',)),
                        'two hardpoints named dock.aft')

    # Part 2 moved under part 0 as a second hull/turret: a sound tree with one path twice.
    raw = split(with_names(('hull', 'hull/turret', 'hull/turret'), GOLDEN_NAMES))
    self.expect_refusal('DuplicateName', with_record(b'PART', 2, {'parent_index': 0}, raw),
                        'two parts named hull/turret')

  def test_refuses_bad_part_trees(self):
    self.expect_refusal('BadPartTree', with_part(0, parent_index=0), 'part 0 its own parent')
    self.expect_refusal('BadPartTree', with_part(1, parent_index=NvfFormat.NO_PARENT), 'a second root')
    self.expect_refusal('BadPartTree', with_part(1, parent_index=1), 'a part its own parent')
    self.expect_refusal('BadPartTree', with_part(1, parent_index=2), 'a parent after its child')
    self.expect_refusal('BadPartTree', with_part(2, parent_index=0), 'a path that names another parent')
    self.expect_refusal('BadPartTree', with_names(('hull', 'hull/turret', 'hull/barrel'), GOLDEN_NAMES),
                        'a path off its parent\'s')
    self.expect_refusal('BadPartTree', with_names(('ship/hull', 'ship/hull/turret', 'ship/hull/turret/barrel'),
                                                  GOLDEN_NAMES), 'a root whose path has two segments')
    self.expect_refusal('BadPartTree', with_names(('hull', 'turret', 'turret/barrel'), GOLDEN_NAMES),
                        'a child of one segment')

    raw = golden_raw()
    parts = chunk_named(raw, b'PART')
    parts.content = bytearray()
    parts.element_count = 0
    self.expect_refusal('BadPartTree', assemble(raw), 'no part at all')

  def test_refuses_parts_too_large(self):
    for axis in range(3):
      self.expect_refusal('PartTooLarge', with_part(1, size={axis: 257}), f'257 on axis {axis}')
    self.expect_accepted(with_part(0, size=(256, 256, 256)), '256 cubed')

  def test_refuses_translations_out_of_range(self):
    # The parts' origins sum down the tree: the barrel's x is the hull's plus 1 plus 1.
    limit = NvfFormat.MAX_PART_ORIGIN
    self.expect_accepted(with_part(0, translation={0: limit - 2}), 'the barrel at the limit')
    self.expect_refusal('TranslationOutOfRange', with_part(0, translation={0: limit - 1}),
                        'the barrel one past the limit')
    self.expect_accepted(with_part(0, translation={1: -limit}), 'the hull at the negative limit')
    self.expect_refusal('TranslationOutOfRange', with_part(0, translation={2: -limit - 1}),
                        'the hull one past the negative limit')
    self.expect_refusal('TranslationOutOfRange', with_part(2, translation={2: -2**31}), 'the least int32')

  def test_refuses_bad_voxel_ranges(self):
    self.expect_refusal('BadVoxelRange', with_part(0, first_voxel=1), 'part 0 not at 0')
    self.expect_refusal('BadVoxelRange', with_part(1, first_voxel=9), 'a gap')
    self.expect_refusal('BadVoxelRange', with_part(1, first_voxel=7), 'an overlap')
    self.expect_refusal('BadVoxelRange', with_part(2, first_voxel=12, voxel_count=0), 'a part without a voxel')
    emptied = split(with_part(1, voxel_count=0))
    self.expect_refusal('BadVoxelRange', with_record(b'PART', 2, {'first_voxel': 8, 'voxel_count': 8}, emptied),
                        'a part without a voxel, the counts adding up')
    self.expect_refusal('BadVoxelRange', with_part(2, voxel_count=3), 'a record no part holds')

    raw = golden_raw()
    voxels = chunk_named(raw, b'VOXL')
    voxels.content += bytes(4)
    voxels.element_count = 17
    self.expect_refusal('BadVoxelRange', assemble(raw), 'VOXL holding more than the parts')

  def test_refuses_voxels_out_of_bounds_or_twice(self):
    # The hull is 5 x 3 x 7, and its records are 0-7.
    self.expect_refusal('VoxelOutOfBounds', with_voxel(3, pack_voxel_record(5, 0, 0, 3)), 'x at the hull\'s width')
    self.expect_refusal('VoxelOutOfBounds', with_voxel(3, pack_voxel_record(0, 3, 0, 3)), 'y at the hull\'s height')
    self.expect_refusal('VoxelOutOfBounds', with_voxel(3, pack_voxel_record(0, 0, 7, 3)), 'z at the hull\'s depth')
    self.expect_refusal('VoxelOutOfBounds', with_voxel(15, pack_voxel_record(0, 0, 4, 15)), 'the barrel\'s fifth voxel')
    self.expect_refusal('DuplicateVoxel', with_voxel(7, pack_voxel_record(4, 2, 6, 7)), 'two voxels at (4, 2, 6)')
    self.expect_refusal('DuplicateVoxel', with_voxel(9, pack_voxel_record(0, 0, 0, 9)), 'the turret\'s corner twice')
    self.expect_refusal('ReservedBitsSet', with_voxel(0, pack_voxel_record(0, 0, 0, 0) | 0x10000000), 'bit 28')
    self.expect_refusal('ReservedBitsSet', with_voxel(12, pack_voxel_record(0, 0, 0, 12) | 0x80000000), 'bit 31')

  def test_refuses_bad_hardpoint_parts(self):
    self.expect_refusal('BadHardpointPart', with_hardpoint(3, part_index=3), 'part 3 of 3')
    self.expect_refusal('BadHardpointPart', with_hardpoint(0, part_index=0xFFFFFFFF), 'no part')

  def test_refuses_non_finite_values(self):
    self.expect_refusal('NotFinite', with_palette(0, emit=NOT_A_NUMBER), 'a NaN emit')
    self.expect_refusal('NotFinite', with_palette(15, flux=POSITIVE_INFINITY), 'an infinite flux')
    self.expect_refusal('NotFinite', with_part(2, pivot={1: -POSITIVE_INFINITY}), 'an infinite pivot')
    self.expect_refusal('NotFinite', with_hardpoint(1, position={2: NOT_A_NUMBER}), 'a NaN position')
    self.expect_refusal('NotFinite', with_hardpoint(3, rotation={3: NOT_A_NUMBER}), 'a NaN rotation')

  def test_refuses_rotations_that_are_not_unit(self):
    def with_rotation(rotation):
      return with_hardpoint(2, rotation=list(rotation))

    self.expect_refusal('NotUnitRotation', with_rotation((0.0, 0.0, 0.0, 1.001)), 'a little long')
    self.expect_refusal('NotUnitRotation', with_rotation((0.0, 0.0, 0.0, 0.999)), 'a little short')
    self.expect_refusal('NotUnitRotation', with_rotation((0.0, 0.0, 0.0, 0.0)), 'zero')
    self.expect_refusal('NotUnitRotation', with_rotation((0.0, 0.0, 0.0, -1.0)), 'w below zero')
    self.expect_refusal('NotUnitRotation', with_rotation((0.0, 0.25881904, 0.0, -0.9659258)),
                        '30 degrees, spelled with w < 0')
    self.expect_accepted(with_rotation((0.0, 0.0, 0.0, 1.00005)), 'within 10^-4')
    self.expect_accepted(with_rotation((0.0, 0.0, -1.0, 0.0)), 'half a turn, w zero')

  # ADR-019: the reader checks in one order and reports the first fault, and NvfModel.cpp checks in the same order, so
  # that both name the same fault in a file that has two.
  def test_checks_in_the_fixed_order(self):
    raw = golden_raw()
    chunk_named(raw, b'PALT').reserved = 1
    self.expect_refusal('MalformedChunk', assemble(raw)[:-1],
                        'framing in file order: PALT\'s reserved field before HPNT\'s end')
    raw = golden_raw()
    chunk_named(raw, b'HPNT').reserved = 1
    self.expect_refusal('Truncated', assemble(raw)[:-1], 'within a chunk, its length before its reserved field')

    raw = golden_raw()
    raw.chunks[3], raw.chunks[4] = raw.chunks[4], raw.chunks[3]
    chunk_named(raw, b'STRS').content[-1] = ord('x')
    self.expect_refusal('ChunkOutOfOrder', assemble(raw), 'chunk order before any chunk\'s content')

    raw = split(with_strings(GOLDEN_STRINGS + b'\xff\0'))
    self.expect_refusal('BadString', with_record(b'PALT', 0, {'emit': NOT_A_NUMBER}, raw), 'STRS before PALT')

    self.expect_refusal('PartTooLarge', with_part(1, size={0: 300}, translation={0: 2**31 - 1}),
                        'a part\'s size before its translation')
    self.expect_refusal('DuplicateName', with_part(2, name_offset=5, parent_index=0, pivot={0: NOT_A_NUMBER}),
                        'a part\'s name before its pivot')
    self.expect_refusal('ReservedBitsSet', with_voxel(2, pack_voxel_record(9, 0, 0, 2) | 0x20000000),
                        'reserved bits before bounds')

    raw = split(with_voxel(1, pack_voxel_record(0, 0, 0, 1)))
    self.expect_refusal('DuplicateVoxel', with_record(b'HPNT', 0, {'part_index': 9}, raw), 'VOXL before HPNT')

    self.expect_refusal('BadHardpointPart', with_hardpoint(0, part_index=7, position={0: NOT_A_NUMBER}),
                        'a hardpoint\'s part before its position')

  # The writer refuses, by the reader's name, a model the reader would refuse, so that nothing it writes fails to read.
  def test_refuses_to_write_what_it_would_not_read(self):
    model = golden_nvf_model()
    model.hardpoints[1].name = 'weapon.main'
    self.expect_write_refusal('DuplicateName', model, 'two hardpoints named weapon.main')

    model = golden_nvf_model()
    model.parts[1].size = (3, 65536 + 2, 3)
    self.expect_write_refusal('PartTooLarge', model, 'a size 16 bits would wrap to 2')

    model = golden_nvf_model()
    model.parts[2].size = (1, 1, 0)
    self.expect_write_refusal('MalformedChunk', model, 'a size of 0')

    model = golden_nvf_model()
    model.parts[0].size = (-1, 3, 7)
    self.expect_write_refusal('MalformedChunk', model, 'a negative size')

    model = golden_nvf_model()
    model.hardpoints[0].name = 'Weapon.main'
    self.expect_write_refusal('BadString', model, 'a hardpoint name in capitals')

    model = golden_nvf_model()
    model.parts[1].pivot = (1.5, NOT_A_NUMBER, 1.5)
    self.expect_write_refusal('NotFinite', model, 'a NaN pivot')

    model = golden_nvf_model()
    model.parts[2].parent = 0
    self.expect_write_refusal('BadPartTree', model, 'a parent its path does not name')

    model = golden_nvf_model()
    model.hardpoints[2].rotation = (0.0, 0.0, 0.0, 2.0)
    self.expect_write_refusal('NotUnitRotation', model, 'a rotation of length 2')

    model = golden_nvf_model()
    model.records.append(pack_voxel_record(0, 0, 1, 0))
    self.expect_write_refusal('BadVoxelRange', model, 'a record no part holds')

    # A name with a NUL in it reads back as the part before the NUL, which the reader alone would accept.
    model = golden_nvf_model()
    model.parts[0].path = 'hull\0x'
    self.expect_write_refusal('BadString', model, 'a part path with a NUL in it')
    model = golden_nvf_model()
    model.hardpoints[3].name = 'dock.aft\0'
    self.expect_write_refusal('BadString', model, 'a hardpoint name ending in a NUL')

  # What Python can hold and the file cannot: a float single precision rounds, or cannot hold at all, and a name no
  # UTF-8 encoder would write. Each is written as single precision or UTF-8 would hold it and read back.
  def test_writes_what_single_precision_holds(self):
    model = golden_nvf_model()
    model.parts[0].pivot = (0.1, 1.5, 3.5)
    written = self.expect_accepted(NvfFormat.serialize_nvf_model(model), 'a pivot of 0.1')
    self.assertNotEqual(0.1, written.parts[0].pivot[0], 'single precision does not hold 0.1')
    self.assertEqual(as_single(0.1), written.parts[0].pivot[0], 'the file holds 0.1 as single precision rounds it')

    self.assertEqual(math.inf, as_single(1.0e39), 'beyond single precision\'s range')
    self.assertEqual(-math.inf, as_single(-1.0e39), 'beyond it on the negative side')
    self.assertEqual(3.4028234663852886e38, as_single(3.4028235e38), 'the largest single rounds to itself')
    model = golden_nvf_model()
    model.hardpoints[0].position = (1.0e39, 0.0, 0.0)
    self.expect_write_refusal('NotFinite', model, 'a position single precision cannot hold')

    model = golden_nvf_model()
    model.hardpoints[0].name = 'weapon.\udc80'
    self.expect_write_refusal('BadString', model, 'a lone surrogate, which is not UTF-8')

  def test_names_every_error(self):
    # NvfModel.h's enum, read from the C++ source, so that the two implementations have the same names (§4.4).
    header = read_repository_file('NeuronCore/NvfModel.h').decode('utf-8')
    body = re.search(r'enum class NvfError : std::uint8_t\s*\{([^}]*)\}', header)
    self.assertIsNotNone(body, 'NvfModel.h declares enum class NvfError')
    names = tuple(re.sub(r'//[^\n]*', '', name).strip() for name in body.group(1).split(','))
    self.assertEqual(NvfFormat.ERROR_NAMES, tuple(name for name in names if name))
    for name in NvfFormat.ERROR_NAMES:
      self.assertEqual(name, NvfError(name).name)
      self.assertEqual(name, str(NvfError(name)))
    with self.assertRaises(ValueError):
      NvfError('NotAName')

  def test_saves_and_loads_files(self):
    with tempfile.TemporaryDirectory() as directory:
      directory = Path(directory)
      with self.assertRaises(NvfError) as caught:
        NvfFormat.load_nvf_model(directory / 'Missing.nvf')
      self.assertEqual('FileNotFound', caught.exception.name, 'a file that does not exist')

      path = directory / 'SavesAndLoadsFiles.nvf'
      temporary = directory / 'SavesAndLoadsFiles.nvf.tmp'
      path.write_bytes(b'an older file, which the save replaces')
      NvfFormat.save_nvf_model(golden_nvf_model(), path)
      self.assertFalse(temporary.exists(), 'no temporary file is left behind')
      self.assert_equal_models(golden_nvf_model(), NvfFormat.load_nvf_model(path))

      # A model the writer refuses leaves the file as it was.
      bad = golden_nvf_model()
      bad.hardpoints[0].name = 'pivot.main'
      with self.assertRaises(NvfError) as caught:
        NvfFormat.save_nvf_model(bad, path)
      self.assertEqual('BadString', caught.exception.name, 'saving a model the writer refuses')
      self.assert_equal_models(golden_nvf_model(), NvfFormat.load_nvf_model(path))

      with self.assertRaises(NvfError) as caught:
        NvfFormat.save_nvf_model(golden_nvf_model(), directory / 'NoSuchFolder' / 'Model.nvf')
      self.assertEqual('WriteFailed', caught.exception.name, 'saving into a folder that does not exist')

  # §4.1's spelling of names.
  def test_spells_names_as_specified(self):
    for path in ('main', 'hull', 'hull/turret', 'a/b/c/d', 'x_1/y2', '0'):
      self.assertTrue(NvfFormat.is_nvf_part_path(path), path)
    for path in ('', '/', 'hull/', '/hull', 'Hull', 'hull turret', 'hull.turret', 'hull\\turret', 'h-1', 'turét',
                 'ｈull'):
      self.assertFalse(NvfFormat.is_nvf_part_path(path), path)
    for name in ('engine.main', 'weapon.left', 'dock.aft', 'a.b.c', 'sensor_2.top_left', 'pivots.x'):
      self.assertTrue(NvfFormat.is_nvf_hardpoint_name(name), name)
    for name in ('', 'engine', 'engine.', '.main', 'engine..main', 'pivot.main', 'Engine.main', 'engine/main',
                 'engine.١'):
      self.assertFalse(NvfFormat.is_nvf_hardpoint_name(name), name)
    hardpoint = NvfHardpoint('sensor.top.left', 0, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), False)
    self.assertEqual('sensor', NvfFormat.hardpoint_type(hardpoint), 'the first segment is the type')

  # R14's record, as NeuronCore's VoxelRecord.h packs it.
  def test_packs_voxel_records_as_r14(self):
    self.assertEqual(0x0F030201, pack_voxel_record(1, 2, 3, 15))
    self.assertEqual((1, 2, 3, 15), NvfFormat.unpack_voxel_record(0x0F030201))
    self.assertEqual(0, pack_voxel_record(255, 255, 255, 15) >> 28, 'bits 28-31 stay zero')
    self.assertEqual(pack_voxel_record(0, 0, 0, 1), pack_voxel_record(0, 0, 0, 17), 'the color keeps to its bits')

  # §9: the committed assets read, and write back as they are. NvfImport wrote them, so the two writers agree on
  # files larger than the golden one.
  def test_reads_and_writes_the_assets(self):
    assets = sorted(find_repository_file(GOLDEN_NVF_PATH).parents[2].joinpath('GameData').glob('*.nvf'))
    self.assertGreaterEqual(len(assets), 3, 'GameData holds the three converted assets')
    for asset in assets:
      data = asset.read_bytes()
      model = self.expect_accepted(data, asset.name)
      self.assertEqual([], model.unknown_chunks, asset.name)
      self.assertEqual(data, NvfFormat.serialize_nvf_model(model), f'{asset.name}, read, then written')


if __name__ == '__main__':
  unittest.main()
