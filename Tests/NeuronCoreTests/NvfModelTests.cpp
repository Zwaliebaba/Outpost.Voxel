#include "pch.h"

#include "NvfGolden.h"
#include "NvfModel.h"
#include "RepositoryFile.h"
#include "VoxFile.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::NvfError;
using NeuronCore::NvfHardpointRecord;
using NeuronCore::NvfModel;
using NeuronCore::NvfPaletteRecord;
using NeuronCore::NvfPartRecord;

constexpr float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();
constexpr float POSITIVE_INFINITY = std::numeric_limits<float>::infinity();

// The golden file's strings, in first-use order: part paths in part order, then hardpoint names in name order.
constexpr std::string_view GOLDEN_STRINGS{"hull\0hull/turret\0hull/turret/barrel\0dock.aft\0engine.main\0sensor.top.left\0weapon.main\0",
                                          85};

// Where each chunk of the golden file begins: its header, then its content at the next 16 bytes.
constexpr std::array<std::size_t, 5> GOLDEN_CHUNK_OFFSETS{16, 128, 400, 560, 640};
constexpr std::size_t GOLDEN_FILE_BYTES = 848;

// One chunk as the file frames it, free to break any rule: the tests' way to build the files the writer refuses to
// write.
struct RawChunk
{
  std::string id;
  std::uint32_t elementCount;
  Bytes content;
  std::uint32_t reserved;
};

struct RawFile
{
  NeuronCore::NvfFileHeader header;
  std::vector<RawChunk> chunks;
  std::optional<std::uint32_t> chunkCount; // the header's count, when it should disagree with the chunks
  std::uint8_t padding;                    // what each chunk's content is padded with
};

[[nodiscard]] std::wstring Widen(std::string_view _text)
{
  return {_text.begin(), _text.end()};
}

[[nodiscard]] Bytes GoldenBytes()
{
  const auto bytes = NeuronCore::SerializeNvfModel(GoldenNvfModel());
  if (!bytes)
  {
    Assert::Fail(std::format(L"the golden model was refused: {}", Widen(NeuronCore::NvfErrorName(bytes.error()))).c_str());
  }
  return *bytes;
}

template <typename Record> [[nodiscard]] Record Read(std::span<const std::uint8_t> _bytes, std::size_t _offset)
{
  Record record{};
  std::memcpy(&record, _bytes.data() + _offset, sizeof(Record));
  return record;
}

[[nodiscard]] RawFile Split(std::span<const std::uint8_t> _bytes)
{
  RawFile file{Read<NeuronCore::NvfFileHeader>(_bytes, 0), {}, std::nullopt, 0};
  std::size_t offset = sizeof(NeuronCore::NvfFileHeader);
  for (std::uint32_t i = 0; i < file.header.chunkCount; ++i)
  {
    const auto header = Read<NeuronCore::NvfChunkHeader>(_bytes, offset);
    offset += sizeof(header);
    file.chunks.push_back(
      {std::string(header.id.begin(), header.id.end()), header.elementCount,
       Bytes(_bytes.begin() + static_cast<std::ptrdiff_t>(offset), _bytes.begin() + static_cast<std::ptrdiff_t>(offset + header.sizeBytes)),
       header.reserved});
    offset += (std::size_t{header.sizeBytes} + 15) / 16 * 16;
  }
  return file;
}

[[nodiscard]] Bytes Assemble(const RawFile& _file)
{
  NeuronCore::NvfFileHeader header = _file.header;
  header.chunkCount = _file.chunkCount.value_or(static_cast<std::uint32_t>(_file.chunks.size()));
  Bytes bytes(sizeof(header));
  std::memcpy(bytes.data(), &header, sizeof(header));
  for (const RawChunk& chunk : _file.chunks)
  {
    NeuronCore::NvfChunkHeader chunkHeader{};
    std::copy_n(chunk.id.begin(), chunkHeader.id.size(), chunkHeader.id.begin());
    chunkHeader.sizeBytes = static_cast<std::uint32_t>(chunk.content.size());
    chunkHeader.elementCount = chunk.elementCount;
    chunkHeader.reserved = chunk.reserved;
    const auto* first = reinterpret_cast<const std::uint8_t*>(&chunkHeader);
    bytes.insert(bytes.end(), first, first + sizeof(chunkHeader));
    bytes.insert(bytes.end(), chunk.content.begin(), chunk.content.end());
    bytes.resize((bytes.size() + 15u) / 16u * 16u, _file.padding);
  }
  return bytes;
}

[[nodiscard]] RawFile GoldenRaw()
{
  return Split(GoldenBytes());
}

[[nodiscard]] RawChunk& ChunkNamed(RawFile& _file, std::string_view _id)
{
  const auto found = std::ranges::find(_file.chunks, _id, &RawChunk::id);
  Assert::IsTrue(found != _file.chunks.end(), L"a chunk the test expects");
  return *found;
}

template <typename Record> [[nodiscard]] Record GetRecord(const RawChunk& _chunk, std::size_t _index)
{
  return Read<Record>(_chunk.content, _index * sizeof(Record));
}

template <typename Record> void SetRecord(RawChunk& _chunk, std::size_t _index, const Record& _record)
{
  std::memcpy(_chunk.content.data() + _index * sizeof(Record), &_record, sizeof(Record));
}

// Applies _change to record _index of chunk _id.
template <typename Record, typename Change> [[nodiscard]] Bytes WithRecord(std::string_view _id, std::size_t _index, Change _change)
{
  RawFile file = GoldenRaw();
  RawChunk& chunk = ChunkNamed(file, _id);
  Record record = GetRecord<Record>(chunk, _index);
  _change(record);
  SetRecord(chunk, _index, record);
  return Assemble(file);
}

template <typename Change> [[nodiscard]] Bytes WithPart(std::size_t _index, Change _change)
{
  return WithRecord<NvfPartRecord>("PART", _index, _change);
}

template <typename Change> [[nodiscard]] Bytes WithHardpoint(std::size_t _index, Change _change)
{
  return WithRecord<NvfHardpointRecord>("HPNT", _index, _change);
}

template <typename Change> [[nodiscard]] Bytes WithPalette(std::size_t _index, Change _change)
{
  return WithRecord<NvfPaletteRecord>("PALT", _index, _change);
}

// The golden file with voxel record _index replaced.
[[nodiscard]] Bytes WithVoxel(std::size_t _index, std::uint32_t _record)
{
  return WithRecord<std::uint32_t>("VOXL", _index, [_record](std::uint32_t& _value) { _value = _record; });
}

// The golden file with its strings rebuilt from _partPaths and _hardpointNames, in first-use order, and every record's
// offset pointing at its own.
[[nodiscard]] Bytes WithNames(const std::vector<std::string>& _partPaths, const std::vector<std::string>& _hardpointNames)
{
  RawFile file = GoldenRaw();
  Bytes strings;
  std::map<std::string, std::uint32_t> offsets;
  const auto offsetOf = [&strings, &offsets](const std::string& _name)
  {
    const auto [found, inserted] = offsets.try_emplace(_name, static_cast<std::uint32_t>(strings.size()));
    if (inserted)
    {
      strings.insert(strings.end(), _name.begin(), _name.end());
      strings.push_back(0);
    }
    return found->second;
  };
  RawChunk& parts = ChunkNamed(file, "PART");
  for (std::size_t i = 0; i < _partPaths.size(); ++i)
  {
    NvfPartRecord record = GetRecord<NvfPartRecord>(parts, i);
    record.nameOffset = offsetOf(_partPaths[i]);
    SetRecord(parts, i, record);
  }
  RawChunk& hardpoints = ChunkNamed(file, "HPNT");
  for (std::size_t i = 0; i < _hardpointNames.size(); ++i)
  {
    NvfHardpointRecord record = GetRecord<NvfHardpointRecord>(hardpoints, i);
    record.nameOffset = offsetOf(_hardpointNames[i]);
    SetRecord(hardpoints, i, record);
  }
  RawChunk& stringChunk = ChunkNamed(file, "STRS");
  stringChunk.content = strings;
  stringChunk.elementCount = static_cast<std::uint32_t>(strings.size());
  return Assemble(file);
}

[[nodiscard]] std::vector<std::string> GoldenPaths()
{
  return {"hull", "hull/turret", "hull/turret/barrel"};
}

[[nodiscard]] std::vector<std::string> GoldenNames()
{
  return {"dock.aft", "engine.main", "sensor.top.left", "weapon.main"};
}

// The golden file with STRS's content replaced by _content, its count agreeing.
[[nodiscard]] Bytes WithStrings(const Bytes& _content)
{
  RawFile file = GoldenRaw();
  RawChunk& strings = ChunkNamed(file, "STRS");
  strings.content = _content;
  strings.elementCount = static_cast<std::uint32_t>(_content.size());
  return Assemble(file);
}

void ExpectRefusal(NvfError _expected, std::span<const std::uint8_t> _bytes, const std::wstring& _case)
{
  const auto model = NeuronCore::ParseNvfModel(_bytes);
  Assert::IsFalse(model.has_value(), (_case + L": the file was accepted").c_str());
  Assert::AreEqual(NeuronCore::NvfErrorName(_expected), NeuronCore::NvfErrorName(model.error()), _case.c_str());
}

[[nodiscard]] NvfModel ExpectAccepted(std::span<const std::uint8_t> _bytes, const std::wstring& _case)
{
  auto model = NeuronCore::ParseNvfModel(_bytes);
  if (!model)
  {
    Assert::Fail(std::format(L"{}: refused with {}", _case, Widen(NeuronCore::NvfErrorName(model.error()))).c_str());
  }
  return std::move(*model);
}

// Bit for bit, so that a NaN or a negative zero cannot pass for what the file holds.
void AreSameFloat(float _expected, float _actual, const std::wstring& _what)
{
  Assert::AreEqual(std::bit_cast<std::uint32_t>(_expected), std::bit_cast<std::uint32_t>(_actual), _what.c_str());
}

void AreEqualModels(const NvfModel& _expected, const NvfModel& _actual)
{
  for (std::size_t i = 0; i < _expected.palette.size(); ++i)
  {
    const NeuronCore::PaletteEntry& expected = _expected.palette[i];
    const NeuronCore::PaletteEntry& actual = _actual.palette[i];
    const std::wstring what = std::format(L"palette entry {}", i);
    Assert::IsTrue(expected.red == actual.red && expected.green == actual.green && expected.blue == actual.blue &&
                     expected.alpha == actual.alpha && expected.emissive == actual.emissive,
                   what.c_str());
    AreSameFloat(expected.emit, actual.emit, what + L" emit");
    AreSameFloat(expected.flux, actual.flux, what + L" flux");
  }

  Assert::AreEqual(_expected.parts.size(), _actual.parts.size(), L"parts");
  for (std::size_t i = 0; i < _expected.parts.size(); ++i)
  {
    const NeuronCore::NvfPart& expected = _expected.parts[i];
    const NeuronCore::NvfPart& actual = _actual.parts[i];
    const std::wstring what = std::format(L"part {}", i);
    Assert::AreEqual(expected.path, actual.path, what.c_str());
    Assert::AreEqual(expected.parent, actual.parent, what.c_str());
    Assert::IsTrue(expected.size.x == actual.size.x && expected.size.y == actual.size.y && expected.size.z == actual.size.z, what.c_str());
    Assert::IsTrue(expected.translation.x == actual.translation.x && expected.translation.y == actual.translation.y &&
                     expected.translation.z == actual.translation.z,
                   what.c_str());
    AreSameFloat(expected.pivot.x, actual.pivot.x, what + L" pivot");
    AreSameFloat(expected.pivot.y, actual.pivot.y, what + L" pivot");
    AreSameFloat(expected.pivot.z, actual.pivot.z, what + L" pivot");
    Assert::AreEqual(expected.pivotAuthored, actual.pivotAuthored, what.c_str());
    Assert::AreEqual(expected.firstVoxel, actual.firstVoxel, what.c_str());
    Assert::AreEqual(expected.voxelCount, actual.voxelCount, what.c_str());
  }
  Assert::IsTrue(_expected.records == _actual.records, L"records");

  // The file holds hardpoints in name order, whatever order the model listed them in.
  std::vector<NeuronCore::NvfHardpoint> expectedHardpoints = _expected.hardpoints;
  std::ranges::stable_sort(expectedHardpoints, {}, &NeuronCore::NvfHardpoint::name);
  Assert::AreEqual(expectedHardpoints.size(), _actual.hardpoints.size(), L"hardpoints");
  for (std::size_t i = 0; i < expectedHardpoints.size(); ++i)
  {
    const NeuronCore::NvfHardpoint& expected = expectedHardpoints[i];
    const NeuronCore::NvfHardpoint& actual = _actual.hardpoints[i];
    const std::wstring what = std::format(L"hardpoint {}", i);
    Assert::AreEqual(expected.name, actual.name, what.c_str());
    Assert::AreEqual(expected.part, actual.part, what.c_str());
    AreSameFloat(expected.position.x, actual.position.x, what + L" position");
    AreSameFloat(expected.position.y, actual.position.y, what + L" position");
    AreSameFloat(expected.position.z, actual.position.z, what + L" position");
    AreSameFloat(expected.rotation.x, actual.rotation.x, what + L" rotation");
    AreSameFloat(expected.rotation.y, actual.rotation.y, what + L" rotation");
    AreSameFloat(expected.rotation.z, actual.rotation.z, what + L" rotation");
    AreSameFloat(expected.rotation.w, actual.rotation.w, what + L" rotation");
    Assert::AreEqual(expected.fromVox, actual.fromVox, what.c_str());
  }
}

// The first offset at which _actual differs from _expected, or their shorter length.
void AreSameBytes(std::span<const std::uint8_t> _expected, std::span<const std::uint8_t> _actual, const wchar_t* _what)
{
  const auto [expected, actual] = std::ranges::mismatch(_expected, _actual);
  if (expected != _expected.end() || actual != _actual.end())
  {
    Assert::Fail(std::format(L"{}: {} bytes against {}, first differing at byte {}", _what, _actual.size(), _expected.size(),
                             expected - _expected.begin())
                   .c_str());
  }
}

} // namespace

TEST_CLASS(NvfModelTests)
{
public:
  // Design/Archive/NeuronVoxelFormat.md §9: the model built in code serializes to the committed golden file, byte for byte.
  // NvfFormat.py's tests read and write the same file, so if either implementation drifts, its test fails.
  TEST_METHOD(WritesTheGoldenFile)
  {
    AreSameBytes(ReadRepositoryFile(GOLDEN_NVF_PATH), GoldenBytes(), L"the golden model, serialized");
  }

  TEST_METHOD(ReadsTheGoldenFile)
  {
    const NvfModel model = ExpectAccepted(ReadRepositoryFile(GOLDEN_NVF_PATH), L"the golden file");
    AreEqualModels(GoldenNvfModel(), model);
    Assert::IsTrue(model.unknownChunks.empty(), L"no unknown chunk");
    const std::array<std::string_view, 4> types{"dock", "engine", "sensor", "weapon"};
    for (std::size_t i = 0; i < types.size(); ++i)
    {
      Assert::IsTrue(NeuronCore::HardpointType(model.hardpoints[i]) == types[i], L"the type is the name's first segment");
    }
  }

  // The golden file against §4's tables, read at the offsets they give rather than through the reader.
  TEST_METHOD(LaysTheGoldenFileOutAsSpecified)
  {
    const Bytes bytes = ReadRepositoryFile(GOLDEN_NVF_PATH);
    Assert::AreEqual(GOLDEN_FILE_BYTES, bytes.size(), L"file size");
    Assert::IsTrue(std::memcmp(bytes.data(), "NVF ", 4) == 0, L"magic");
    Assert::AreEqual(std::uint16_t{1}, Read<std::uint16_t>(bytes, 4), L"versionMajor");
    Assert::AreEqual(std::uint16_t{0}, Read<std::uint16_t>(bytes, 6), L"versionMinor");
    Assert::AreEqual(5u, Read<std::uint32_t>(bytes, 8), L"chunkCount");
    Assert::AreEqual(0u, Read<std::uint32_t>(bytes, 12), L"reserved");

    const std::array<const char*, 5> ids{"STRS", "PALT", "PART", "VOXL", "HPNT"};
    const std::array<std::uint32_t, 5> sizes{85, 256, 144, 64, 192};
    const std::array<std::uint32_t, 5> counts{85, 16, 3, 16, 4};
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
      const std::size_t offset = GOLDEN_CHUNK_OFFSETS[i];
      const std::wstring what = Widen(ids[i]);
      Assert::IsTrue(std::memcmp(bytes.data() + offset, ids[i], 4) == 0, what.c_str());
      Assert::AreEqual(sizes[i], Read<std::uint32_t>(bytes, offset + 4), (what + L" sizeBytes").c_str());
      Assert::AreEqual(counts[i], Read<std::uint32_t>(bytes, offset + 8), (what + L" elementCount").c_str());
      Assert::AreEqual(0u, Read<std::uint32_t>(bytes, offset + 12), (what + L" reserved").c_str());
      Assert::AreEqual(std::size_t{0}, (offset + 16) % 16, (what + L" content is 16-byte aligned").c_str());
      const std::size_t end = offset + 16 + sizes[i];
      const std::size_t next = i + 1 < ids.size() ? GOLDEN_CHUNK_OFFSETS[i + 1] : GOLDEN_FILE_BYTES;
      Assert::AreEqual((end + 15) / 16 * 16, next, (what + L" is padded to the next 16 bytes").c_str());
      Assert::IsTrue(std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(end), bytes.begin() + static_cast<std::ptrdiff_t>(next),
                                 [](std::uint8_t _byte) { return _byte == 0; }),
                     (what + L" padding is zero").c_str());
    }

    const std::size_t strings = GOLDEN_CHUNK_OFFSETS[0] + 16;
    Assert::IsTrue(std::string_view(reinterpret_cast<const char*>(bytes.data()) + strings, GOLDEN_STRINGS.size()) == GOLDEN_STRINGS,
                   L"STRS in first-use order");

    // PALT entry 10, the light blue that glows: bytes, flags, emit and flux.
    const std::size_t lightBlue = GOLDEN_CHUNK_OFFSETS[1] + 16 + std::size_t{9} * 16;
    Assert::IsTrue(bytes[lightBlue] == 85 && bytes[lightBlue + 1] == 85 && bytes[lightBlue + 2] == 255 && bytes[lightBlue + 3] == 255,
                   L"entry 10's color");
    Assert::AreEqual(1u, Read<std::uint32_t>(bytes, lightBlue + 4), L"entry 10 is emissive");
    Assert::AreEqual(0.6f, Read<float>(bytes, lightBlue + 8), L"entry 10's emit");
    Assert::AreEqual(2.0f, Read<float>(bytes, lightBlue + 12), L"entry 10's flux");

    // PART record 1, hull/turret, field by field at §4.3's offsets.
    const std::size_t turret = GOLDEN_CHUNK_OFFSETS[2] + 16 + 48;
    Assert::AreEqual(5u, Read<std::uint32_t>(bytes, turret + 0), L"nameOffset");
    Assert::AreEqual(0u, Read<std::uint32_t>(bytes, turret + 4), L"parentIndex");
    Assert::AreEqual(std::uint16_t{3}, Read<std::uint16_t>(bytes, turret + 8), L"sizeVoxels x");
    Assert::AreEqual(std::uint16_t{2}, Read<std::uint16_t>(bytes, turret + 10), L"sizeVoxels y");
    Assert::AreEqual(std::uint16_t{3}, Read<std::uint16_t>(bytes, turret + 12), L"sizeVoxels z");
    Assert::AreEqual(std::uint16_t{1}, Read<std::uint16_t>(bytes, turret + 14), L"flags: PivotAuthored");
    Assert::AreEqual(1, Read<std::int32_t>(bytes, turret + 16), L"translation x");
    Assert::AreEqual(3, Read<std::int32_t>(bytes, turret + 20), L"translation y");
    Assert::AreEqual(2, Read<std::int32_t>(bytes, turret + 24), L"translation z");
    Assert::AreEqual(1.5f, Read<float>(bytes, turret + 28), L"pivot x");
    Assert::AreEqual(0.0f, Read<float>(bytes, turret + 32), L"pivot y");
    Assert::AreEqual(1.5f, Read<float>(bytes, turret + 36), L"pivot z");
    Assert::AreEqual(8u, Read<std::uint32_t>(bytes, turret + 40), L"firstVoxel");
    Assert::AreEqual(4u, Read<std::uint32_t>(bytes, turret + 44), L"voxelCount");
    Assert::AreEqual(0xFFFFFFFFu, Read<std::uint32_t>(bytes, GOLDEN_CHUNK_OFFSETS[2] + 16 + 4), L"part 0 has no parent");

    // VOXL's last record: the barrel's (0, 0, 3) in entry 16.
    Assert::AreEqual(NeuronCore::PackVoxelRecord({0, 0, 3, 15}),
                     Read<std::uint32_t>(bytes, GOLDEN_CHUNK_OFFSETS[3] + 16 + std::size_t{15} * 4), L"the last record");

    // HPNT in name order; record 3 is weapon.main, field by field.
    const std::size_t hardpoints = GOLDEN_CHUNK_OFFSETS[4] + 16;
    const std::array<std::uint32_t, 4> nameOffsets{36, 45, 57, 73};
    for (std::size_t i = 0; i < nameOffsets.size(); ++i)
    {
      Assert::AreEqual(nameOffsets[i], Read<std::uint32_t>(bytes, hardpoints + i * 48), L"hardpoints in name order");
    }
    const std::size_t weapon = hardpoints + std::size_t{3} * 48;
    Assert::AreEqual(2u, Read<std::uint32_t>(bytes, weapon + 4), L"partIndex");
    Assert::AreEqual(0u, Read<std::uint32_t>(bytes, weapon + 8), L"flags: not from the .vox");
    Assert::AreEqual(4.0f, Read<float>(bytes, weapon + 20), L"position z");
    Assert::AreEqual(0.25881904f, Read<float>(bytes, weapon + 28), L"rotation y");
    Assert::AreEqual(0.9659258f, Read<float>(bytes, weapon + 36), L"rotation w");
    Assert::AreEqual(0u, Read<std::uint32_t>(bytes, weapon + 40) | Read<std::uint32_t>(bytes, weapon + 44), L"reserved");
    Assert::AreEqual(1u, Read<std::uint32_t>(bytes, hardpoints + 48 + 8), L"engine.main comes from the .vox");
  }

  TEST_METHOD(RoundTripsItsOwnBytes)
  {
    const Bytes golden = ReadRepositoryFile(GOLDEN_NVF_PATH);
    const auto bytes = NeuronCore::SerializeNvfModel(ExpectAccepted(golden, L"the golden file"));
    Assert::IsTrue(bytes.has_value(), L"the model read back is written again");
    AreSameBytes(golden, *bytes, L"read, then written");
  }

  // §4.3: two writers produce the same bytes. Strings follow first use, and hardpoints are written in name order, so
  // the order a tool listed them in cannot change the file.
  TEST_METHOD(WritesHardpointsInNameOrder)
  {
    NvfModel model = GoldenNvfModel();
    std::ranges::reverse(model.hardpoints);
    const auto reversed = NeuronCore::SerializeNvfModel(model);
    Assert::IsTrue(reversed.has_value(), L"the reversed model");
    AreSameBytes(GoldenBytes(), *reversed, L"hardpoints listed in reverse");

    std::ranges::sort(model.hardpoints, {}, &NeuronCore::NvfHardpoint::part);
    const auto byPart = NeuronCore::SerializeNvfModel(model);
    Assert::IsTrue(byPart.has_value(), L"the model sorted by part");
    AreSameBytes(GoldenBytes(), *byPart, L"hardpoints listed by part");
  }

  // §4.2, §4.6: a reader of 1.0 skips a chunk it does not know, wherever it lies, reads any minor version, and says what
  // it skipped so that a tool that rewrites the file can refuse it.
  TEST_METHOD(SkipsUnknownChunks)
  {
    RawFile file = GoldenRaw();
    file.header.versionMinor = 7;
    file.chunks.insert(file.chunks.begin(), RawChunk{"NOTE", 3, {1, 2, 3}, 0});
    file.chunks.insert(file.chunks.begin() + 4, RawChunk{"XTRA", 0, {}, 0});
    file.chunks.push_back(RawChunk{std::string("\0\x01zz", 4), 99, Bytes(40, 0xAB), 0});
    const NvfModel model = ExpectAccepted(Assemble(file), L"three unknown chunks");
    AreEqualModels(GoldenNvfModel(), model);
    Assert::IsTrue(model.unknownChunks == std::vector<std::string>{"NOTE", "XTRA", std::string("\0\x01zz", 4)}, L"the chunks skipped");

    // The framing of a chunk the reader does not know is still checked.
    RawFile reserved = GoldenRaw();
    reserved.chunks.push_back(RawChunk{"XTRA", 0, {}, 1});
    ExpectRefusal(NvfError::MalformedChunk, Assemble(reserved), L"an unknown chunk's reserved field");
    RawFile padded = GoldenRaw();
    padded.chunks.push_back(RawChunk{"XTRA", 0, {7}, 0});
    Bytes bytes = Assemble(padded);
    bytes.back() = 1;
    ExpectRefusal(NvfError::MalformedChunk, bytes, L"an unknown chunk's padding");
  }

  TEST_METHOD(RefusesWhatIsNotAnNvfFile)
  {
    ExpectRefusal(NvfError::NotAnNvfFile, Bytes{}, L"empty");
    ExpectRefusal(NvfError::NotAnNvfFile, Bytes{'N', 'V', 'F'}, L"three bytes");
    Bytes lower = GoldenBytes();
    lower[0] = 'n';
    ExpectRefusal(NvfError::NotAnNvfFile, lower, L"nvf in lower case");
    Bytes noSpace = GoldenBytes();
    noSpace[3] = 0;
    ExpectRefusal(NvfError::NotAnNvfFile, noSpace, L"a NUL for the space");
    ExpectRefusal(NvfError::NotAnNvfFile, VoxFile(OneModelChunks({1, 1, 1}, std::array<FileVoxel, 1>{{{0, 0, 0, 1}}}, "0 0 0")),
                  L"a .vox file");
  }

  TEST_METHOD(RefusesUnsupportedVersions)
  {
    for (const std::uint16_t major : {std::uint16_t{0}, std::uint16_t{2}, std::uint16_t{0xFFFF}})
    {
      RawFile file = GoldenRaw();
      file.header.versionMajor = major;
      ExpectRefusal(NvfError::UnsupportedVersion, Assemble(file), std::format(L"version {}.0", major));
    }
    for (const std::uint16_t minor : {std::uint16_t{1}, std::uint16_t{0xFFFF}})
    {
      RawFile file = GoldenRaw();
      file.header.versionMinor = minor;
      AreEqualModels(GoldenNvfModel(), ExpectAccepted(Assemble(file), std::format(L"version 1.{}", minor)));
    }
  }

  TEST_METHOD(RefusesEveryTruncation)
  {
    const Bytes file = GoldenBytes();
    for (std::size_t length = 0; length < file.size(); ++length)
    {
      // Four bytes hold the magic; after it, every cut leaves the header or a chunk short.
      const NvfError expected = length < 4 ? NvfError::NotAnNvfFile : NvfError::Truncated;
      ExpectRefusal(expected, std::span(file).first(length), std::format(L"cut to {} of {} bytes", length, file.size()));
    }
  }

  TEST_METHOD(RefusesMalformedChunks)
  {
    RawFile file = GoldenRaw();
    file.header.reserved = 1;
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"the header's reserved field");

    file = GoldenRaw();
    ChunkNamed(file, "PALT").reserved = 1;
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"a chunk's reserved field");

    Bytes padding = GoldenBytes();
    padding[GOLDEN_CHUNK_OFFSETS[0] + 16 + 85] = 1;
    ExpectRefusal(NvfError::MalformedChunk, padding, L"a padding byte after STRS");

    Bytes trailing = GoldenBytes();
    trailing.push_back(0);
    ExpectRefusal(NvfError::MalformedChunk, trailing, L"a byte after the last chunk");

    file = GoldenRaw();
    file.chunkCount = 4;
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"a chunk count one short");

    file = GoldenRaw();
    ChunkNamed(file, "STRS").elementCount = 84;
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"STRS counting other than its bytes");

    Bytes large(GOLDEN_STRINGS.begin(), GOLDEN_STRINGS.end());
    large.insert(large.end(), NeuronCore::NVF_MAX_STRING_BYTES - large.size(), 'a');
    large.push_back(0);
    ExpectRefusal(NvfError::MalformedChunk, WithStrings(large), L"STRS of 64 KiB and one byte");
    large.resize(NeuronCore::NVF_MAX_STRING_BYTES - 1);
    large.push_back(0);
    static_cast<void>(ExpectAccepted(WithStrings(large), L"STRS of exactly 64 KiB"));

    file = GoldenRaw();
    RawChunk& palette = ChunkNamed(file, "PALT");
    palette.content.resize(15 * sizeof(NvfPaletteRecord));
    palette.elementCount = 15;
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"a palette of 15 entries");
    ChunkNamed(file, "PALT").elementCount = 16;
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"a palette counting 16 in the bytes of 15");

    file = GoldenRaw();
    ChunkNamed(file, "PART").elementCount = 2;
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"PART counting two of three records");

    file = GoldenRaw();
    RawChunk& voxels = ChunkNamed(file, "VOXL");
    voxels.content.pop_back();
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"VOXL a byte short of its count");

    file = GoldenRaw();
    RawChunk& hardpoints = ChunkNamed(file, "HPNT");
    hardpoints.content.push_back(0);
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"HPNT a byte over its count");

    // The limits are checked before anything in the chunk is read, so these records need not be valid.
    file = GoldenRaw();
    RawChunk& manyParts = ChunkNamed(file, "PART");
    manyParts.elementCount = NeuronCore::NVF_MAX_PARTS + 1;
    manyParts.content.assign(std::size_t{manyParts.elementCount} * sizeof(NvfPartRecord), 0);
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"1,025 parts");

    file = GoldenRaw();
    RawChunk& manyHardpoints = ChunkNamed(file, "HPNT");
    manyHardpoints.elementCount = NeuronCore::NVF_MAX_HARDPOINTS + 1;
    manyHardpoints.content.assign(std::size_t{manyHardpoints.elementCount} * sizeof(NvfHardpointRecord), 0);
    ExpectRefusal(NvfError::MalformedChunk, Assemble(file), L"4,097 hardpoints");

    ExpectRefusal(NvfError::MalformedChunk, WithPalette(3, [](NvfPaletteRecord& _record) { _record.flags = 2; }), L"a palette flag bit 1");
    ExpectRefusal(NvfError::MalformedChunk, WithPalette(9, [](NvfPaletteRecord& _record) { _record.flags = 0x80000001u; }),
                  L"a palette flag bit 31");
    ExpectRefusal(NvfError::MalformedChunk, WithPart(1, [](NvfPartRecord& _record) { _record.flags = 3; }), L"a part flag bit 1");
    ExpectRefusal(NvfError::MalformedChunk, WithPart(2, [](NvfPartRecord& _record) { _record.sizeVoxels[1] = 0; }),
                  L"a part 0 voxels tall");
    ExpectRefusal(NvfError::MalformedChunk, WithHardpoint(0, [](NvfHardpointRecord& _record) { _record.flags = 2; }),
                  L"a hardpoint flag bit 1");
    ExpectRefusal(NvfError::MalformedChunk, WithHardpoint(1, [](NvfHardpointRecord& _record) { _record.reserved[0] = 1; }),
                  L"a hardpoint's first reserved word");
    ExpectRefusal(NvfError::MalformedChunk, WithHardpoint(3, [](NvfHardpointRecord& _record) { _record.reserved[1] = 1; }),
                  L"a hardpoint's second reserved word");
  }

  TEST_METHOD(RefusesMissingAndMisorderedChunks)
  {
    for (const std::string_view id : {"STRS", "PALT", "PART", "VOXL", "HPNT"})
    {
      RawFile file = GoldenRaw();
      std::erase_if(file.chunks, [id](const RawChunk& _chunk) { return _chunk.id == id; });
      ExpectRefusal(NvfError::MissingChunk, Assemble(file), L"without " + Widen(id));
    }

    RawFile swapped = GoldenRaw();
    std::swap(swapped.chunks[0], swapped.chunks[1]);
    ExpectRefusal(NvfError::ChunkOutOfOrder, Assemble(swapped), L"PALT before STRS");

    RawFile late = GoldenRaw();
    std::swap(late.chunks[3], late.chunks[4]);
    ExpectRefusal(NvfError::ChunkOutOfOrder, Assemble(late), L"HPNT before VOXL");

    RawFile twice = GoldenRaw();
    const RawChunk palette = twice.chunks[1];
    twice.chunks.push_back(palette);
    ExpectRefusal(NvfError::ChunkOutOfOrder, Assemble(twice), L"a second PALT");

    RawFile repeated = GoldenRaw();
    const RawChunk strings = repeated.chunks[0];
    repeated.chunks.insert(repeated.chunks.begin() + 1, strings);
    ExpectRefusal(NvfError::ChunkOutOfOrder, Assemble(repeated), L"STRS twice in a row");
  }

  TEST_METHOD(RefusesBadStrings)
  {
    ExpectRefusal(NvfError::BadString, WithPart(1, [](NvfPartRecord& _record) { _record.nameOffset = 6; }), L"an offset inside a string");
    ExpectRefusal(NvfError::BadString, WithPart(0, [](NvfPartRecord& _record) { _record.nameOffset = 85; }), L"an offset past STRS");
    ExpectRefusal(NvfError::BadString, WithHardpoint(2, [](NvfHardpointRecord& _record) { _record.nameOffset = 0xFFFFFFFFu; }),
                  L"a hardpoint's offset far past STRS");
    ExpectRefusal(NvfError::BadString, WithHardpoint(0, [](NvfHardpointRecord& _record) { _record.nameOffset = 4; }),
                  L"an offset at the NUL that ends a string");
    Bytes withEmpty(GOLDEN_STRINGS.begin(), GOLDEN_STRINGS.end());
    withEmpty.push_back(0);
    RawFile empty = Split(WithStrings(withEmpty));
    NvfHardpointRecord dock = GetRecord<NvfHardpointRecord>(ChunkNamed(empty, "HPNT"), 0);
    dock.nameOffset = 85;
    SetRecord(ChunkNamed(empty, "HPNT"), 0, dock);
    ExpectRefusal(NvfError::BadString, Assemble(empty), L"the empty name");

    Bytes unterminated(GOLDEN_STRINGS.begin(), GOLDEN_STRINGS.end());
    unterminated.back() = 'x';
    ExpectRefusal(NvfError::BadString, WithStrings(unterminated), L"STRS whose last string has no NUL");

    // Strings nothing refers to are read, but must still be UTF-8, which Table 3-7 of the Unicode Standard defines.
    const std::array<std::pair<const wchar_t*, Bytes>, 7> invalid{{
      {L"a lone continuation byte", {0x80}},
      {L"an overlong slash", {0xC0, 0xAF}},
      {L"an overlong three-byte form", {0xE0, 0x80, 0xAF}},
      {L"a surrogate", {0xED, 0xA0, 0x80}},
      {L"beyond U+10FFFF", {0xF4, 0x90, 0x80, 0x80}},
      {L"a lead byte cut short", {0xE2, 0x82}},
      {L"a byte UTF-8 never uses", {0xFF}},
    }};
    for (const auto& [what, sequence] : invalid)
    {
      Bytes strings(GOLDEN_STRINGS.begin(), GOLDEN_STRINGS.end());
      strings.insert(strings.end(), sequence.begin(), sequence.end());
      strings.push_back(0);
      ExpectRefusal(NvfError::BadString, WithStrings(strings), what);
    }
    Bytes accented(GOLDEN_STRINGS.begin(), GOLDEN_STRINGS.end());
    accented.insert(accented.end(), {0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x9A, 0x80, 0x00});
    static_cast<void>(ExpectAccepted(WithStrings(accented), L"well-formed UTF-8 nothing refers to"));

    const std::string longest(NeuronCore::NVF_MAX_SEGMENT_CHARS, 'a');
    static_cast<void>(
      ExpectAccepted(WithNames({"hull", "hull/" + longest, "hull/" + longest + "/barrel"}, GoldenNames()), L"a segment of 31 characters"));
    const std::array<std::vector<std::string>, 7> badPaths{{
      {"Hull", "Hull/turret", "Hull/turret/barrel"},
      {"hull", "hull/turret", "hull/turret/barrel-2"},
      {"hull", "hull/turret", "hull/turret/"},
      {"hull", "hull//turret", "hull//turret/barrel"},
      {"hull", "hull/turret", "hull/turret/barrel.main"},
      {"hull", "hull/" + longest + "a", "hull/" + longest + "a/barrel"},
      {"hull", "hull/tur\xC3\xA9t", "hull/tur\xC3\xA9t/barrel"},
    }};
    for (const std::vector<std::string>& paths : badPaths)
    {
      ExpectRefusal(NvfError::BadString, WithNames(paths, GoldenNames()), L"part path " + Widen(paths[1] + " " + paths[2]));
    }
    const std::array<std::string, 7> badNames{"weapon", "pivot.main",  "weapon..main",      "weapon.main.",
                                              ".main",  "Weapon.main", "weapon.m" + longest};
    for (const std::string& name : badNames)
    {
      ExpectRefusal(NvfError::BadString, WithNames(GoldenPaths(), {"dock.aft", "engine.main", "sensor.top.left", name}),
                    L"hardpoint name " + Widen(name));
    }
    static_cast<void>(
      ExpectAccepted(WithNames(GoldenPaths(), {"dock.aft", "engine.main", "sensor.top.left", "pivots.main"}), L"a type that begins pivot"));
    static_cast<void>(ExpectAccepted(WithNames({"pivot", "pivot/turret", "pivot/turret/barrel"}, GoldenNames()), L"a part named pivot"));
  }

  TEST_METHOD(RefusesDuplicateNames)
  {
    ExpectRefusal(NvfError::DuplicateName, WithNames(GoldenPaths(), {"dock.aft", "engine.main", "sensor.top.left", "dock.aft"}),
                  L"two hardpoints named dock.aft");

    // Part 2 moved under part 0 as a second hull/turret: a sound tree with one path twice.
    RawFile file = Split(WithNames({"hull", "hull/turret", "hull/turret"}, GoldenNames()));
    RawChunk& parts = ChunkNamed(file, "PART");
    NvfPartRecord barrel = GetRecord<NvfPartRecord>(parts, 2);
    barrel.parentIndex = 0;
    SetRecord(parts, 2, barrel);
    ExpectRefusal(NvfError::DuplicateName, Assemble(file), L"two parts named hull/turret");
  }

  TEST_METHOD(RefusesBadPartTrees)
  {
    ExpectRefusal(NvfError::BadPartTree, WithPart(0, [](NvfPartRecord& _record) { _record.parentIndex = 0; }), L"part 0 its own parent");
    ExpectRefusal(NvfError::BadPartTree, WithPart(1, [](NvfPartRecord& _record) { _record.parentIndex = NeuronCore::NVF_NO_PARENT; }),
                  L"a second root");
    ExpectRefusal(NvfError::BadPartTree, WithPart(1, [](NvfPartRecord& _record) { _record.parentIndex = 1; }), L"a part its own parent");
    ExpectRefusal(NvfError::BadPartTree, WithPart(1, [](NvfPartRecord& _record) { _record.parentIndex = 2; }), L"a parent after its child");
    ExpectRefusal(NvfError::BadPartTree, WithPart(2, [](NvfPartRecord& _record) { _record.parentIndex = 0; }),
                  L"a path that names another parent");
    ExpectRefusal(NvfError::BadPartTree, WithNames({"hull", "hull/turret", "hull/barrel"}, GoldenNames()), L"a path off its parent's");
    ExpectRefusal(NvfError::BadPartTree, WithNames({"ship/hull", "ship/hull/turret", "ship/hull/turret/barrel"}, GoldenNames()),
                  L"a root whose path has two segments");
    ExpectRefusal(NvfError::BadPartTree, WithNames({"hull", "turret", "turret/barrel"}, GoldenNames()), L"a child of one segment");

    RawFile empty = GoldenRaw();
    RawChunk& parts = ChunkNamed(empty, "PART");
    parts.content.clear();
    parts.elementCount = 0;
    ExpectRefusal(NvfError::BadPartTree, Assemble(empty), L"no part at all");
  }

  TEST_METHOD(RefusesPartsTooLarge)
  {
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      ExpectRefusal(NvfError::PartTooLarge, WithPart(1, [axis](NvfPartRecord& _record) { _record.sizeVoxels[axis] = 257; }),
                    std::format(L"257 on axis {}", axis));
    }
    static_cast<void>(ExpectAccepted(WithPart(0, [](NvfPartRecord& _record) { _record.sizeVoxels = {256, 256, 256}; }), L"256 cubed"));
  }

  TEST_METHOD(RefusesTranslationsOutOfRange)
  {
    // The parts' origins sum down the tree: the barrel's x is the hull's plus 1 plus 1.
    const std::int32_t limit = NeuronCore::NVF_MAX_PART_ORIGIN;
    static_cast<void>(
      ExpectAccepted(WithPart(0, [limit](NvfPartRecord& _record) { _record.translation[0] = limit - 2; }), L"the barrel at the limit"));
    ExpectRefusal(NvfError::TranslationOutOfRange, WithPart(0, [limit](NvfPartRecord& _record) { _record.translation[0] = limit - 1; }),
                  L"the barrel one past the limit");
    static_cast<void>(
      ExpectAccepted(WithPart(0, [limit](NvfPartRecord& _record) { _record.translation[1] = -limit; }), L"the hull at the negative limit"));
    ExpectRefusal(NvfError::TranslationOutOfRange, WithPart(0, [limit](NvfPartRecord& _record) { _record.translation[2] = -limit - 1; }),
                  L"the hull one past the negative limit");
    ExpectRefusal(NvfError::TranslationOutOfRange,
                  WithPart(2, [](NvfPartRecord& _record) { _record.translation[2] = std::numeric_limits<std::int32_t>::min(); }),
                  L"the least int32");
  }

  TEST_METHOD(RefusesBadVoxelRanges)
  {
    ExpectRefusal(NvfError::BadVoxelRange, WithPart(0, [](NvfPartRecord& _record) { _record.firstVoxel = 1; }), L"part 0 not at 0");
    ExpectRefusal(NvfError::BadVoxelRange, WithPart(1, [](NvfPartRecord& _record) { _record.firstVoxel = 9; }), L"a gap");
    ExpectRefusal(NvfError::BadVoxelRange, WithPart(1, [](NvfPartRecord& _record) { _record.firstVoxel = 7; }), L"an overlap");
    ExpectRefusal(NvfError::BadVoxelRange,
                  WithPart(2,
                           [](NvfPartRecord& _record)
                           {
                             _record.firstVoxel = 12;
                             _record.voxelCount = 0;
                           }),
                  L"a part without a voxel");
    RawFile emptied = Split(WithPart(1, [](NvfPartRecord& _record) { _record.voxelCount = 0; }));
    NvfPartRecord barrel = GetRecord<NvfPartRecord>(ChunkNamed(emptied, "PART"), 2);
    barrel.firstVoxel = 8;
    barrel.voxelCount = 8;
    SetRecord(ChunkNamed(emptied, "PART"), 2, barrel);
    ExpectRefusal(NvfError::BadVoxelRange, Assemble(emptied), L"a part without a voxel, the counts adding up");
    ExpectRefusal(NvfError::BadVoxelRange, WithPart(2, [](NvfPartRecord& _record) { _record.voxelCount = 3; }), L"a record no part holds");

    RawFile extra = GoldenRaw();
    RawChunk& voxels = ChunkNamed(extra, "VOXL");
    voxels.content.insert(voxels.content.end(), 4, 0);
    voxels.elementCount = 17;
    ExpectRefusal(NvfError::BadVoxelRange, Assemble(extra), L"VOXL holding more than the parts");
  }

  TEST_METHOD(RefusesVoxelsOutOfBoundsOrTwice)
  {
    // The hull is 5 x 3 x 7, and its records are 0-7.
    ExpectRefusal(NvfError::VoxelOutOfBounds, WithVoxel(3, NeuronCore::PackVoxelRecord({5, 0, 0, 3})), L"x at the hull's width");
    ExpectRefusal(NvfError::VoxelOutOfBounds, WithVoxel(3, NeuronCore::PackVoxelRecord({0, 3, 0, 3})), L"y at the hull's height");
    ExpectRefusal(NvfError::VoxelOutOfBounds, WithVoxel(3, NeuronCore::PackVoxelRecord({0, 0, 7, 3})), L"z at the hull's depth");
    ExpectRefusal(NvfError::VoxelOutOfBounds, WithVoxel(15, NeuronCore::PackVoxelRecord({0, 0, 4, 15})), L"the barrel's fifth voxel");
    ExpectRefusal(NvfError::DuplicateVoxel, WithVoxel(7, NeuronCore::PackVoxelRecord({4, 2, 6, 7})), L"two voxels at (4, 2, 6)");
    ExpectRefusal(NvfError::DuplicateVoxel, WithVoxel(9, NeuronCore::PackVoxelRecord({0, 0, 0, 9})), L"the turret's corner twice");
    ExpectRefusal(NvfError::ReservedBitsSet, WithVoxel(0, NeuronCore::PackVoxelRecord({0, 0, 0, 0}) | 0x10000000u), L"bit 28");
    ExpectRefusal(NvfError::ReservedBitsSet, WithVoxel(12, NeuronCore::PackVoxelRecord({0, 0, 0, 12}) | 0x80000000u), L"bit 31");
  }

  TEST_METHOD(RefusesBadHardpointParts)
  {
    ExpectRefusal(NvfError::BadHardpointPart, WithHardpoint(3, [](NvfHardpointRecord& _record) { _record.partIndex = 3; }), L"part 3 of 3");
    ExpectRefusal(NvfError::BadHardpointPart, WithHardpoint(0, [](NvfHardpointRecord& _record) { _record.partIndex = 0xFFFFFFFFu; }),
                  L"no part");
  }

  TEST_METHOD(RefusesNonFiniteValues)
  {
    ExpectRefusal(NvfError::NotFinite, WithPalette(0, [](NvfPaletteRecord& _record) { _record.emit = NOT_A_NUMBER; }), L"a NaN emit");
    ExpectRefusal(NvfError::NotFinite, WithPalette(15, [](NvfPaletteRecord& _record) { _record.flux = POSITIVE_INFINITY; }),
                  L"an infinite flux");
    ExpectRefusal(NvfError::NotFinite, WithPart(2, [](NvfPartRecord& _record) { _record.pivot[1] = -POSITIVE_INFINITY; }),
                  L"an infinite pivot");
    ExpectRefusal(NvfError::NotFinite, WithHardpoint(1, [](NvfHardpointRecord& _record) { _record.position[2] = NOT_A_NUMBER; }),
                  L"a NaN position");
    ExpectRefusal(NvfError::NotFinite, WithHardpoint(3, [](NvfHardpointRecord& _record) { _record.rotation[3] = NOT_A_NUMBER; }),
                  L"a NaN rotation");
  }

  TEST_METHOD(RefusesRotationsThatAreNotUnit)
  {
    const auto withRotation = [](std::array<float, 4> _rotation)
    { return WithHardpoint(2, [_rotation](NvfHardpointRecord& _record) { _record.rotation = _rotation; }); };
    ExpectRefusal(NvfError::NotUnitRotation, withRotation({0.0f, 0.0f, 0.0f, 1.001f}), L"a little long");
    ExpectRefusal(NvfError::NotUnitRotation, withRotation({0.0f, 0.0f, 0.0f, 0.999f}), L"a little short");
    ExpectRefusal(NvfError::NotUnitRotation, withRotation({0.0f, 0.0f, 0.0f, 0.0f}), L"zero");
    ExpectRefusal(NvfError::NotUnitRotation, withRotation({0.0f, 0.0f, 0.0f, -1.0f}), L"w below zero");
    ExpectRefusal(NvfError::NotUnitRotation, withRotation({0.0f, 0.25881904f, 0.0f, -0.9659258f}), L"30 degrees, spelled with w < 0");
    static_cast<void>(ExpectAccepted(withRotation({0.0f, 0.0f, 0.0f, 1.00005f}), L"within 10^-4"));
    static_cast<void>(ExpectAccepted(withRotation({0.0f, 0.0f, -1.0f, 0.0f}), L"half a turn, w zero"));
  }

  // ADR-019: the reader checks in one order and reports the first fault, and NvfFormat.py checks in the same order, so
  // that both name the same fault in a file that has two.
  TEST_METHOD(ChecksInTheFixedOrder)
  {
    RawFile file = GoldenRaw();
    ChunkNamed(file, "PALT").reserved = 1;
    Bytes truncated = Assemble(file);
    truncated.pop_back();
    ExpectRefusal(NvfError::MalformedChunk, truncated, L"framing in file order: PALT's reserved field before HPNT's end");
    file = GoldenRaw();
    ChunkNamed(file, "HPNT").reserved = 1;
    truncated = Assemble(file);
    truncated.pop_back();
    ExpectRefusal(NvfError::Truncated, truncated, L"within a chunk, its length before its reserved field");

    file = GoldenRaw();
    std::swap(file.chunks[3], file.chunks[4]);
    ChunkNamed(file, "STRS").content.back() = 'x';
    ExpectRefusal(NvfError::ChunkOutOfOrder, Assemble(file), L"chunk order before any chunk's content");

    Bytes strings(GOLDEN_STRINGS.begin(), GOLDEN_STRINGS.end());
    strings.insert(strings.end(), {0xFF, 0x00});
    file = Split(WithStrings(strings));
    NvfPaletteRecord entry = GetRecord<NvfPaletteRecord>(ChunkNamed(file, "PALT"), 0);
    entry.emit = NOT_A_NUMBER;
    SetRecord(ChunkNamed(file, "PALT"), 0, entry);
    ExpectRefusal(NvfError::BadString, Assemble(file), L"STRS before PALT");

    ExpectRefusal(NvfError::PartTooLarge,
                  WithPart(1,
                           [](NvfPartRecord& _record)
                           {
                             _record.sizeVoxels[0] = 300;
                             _record.translation[0] = std::numeric_limits<std::int32_t>::max();
                           }),
                  L"a part's size before its translation");
    ExpectRefusal(NvfError::DuplicateName,
                  WithPart(2,
                           [](NvfPartRecord& _record)
                           {
                             _record.nameOffset = 5;
                             _record.parentIndex = 0;
                             _record.pivot[0] = NOT_A_NUMBER;
                           }),
                  L"a part's name before its pivot");
    ExpectRefusal(NvfError::ReservedBitsSet, WithVoxel(2, NeuronCore::PackVoxelRecord({9, 0, 0, 2}) | 0x20000000u),
                  L"reserved bits before bounds");

    file = Split(WithVoxel(1, NeuronCore::PackVoxelRecord({0, 0, 0, 1})));
    NvfHardpointRecord hardpoint = GetRecord<NvfHardpointRecord>(ChunkNamed(file, "HPNT"), 0);
    hardpoint.partIndex = 9;
    SetRecord(ChunkNamed(file, "HPNT"), 0, hardpoint);
    ExpectRefusal(NvfError::DuplicateVoxel, Assemble(file), L"VOXL before HPNT");

    ExpectRefusal(NvfError::BadHardpointPart,
                  WithHardpoint(0,
                                [](NvfHardpointRecord& _record)
                                {
                                  _record.partIndex = 7;
                                  _record.position[0] = NOT_A_NUMBER;
                                }),
                  L"a hardpoint's part before its position");
  }

  // The writer refuses, by the reader's name, a model the reader would refuse, so that nothing it writes fails to read.
  TEST_METHOD(RefusesToWriteWhatItWouldNotRead)
  {
    const auto expectWriteRefusal = [](NvfError _expected, const NvfModel& _model, const wchar_t* _case)
    {
      const auto bytes = NeuronCore::SerializeNvfModel(_model);
      Assert::IsFalse(bytes.has_value(), (std::wstring(_case) + L": the model was written").c_str());
      Assert::AreEqual(NeuronCore::NvfErrorName(_expected), NeuronCore::NvfErrorName(bytes.error()), _case);
    };

    NvfModel model = GoldenNvfModel();
    model.hardpoints[1].name = "weapon.main";
    expectWriteRefusal(NvfError::DuplicateName, model, L"two hardpoints named weapon.main");

    model = GoldenNvfModel();
    model.parts[1].size.y = 65536 + 2;
    expectWriteRefusal(NvfError::PartTooLarge, model, L"a size 16 bits would wrap to 2");

    model = GoldenNvfModel();
    model.parts[2].size.z = 0;
    expectWriteRefusal(NvfError::MalformedChunk, model, L"a size of 0");

    model = GoldenNvfModel();
    model.parts[0].size.x = -1;
    expectWriteRefusal(NvfError::MalformedChunk, model, L"a negative size");

    model = GoldenNvfModel();
    model.hardpoints[0].name = "Weapon.main";
    expectWriteRefusal(NvfError::BadString, model, L"a hardpoint name in capitals");

    model = GoldenNvfModel();
    model.parts[1].pivot.y = NOT_A_NUMBER;
    expectWriteRefusal(NvfError::NotFinite, model, L"a NaN pivot");

    model = GoldenNvfModel();
    model.parts[2].parent = 0;
    expectWriteRefusal(NvfError::BadPartTree, model, L"a parent its path does not name");

    model = GoldenNvfModel();
    model.hardpoints[2].rotation = {0.0f, 0.0f, 0.0f, 2.0f};
    expectWriteRefusal(NvfError::NotUnitRotation, model, L"a rotation of length 2");

    model = GoldenNvfModel();
    model.records.push_back(NeuronCore::PackVoxelRecord({0, 0, 1, 0}));
    expectWriteRefusal(NvfError::BadVoxelRange, model, L"a record no part holds");

    // A name with a NUL in it reads back as the part before the NUL, which the reader alone would accept.
    model = GoldenNvfModel();
    model.parts[0].path = std::string("hull\0x", 6);
    expectWriteRefusal(NvfError::BadString, model, L"a part path with a NUL in it");
    model = GoldenNvfModel();
    model.hardpoints[3].name = std::string("dock.aft\0", 9);
    expectWriteRefusal(NvfError::BadString, model, L"a hardpoint name ending in a NUL");
  }

  TEST_METHOD(NamesEveryError)
  {
    const std::array<std::pair<NvfError, const char*>, 21> names{{
      {NvfError::FileNotFound, "FileNotFound"},
      {NvfError::ReadFailed, "ReadFailed"},
      {NvfError::WriteFailed, "WriteFailed"},
      {NvfError::NotAnNvfFile, "NotAnNvfFile"},
      {NvfError::UnsupportedVersion, "UnsupportedVersion"},
      {NvfError::Truncated, "Truncated"},
      {NvfError::MalformedChunk, "MalformedChunk"},
      {NvfError::MissingChunk, "MissingChunk"},
      {NvfError::ChunkOutOfOrder, "ChunkOutOfOrder"},
      {NvfError::BadString, "BadString"},
      {NvfError::DuplicateName, "DuplicateName"},
      {NvfError::BadPartTree, "BadPartTree"},
      {NvfError::PartTooLarge, "PartTooLarge"},
      {NvfError::TranslationOutOfRange, "TranslationOutOfRange"},
      {NvfError::BadVoxelRange, "BadVoxelRange"},
      {NvfError::VoxelOutOfBounds, "VoxelOutOfBounds"},
      {NvfError::DuplicateVoxel, "DuplicateVoxel"},
      {NvfError::ReservedBitsSet, "ReservedBitsSet"},
      {NvfError::BadHardpointPart, "BadHardpointPart"},
      {NvfError::NotFinite, "NotFinite"},
      {NvfError::NotUnitRotation, "NotUnitRotation"},
    }};
    for (const auto& [error, name] : names)
    {
      Assert::AreEqual(name, NeuronCore::NvfErrorName(error));
    }
  }

  TEST_METHOD(SavesAndLoadsFiles)
  {
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path missing = directory / "OutpostNeuronCoreTests-Missing.nvf";
    std::filesystem::remove(missing);
    const auto notFound = NeuronCore::LoadNvfModel(missing);
    Assert::IsFalse(notFound.has_value(), L"a file that does not exist");
    Assert::AreEqual("FileNotFound", NeuronCore::NvfErrorName(notFound.error()));

    const std::filesystem::path path = directory / "OutpostNeuronCoreTests-SavesAndLoadsFiles.nvf";
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
      std::ofstream stale(path, std::ios::binary | std::ios::trunc);
      stale << "an older file, which the save replaces";
    }
    Assert::IsTrue(NeuronCore::SaveNvfModel(GoldenNvfModel(), path).has_value(), L"saving over an existing file");
    Assert::IsFalse(std::filesystem::exists(temporary), L"no temporary file is left behind");
    const auto loaded = NeuronCore::LoadNvfModel(path);
    Assert::IsTrue(loaded.has_value(), L"loading the file saved");
    AreEqualModels(GoldenNvfModel(), *loaded);

    // A model the writer refuses leaves the file as it was.
    NvfModel bad = GoldenNvfModel();
    bad.hardpoints[0].name = "pivot.main";
    const auto refused = NeuronCore::SaveNvfModel(bad, path);
    Assert::IsFalse(refused.has_value(), L"saving a model the writer refuses");
    Assert::AreEqual("BadString", NeuronCore::NvfErrorName(refused.error()));
    const auto kept = NeuronCore::LoadNvfModel(path);
    std::filesystem::remove(path);
    Assert::IsTrue(kept.has_value(), L"the earlier file is still there");
    AreEqualModels(GoldenNvfModel(), *kept);

    const auto unwritable = NeuronCore::SaveNvfModel(GoldenNvfModel(), directory / "OutpostNeuronCoreTests-NoSuchFolder" / "Model.nvf");
    Assert::IsFalse(unwritable.has_value(), L"saving into a folder that does not exist");
    Assert::AreEqual("WriteFailed", NeuronCore::NvfErrorName(unwritable.error()));
  }

  // §4.1's spelling of names.
  TEST_METHOD(SpellsNamesAsSpecified)
  {
    for (const std::string_view path : {"main", "hull", "hull/turret", "a/b/c/d", "x_1/y2", "0"})
    {
      Assert::IsTrue(NeuronCore::IsNvfPartPath(path), Widen(path).c_str());
    }
    for (const std::string_view path : {"", "/", "hull/", "/hull", "Hull", "hull turret", "hull.turret", "hull\\turret", "h-1"})
    {
      Assert::IsFalse(NeuronCore::IsNvfPartPath(path), Widen(path).c_str());
    }
    for (const std::string_view name : {"engine.main", "weapon.left", "dock.aft", "a.b.c", "sensor_2.top_left", "pivots.x"})
    {
      Assert::IsTrue(NeuronCore::IsNvfHardpointName(name), Widen(name).c_str());
    }
    for (const std::string_view name : {"", "engine", "engine.", ".main", "engine..main", "pivot.main", "Engine.main", "engine/main"})
    {
      Assert::IsFalse(NeuronCore::IsNvfHardpointName(name), Widen(name).c_str());
    }
    NeuronCore::NvfHardpoint hardpoint{};
    hardpoint.name = "sensor.top.left";
    Assert::IsTrue(NeuronCore::HardpointType(hardpoint) == "sensor", L"the first segment is the type");
  }
};

} // namespace NeuronCoreTests
