# ADR-019 — The Neuron Voxel Format as it is read and written

**Status:** accepted, 2026-09-28 · **Lands with:** N-M1 of [`Design/NeuronVoxelFormat.md`](../NeuronVoxelFormat.md) (§10), and its Python twin with N-M3 · **Implements:** that design's §4, and settles what §4 left open, each point written back into §4 in the same commit

## Context

NVF §4 specifies the file: a header, five chunks, fixed-size records and a list of refusals by name. N-M1 builds the C++ reader, writer and validation in `NeuronCore`, with a golden file that N-M3's Python must reproduce byte for byte (N7). Writing the code turned up places where §4 left a choice open. A choice two implementations make differently is a file one of them refuses, so each is made here, once.

The owner answered three questions on 2026-09-28 (§11). A part's default pivot is its geometric centre, `size / 2`: §4.3's ⌊size / 2⌋ was the corner of MagicaVoxel's pivot voxel, half a voxel off-centre on every odd axis. NVF's ADRs are ADR-019 and ADR-020, reserved because the space scene takes numbers in parallel and S-M4 had already taken ADR-018. And an authored hardpoint that shares a marker's name stays a refusal, which ADR-020 records with the importer.

## Decision

**The records are structs, copied as they lie.** `NvfFileHeader`, `NvfChunkHeader`, `NvfPaletteRecord`, `NvfPartRecord` and `NvfHardpointRecord` in `NeuronCore/NvfModel.h` carry `static_assert`s on their size and on every offset §4.2 and §4.3 give, as R16 holds a layout shared with HLSL. The reader and writer copy them whole, and a `static_assert` requires a little-endian machine, which x64 and ARM64 both are.

**The model.** `ParseNvfModel` returns an `NvfModel`: the palette as the `.vox` reader's `PaletteEntry`, the parts, the records, the hardpoints, and the ids of any chunks it skipped. `HardpointType` gives a hardpoint's type, its name's first segment. `LoadNvfModel` reads a file, `SerializeNvfModel` writes the bytes, and `SaveNvfModel` writes a temporary file beside the target and renames it over it, so a failure never leaves half a file.

**What §4 left open:**

| # | Question | Answer |
|---|---|---|
| 1 | Refusals §4.4 does not name | `FileNotFound` and `ReadFailed` for loading and `WriteFailed` for saving, as `VoxError` has. `TranslationOutOfRange` for a part whose origin in model space, the sum of the translations from part 0 down to it, leaves ±2²¹ on any axis (below). |
| 2 | Why ±2²¹ | Every `.vox` the reader accepts places its models within ±(2²⁰ + 128) of the origin: `MAX_TRANSLATION`, plus half the largest model. So every import passes. Every voxel centre stays exact in single precision, below 2²³, and no origin or translation a consumer sums along the tree leaves `int32`. |
| 3 | `ReservedBitsSet` against `MalformedChunk` | `ReservedBitsSet` is a voxel record with bits 28–31 set. Every other nonzero reserved field, and every flag bit §4.3 does not define, is `MalformedChunk`. |
| 4 | Sizes and limits | An extent of 0 is `MalformedChunk` and one above 256 is `PartTooLarge`, as the `.vox` reader splits them. A count beyond §4.4's limits is `MalformedChunk`. A file with no part is `BadPartTree`: it has no root. |
| 5 | A part's path against its parent | §4.3 calls the name "the full path". Part 0's path is one segment, and every other part's is its parent's path plus one segment; anything else is `BadPartTree`. |
| 6 | `pivot` | It is reserved as a hardpoint type only. A part may be named `pivot`. |
| 7 | Strings | STRS is empty or ends in a NUL, and all of it is well-formed UTF-8 (the Unicode Standard's Table 3-7), else `BadString`. A name is referred to at a string's first byte. A string nothing refers to is allowed, since a newer chunk may refer to it. |
| 8 | Unknown chunks | They may lie anywhere after the header, and their framing is checked like any chunk's: a reserved field of 0 and zero padding. The five known chunks appear once each, in order; one that appears again, or before one it should follow, is `ChunkOutOfOrder`. The reader lists the ids it skipped, and a tool that rewrites a file refuses a model with any (§4.6). |
| 9 | Minor versions | Any minor version of major version 1 is read. |
| 10 | The writer's order | Hardpoints in name order, by the bytes of their ASCII names, so that `NvfImport`'s merge and Blender's export write the same bytes for the same hardpoints. Without it, `--check` would fail after every export. STRS's first-use order follows from it. Parts are written in the order the model gives; the importer's order is ADR-020's. |
| 11 | What the writer may write | Nothing the reader refuses, and nothing it reads back otherwise. `SerializeNvfModel` reads its own bytes back and returns the reader's refusal, by the same name. It first refuses an extent that the file's 16 bits would wrap. N-M3 added the last check: a name the reader gives back otherwise than it was written is `BadString` (below). |

**The order of checking.** Both implementations check in one order and report the first failure, so that a file with two faults gets the same name from each:

1. **Header:** `NotAnNvfFile` (fewer than 4 bytes, or not `NVF `), `Truncated` (fewer than 16), `UnsupportedVersion`, `MalformedChunk` (reserved).
2. **Framing, chunk by chunk in file order:** `Truncated` (a header, or a content and its padding, cut short), then `MalformedChunk` (a reserved field, nonzero padding). After the last chunk, a byte left over is `MalformedChunk`.
3. **Order:** `ChunkOutOfOrder`, then `MissingChunk`.
4. **STRS:** `MalformedChunk` (a count other than its size, more than 64 KiB), then `BadString` (no final NUL, not UTF-8).
5. **PALT:** `MalformedChunk` (not sixteen 16-byte records). Then, entry by entry: `MalformedChunk` (flags), `NotFinite` (emit, flux).
6. **PART:** `MalformedChunk` (a size other than 48 bytes a record, more than 1,024), `BadPartTree` (no part). Then, part by part: `BadString` (its name), `BadPartTree` (its parent, its path), `DuplicateName`, `MalformedChunk` (an extent of 0), `PartTooLarge`, `MalformedChunk` (flags), `TranslationOutOfRange`, `NotFinite` (pivot), `BadVoxelRange` (a first voxel other than the one after the last part's, a count of 0).
7. **VOXL:** `MalformedChunk` (a size other than 4 bytes a record), `BadVoxelRange` (a count other than the parts'). Then, part by part: `ReservedBitsSet`, `VoxelOutOfBounds`, `DuplicateVoxel`.
8. **HPNT:** `MalformedChunk` (a size other than 48 bytes a record, more than 4,096). Then, hardpoint by hardpoint: `BadString`, `DuplicateName`, `BadHardpointPart`, `MalformedChunk` (flags, reserved), `NotFinite` (position, rotation), `NotUnitRotation`.

**A unit rotation** is what `IsUnitRotation` accepts, the test the protocol shares (ADR-015): |‖q‖ − 1| ≤ 10⁻⁴ in single precision, and w ≥ 0. NvfFormat.py computes the length in double precision. The two can disagree only about a quaternion within rounding of the tolerance, which no tool writes.

**A name read back otherwise (N-M3).** Writing the Python twin turned up a model that `SerializeNvfModel` wrote and read back changed: a name with a NUL in it, which the reader takes as the part before the NUL, so that `hull\0x` came back as `hull`. Both writers now compare the names they read back with those they wrote, once the reader has accepted the file, and refuse a difference as `BadString`. The check comes after the reader's, so that a model with another fault still gets the reader's name for it.

**The Python twin (N-M3).** `Tools/Blender/NeuronVoxelFormat/NvfFormat.py` is `NvfModel.cpp` in Python, function for function (`parse_nvf_model`, `load_nvf_model`, `serialize_nvf_model`, `save_nvf_model`), and it checks in the order above. What Python can hold and the file cannot is settled so that the reader still decides:
- a float is stored as single precision rounds it (`as_single`), and a finite value beyond single precision's range becomes an infinity, which the reader refuses as `NotFinite` in its place;
- a name is encoded as UTF-8 with any surrogate kept, so that a string no UTF-8 encoder would write reaches the reader and is refused as `BadString` in its place;
- a value its record's field cannot hold at all, such as a negative index, raises Python's `struct.error`. C++'s types rule that value out, so it is a caller's mistake rather than a refusal;
- a file that cannot be opened is `FileNotFound`, as it is for `std::ifstream`.

Its tests in `Tests/NvfFormatTests.py` are `NvfModelTests`', case for case and named alike, and corrupt the golden file into the same refusals. They also read `NvfError`'s names from `NvfModel.h`, so that the two lists cannot drift, and they write each of `GameData`'s `.nvf` files back byte for byte.

**The golden file.** `Tools/Golden/Golden.nvf`, 848 bytes, holds the model `NeuronCoreTests/NvfGolden.cpp` builds field by field:
- three parts two levels deep, with negative translations, default pivots and one authored pivot;
- all sixteen palette entries, one translucent, three emissive, and one diffuse entry that still carries an emit and a flux, as the `.vox` reader keeps them;
- a voxel in every entry, and one at each part's far corner;
- four hardpoints, two from the `.vox` and one of three segments, turned by 0, 90, 180 and 30 degrees, listed out of name order.

`Tools/` is a new top-level folder. It holds data and, from N-M3, Python, never C++; ADR-020 records it with the importer's layout.

## Figures

**The golden file's provenance.** `SerializeNvfModel` wrote it, from `GoldenNvfModel`. A throwaway script then decoded it from §4.2's and §4.3's tables alone, sharing no code with `NvfModel.cpp`, and read back every field as built:
- the chunks at offsets 16, 128, 400, 560 and 640;
- STRS's 85 bytes in first-use order;
- the hardpoints in name order.

`NvfModelTests::LaysTheGoldenFileOutAsSpecified` checks the same offsets without the reader.

**The suite.** `NvfModelTests`, 26 tests:
- the golden file written, read, laid out as §4 specifies, and round-tripped;
- hardpoints written in name order, whatever order the model lists them in;
- unknown chunks skipped wherever they lie, and listed;
- every `NvfError` from a corrupt file, and every truncation of a valid one;
- the order of checking, over nine files with two faults each;
- the writer's refusals, and saving and loading, over an existing file and into a folder that does not exist.

**Measured, natively.** `NeuronCoreTests`, built by GCC 13.3 at `-O1` for x86-64 against a throwaway stand-in for the test framework: all 169 tests pass, the 143 before N-M1 and its 26.

**In CI.** MSVC 14.51.36231's Debug|x64 build:
- run 36460843922, on N-M1's commit: all 218 tests of the four suites pass, and clang-tidy is clean over the tree's 103 translation units;
- run 36462201748, after main's S-M4 was merged in: all 242 pass, and clang-tidy is clean over 110.

**Mutations.** Seven faults were put into the reader and writer by hand, one at a time, and each failed at least one test before it was taken out:
- padding left unchecked;
- hardpoints left unsorted;
- a part's pivot checked before its name;
- STRS's UTF-8 left unchecked;
- surrogates accepted;
- a negative w accepted;
- origins not summed down the tree.

## What this forecloses

- **A second layout.** Records are read and written through the structs, which the `static_assert`s hold to §4.
- **A writer that emits another order, or a file the reader refuses.** A second NVF writer sorts its hardpoints by name and checks its output as `SerializeNvfModel` does.
- **An implementation that checks in another order.** NvfFormat.py follows the order above, and its tests pin the same files to the same names.
- **A big-endian host,** without a byte-swapping reader.
- **Translations beyond ±2²¹.** A model that needs them needs a different placement scheme, and a new major version.
