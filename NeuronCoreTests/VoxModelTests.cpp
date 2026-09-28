#include "pch.h"

#include "Box.h"
#include "RigidTransform.h"
#include "VoxFile.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
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

using NeuronCore::Int3;
using NeuronCore::VoxError;

// The positions of the chunks in OneModelChunks.
constexpr std::size_t SIZE_CHUNK = 0;
constexpr std::size_t VOXELS_CHUNK = 1;
constexpr std::size_t ROOT_CHUNK = 2;
constexpr std::size_t GROUP_CHUNK = 3;
constexpr std::size_t PLACEMENT_CHUNK = 4;
constexpr std::size_t SHAPE_CHUNK = 5;
constexpr std::size_t PALETTE_CHUNK = 6;

// An L of three voxels in palette entries 1, 2 and 16, the lowest and highest the reader accepts. The file is in
// MagicaVoxel's axes; the engine sees it with y and z swapped (Design/NeuronVoxelFormat.md §12).
constexpr Int3 L_SIZE{4, 3, 5};
constexpr Int3 ENGINE_L_SIZE{4, 5, 3};
constexpr std::array<FileVoxel, 3> L_VOXELS{{{0, 0, 0, 1}, {1, 0, 0, 2}, {1, 2, 3, 16}}};

// A second model: one voxel in entry 5.
constexpr Int3 DOT_SIZE{1, 1, 1};
constexpr std::array<FileVoxel, 1> DOT_VOXELS{{{0, 0, 0, 5}}};

// A marker as Design/NeuronVoxelFormat.md §5 draws one: an arrow three voxels long along MagicaVoxel's +Y, which is the
// engine's forward, +Z. Odd in every dimension, so it may be turned.
constexpr Int3 ARROW_SIZE{1, 3, 1};
constexpr std::array<FileVoxel, 3> ARROW_VOXELS{{{0, 0, 0, 3}, {0, 1, 0, 3}, {0, 2, 0, 16}}};

// _r = 33: row 0 picks y, row 1 picks x negated, row 2 picks z. It turns MagicaVoxel's +Y to +X: a quarter turn about +Z.
constexpr std::string_view Y_TO_X_ROTATION = "33";

// MagicaVoxel's y and z swapped, which converts a vector either way (Design/NeuronVoxelFormat.md §4.1).
[[nodiscard]] constexpr NeuronCore::Float3 Swap(NeuronCore::Float3 _vector) noexcept
{
  return {_vector.x, _vector.z, _vector.y};
}

// _r decoded as MagicaVoxel-file-format-vox-extension.txt describes it, apart from the reader's own decoding: the matrix
// row by row, or nothing when a column is named twice or out of range.
[[nodiscard]] std::optional<std::array<std::array<float, 3>, 3>> DecodeRotation(std::uint32_t _bits)
{
  const std::uint32_t first = _bits & 3u;
  const std::uint32_t second = (_bits >> 2u) & 3u;
  if (first > 2 || second > 2 || first == second)
  {
    return std::nullopt;
  }
  const std::array<std::uint32_t, 3> columns{first, second, 3 - first - second};
  std::array<std::array<float, 3>, 3> rows{};
  for (std::uint32_t row = 0; row < 3; ++row)
  {
    rows[row][columns[row]] = ((_bits >> (4u + row)) & 1u) != 0u ? -1.0f : 1.0f;
  }
  return rows;
}

// _rows times _vector.
[[nodiscard]] NeuronCore::Float3 Multiply(const std::array<std::array<float, 3>, 3>& _rows, NeuronCore::Float3 _vector) noexcept
{
  const auto row = [&_rows, _vector](std::size_t _row)
  { return _rows[_row][0] * _vector.x + _rows[_row][1] * _vector.y + _rows[_row][2] * _vector.z; };
  return {row(0), row(1), row(2)};
}

// The engine's image of each axis under the MagicaVoxel rotation _rows: swap in, turn, swap out.
[[nodiscard]] NeuronCore::Rotation EngineRotation(const std::array<std::array<float, 3>, 3>& _rows) noexcept
{
  const auto image = [&_rows](NeuronCore::Float3 _axis) { return Swap(Multiply(_rows, Swap(_axis))); };
  return {image({1.0f, 0.0f, 0.0f}), image({0.0f, 1.0f, 0.0f}), image({0.0f, 0.0f, 1.0f})};
}

void AreEqualFloat3(NeuronCore::Float3 _expected, NeuronCore::Float3 _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _what);
  Assert::AreEqual(_expected.y, _actual.y, _what);
  Assert::AreEqual(_expected.z, _actual.z, _what);
}

// A scene of one model placed by node 2, whose rotation is _rotation, with the scene graph of OneModelChunks.
[[nodiscard]] Bytes TurnedModel(Int3 _size, std::span<const FileVoxel> _voxels, std::string_view _translation, std::string_view _rotation,
                                std::string_view _name = {})
{
  std::vector<Bytes> chunks = OneModelChunks(_size, _voxels, _translation);
  NeuronCore::VoxAttributes attributes;
  if (!_name.empty())
  {
    attributes.emplace("_name", _name);
  }
  chunks[PLACEMENT_CHUNK] = TransformChunk(2, attributes, 3, 0, {{{"_t", std::string(_translation)}, {"_r", std::string(_rotation)}}});
  return VoxFile(chunks);
}

[[nodiscard]] std::vector<Bytes> LChunks(std::string_view _translation = "0 0 0")
{
  return OneModelChunks(L_SIZE, L_VOXELS, _translation);
}

[[nodiscard]] Bytes WithTranslation(std::string_view _translation)
{
  return VoxFile(LChunks(_translation));
}

void ExpectRefusal(VoxError _expected, std::span<const std::uint8_t> _file, const std::wstring& _case)
{
  const auto model = NeuronCore::ParseVoxModel(_file);
  Assert::IsFalse(model.has_value(), (_case + L": the file was accepted").c_str());
  Assert::AreEqual(NeuronCore::VoxErrorName(_expected), NeuronCore::VoxErrorName(model.error()), _case.c_str());
}

[[nodiscard]] std::wstring Widen(std::string_view _text)
{
  return {_text.begin(), _text.end()};
}

[[nodiscard]] NeuronCore::VoxModel ExpectAccepted(std::span<const std::uint8_t> _file, const std::wstring& _case)
{
  auto model = NeuronCore::ParseVoxModel(_file);
  if (!model)
  {
    Assert::Fail(std::format(L"{}: refused with {}", _case, Widen(NeuronCore::VoxErrorName(model.error()))).c_str());
  }
  return std::move(*model);
}

// _chunk with one more byte at the end of its content, which nothing in the chunk accounts for.
[[nodiscard]] Bytes WithSpareByte(const Bytes& _chunk)
{
  const std::string_view id(reinterpret_cast<const char*>(_chunk.data()), 4);
  Bytes content(_chunk.begin() + 12, _chunk.end());
  content.push_back(0);
  return Chunk(id, content);
}

void AreEqualInt3(Int3 _expected, Int3 _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _what);
  Assert::AreEqual(_expected.y, _actual.y, _what);
  Assert::AreEqual(_expected.z, _actual.z, _what);
}

// The L model placed at the origin under the scene graph of OneModelChunks, with node 2 replaced.
[[nodiscard]] Bytes WithPlacement(const NeuronCore::VoxAttributes& _nodeAttributes, std::int32_t _layer,
                                  const std::vector<NeuronCore::VoxAttributes>& _frames)
{
  std::vector<Bytes> chunks = LChunks();
  chunks[PLACEMENT_CHUNK] = TransformChunk(2, _nodeAttributes, 3, _layer, _frames);
  return VoxFile(chunks);
}

// Three instances under one group: the dot at the origin, the L at (5, 0, 0) and the dot again at (0, 9, 0), in MagicaVoxel's
// axes.
[[nodiscard]] Bytes ThreeInstanceFile()
{
  const std::vector<Bytes> chunks{
    SizeChunk(L_SIZE),
    VoxelsChunk(L_VOXELS),
    SizeChunk(DOT_SIZE),
    VoxelsChunk(DOT_VOXELS),
    TransformChunk(0, {}, 1, -1, {{}}),
    GroupChunk(1, {}, {2, 4, 6}),
    TransformChunk(2, {}, 3, 0, {{{"_t", "0 0 0"}}}),
    ShapeChunk(3, {1}),
    TransformChunk(4, {}, 5, 0, {{{"_t", "5 0 0"}}}),
    ShapeChunk(5, {0}),
    TransformChunk(6, {}, 7, 0, {{{"_t", "0 9 0"}}}),
    ShapeChunk(7, {1}),
    PaletteChunk(),
  };
  return VoxFile(chunks);
}

} // namespace

TEST_CLASS(VoxModelTests)
{
public:
  // Design/NeuronVoxelFormat.md §12.4: the asymmetric L lands with every coordinate, its size and its translation swapped
  // into the engine's axes. MagicaVoxel's voxel (1, 2, 3) is the engine's (1, 3, 2), and the translation 10 -20 30 is
  // (10, 30, -20), from which the origin takes floor((4, 5, 3) / 2).
  TEST_METHOD(ReadsOneModel)
  {
    const NeuronCore::VoxModel model = ExpectAccepted(WithTranslation("10 -20 30"), L"the L");
    Assert::AreEqual(200, model.version);
    Assert::AreEqual(std::size_t{1}, model.instances.size());
    const NeuronCore::ModelInstance& instance = model.instances.front();
    AreEqualInt3({8, 28, -21}, instance.origin, L"origin: the translation minus floor(size / 2)");
    AreEqualInt3(ENGINE_L_SIZE, instance.size, L"size");
    Assert::AreEqual(0u, instance.firstRecord);
    Assert::AreEqual(3u, instance.recordCount);

    // Colors arrive as the palette entry minus one, and the records keep the file's order.
    const std::vector<std::uint32_t> records{NeuronCore::PackVoxelRecord({0, 0, 0, 0}), NeuronCore::PackVoxelRecord({1, 0, 0, 1}),
                                             NeuronCore::PackVoxelRecord({1, 3, 2, 15})};
    Assert::IsTrue(model.records == records, L"records");

    for (std::size_t i = 0; i < model.palette.size(); ++i)
    {
      const NeuronCore::PaletteEntry& entry = model.palette[i];
      const std::wstring what = std::format(L"palette entry {}", i + 1);
      Assert::AreEqual(EGA_PALETTE[i][0], entry.red, what.c_str());
      Assert::AreEqual(EGA_PALETTE[i][1], entry.green, what.c_str());
      Assert::AreEqual(EGA_PALETTE[i][2], entry.blue, what.c_str());
      Assert::AreEqual(EGA_PALETTE[i][3], entry.alpha, what.c_str());
      Assert::IsFalse(entry.emissive, what.c_str());
      Assert::AreEqual(0.0f, entry.emit, what.c_str());
      Assert::AreEqual(0.0f, entry.flux, what.c_str());
    }
    Assert::IsTrue(model.renderObjects.empty(), L"render objects");
  }

  TEST_METHOD(AcceptsVersions150And200Only)
  {
    for (const std::int32_t version : {150, 200})
    {
      const NeuronCore::VoxModel model = ExpectAccepted(VoxFile(LChunks(), version), std::format(L"version {}", version));
      Assert::AreEqual(version, model.version);
    }
    for (const std::int32_t version : {0, 149, 151, 199, 201})
    {
      ExpectRefusal(VoxError::UnsupportedVersion, VoxFile(LChunks(), version), std::format(L"version {}", version));
    }
  }

  TEST_METHOD(RefusesWhatIsNotAVoxFile)
  {
    Bytes file = VoxFile(LChunks());
    file[3] = '!';
    ExpectRefusal(VoxError::NotAVoxFile, file, L"magic");
    ExpectRefusal(VoxError::NotAVoxFile, Bytes{}, L"empty");

    Bytes packFirst = VoxFile(LChunks());
    std::memcpy(packFirst.data() + 8, "PACK", 4);
    ExpectRefusal(VoxError::NotAVoxFile, packFirst, L"a first chunk other than MAIN");
  }

  TEST_METHOD(RefusesEveryTruncation)
  {
    const Bytes file = VoxFile(LChunks());
    for (std::size_t length = 0; length < file.size(); ++length)
    {
      // The header is eight bytes; after it, every cut leaves MAIN or a chunk inside it short.
      const VoxError expected = length < 8 ? VoxError::NotAVoxFile : VoxError::Truncated;
      ExpectRefusal(expected, std::span(file).first(length), std::format(L"cut to {} of {} bytes", length, file.size()));
    }
  }

  TEST_METHOD(RefusesBytesInOrAfterMain)
  {
    Bytes trailing = VoxFile(LChunks());
    trailing.push_back(0);
    ExpectRefusal(VoxError::MalformedChunk, trailing, L"a byte after MAIN");

    Bytes children;
    for (const Bytes& chunk : LChunks())
    {
      children.insert(children.end(), chunk.begin(), chunk.end());
    }
    Bytes file{'V', 'O', 'X', ' ', 200, 0, 0, 0};
    const Bytes mainChunk = Chunk("MAIN", {0, 0, 0, 0}, children);
    file.insert(file.end(), mainChunk.begin(), mainChunk.end());
    ExpectRefusal(VoxError::MalformedChunk, file, L"content in MAIN");
  }

  TEST_METHOD(RefusesSizesAndCountsThatDisagreeWithTheirChunk)
  {
    const auto refuseWith = [](std::size_t _index, const Bytes& _chunk, const wchar_t* _case)
    {
      std::vector<Bytes> chunks = LChunks();
      chunks[_index] = _chunk;
      ExpectRefusal(VoxError::MalformedChunk, VoxFile(chunks), _case);
    };
    refuseWith(SIZE_CHUNK, Chunk("SIZE", Bytes(8, 1)), L"SIZE of eight bytes");
    refuseWith(SIZE_CHUNK, SizeChunk({4, 0, 5}), L"SIZE with a zero extent");
    refuseWith(SIZE_CHUNK, SizeChunk({-1, 3, 5}), L"SIZE with a negative extent");
    refuseWith(PALETTE_CHUNK, Chunk("RGBA", Bytes(1020, 0)), L"RGBA of 255 entries");
    refuseWith(VOXELS_CHUNK, Chunk("XYZI", {4, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 2, 1, 2, 3, 16}), L"XYZI counting four of three");
    refuseWith(VOXELS_CHUNK, Chunk("XYZI", {2, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 2, 1, 2, 3, 16}), L"XYZI counting two of three");
    refuseWith(VOXELS_CHUNK, Chunk("XYZI", {255, 255, 255, 255, 0, 0, 0, 1}), L"XYZI with a negative count");

    refuseWith(PLACEMENT_CHUNK, WithSpareByte(TransformChunk(2, {}, 3, 0, {{{"_t", "0 0 0"}}})), L"nTRN with a byte to spare");
    refuseWith(SIZE_CHUNK, WithSpareByte(SizeChunk(L_SIZE)), L"SIZE with a byte to spare");
    refuseWith(GROUP_CHUNK, WithSpareByte(GroupChunk(1, {}, {2})), L"nGRP with a byte to spare");
    refuseWith(SHAPE_CHUNK, WithSpareByte(ShapeChunk(3, {0})), L"nSHP with a byte to spare");

    // A string whose length runs past the end of its chunk.
    Bytes badString = TransformChunk(2, {{"_name", "x"}}, 3, 0, {{}});
    badString[12 + 4 + 4] = 200;
    refuseWith(PLACEMENT_CHUNK, badString, L"a string longer than its chunk");

    for (const std::string_view translation : {"1 2", "1 2 3 4", "1  2 3", "a b c", "1 2 3 ", "", "1.5 0 0", "99999999999 0 0"})
    {
      ExpectRefusal(VoxError::MalformedChunk, WithTranslation(translation), std::format(L"_t \"{}\"", Widen(translation)));
    }

    std::vector<Bytes> chunks = LChunks();
    chunks.push_back(MaterialChunk(3, {{"_type", "_emit"}, {"_emit", "bright"}}));
    ExpectRefusal(VoxError::MalformedChunk, VoxFile(chunks), L"_emit that is not a number");
  }

  TEST_METHOD(RefusesSizeAndVoxelsOutOfOrder)
  {
    std::vector<Bytes> chunks = LChunks();
    chunks.insert(chunks.begin() + VOXELS_CHUNK, SizeChunk(L_SIZE));
    ExpectRefusal(VoxError::MalformedChunk, VoxFile(chunks), L"SIZE twice");

    chunks = LChunks();
    chunks.erase(chunks.begin() + SIZE_CHUNK);
    ExpectRefusal(VoxError::MalformedChunk, VoxFile(chunks), L"XYZI without SIZE");

    chunks = LChunks();
    chunks.push_back(SizeChunk(L_SIZE));
    ExpectRefusal(VoxError::MalformedChunk, VoxFile(chunks), L"SIZE without XYZI");
  }

  TEST_METHOD(RefusesChunksThatOverrunTheirParent)
  {
    std::vector<Bytes> chunks = LChunks();
    chunks[SIZE_CHUNK][4] = 100; // the content claims 100 bytes; MAIN holds fewer after it
    chunks.resize(1);
    ExpectRefusal(VoxError::Truncated, VoxFile(chunks), L"content past MAIN's end");

    chunks = LChunks();
    chunks[SIZE_CHUNK][4] = 0xFF;
    chunks[SIZE_CHUNK][5] = 0xFF;
    chunks[SIZE_CHUNK][6] = 0xFF;
    chunks[SIZE_CHUNK][7] = 0xFF;
    ExpectRefusal(VoxError::Truncated, VoxFile(chunks), L"a negative content size");
  }

  TEST_METHOD(RefusesModelsLargerThan256)
  {
    const std::array<FileVoxel, 1> corner{{{255, 255, 255, 1}}};
    const NeuronCore::VoxModel largest = ExpectAccepted(VoxFile(OneModelChunks({256, 256, 256}, corner, "0 0 0")), L"256 cubed");
    AreEqualInt3({-128, -128, -128}, largest.instances.front().origin, L"origin of the largest model");

    for (const Int3 size : {Int3{257, 1, 1}, Int3{1, 257, 1}, Int3{1, 1, 257}})
    {
      ExpectRefusal(VoxError::ModelTooLarge, VoxFile(OneModelChunks(size, DOT_VOXELS, "0 0 0")),
                    std::format(L"size {} {} {}", size.x, size.y, size.z));
    }
  }

  TEST_METHOD(RefusesVoxelsOutsideTheirModel)
  {
    for (const FileVoxel voxel : {FileVoxel{4, 0, 0, 1}, FileVoxel{0, 3, 0, 1}, FileVoxel{0, 0, 5, 1}})
    {
      const std::array<FileVoxel, 1> voxels{voxel};
      ExpectRefusal(VoxError::VoxelOutOfBounds, VoxFile(OneModelChunks(L_SIZE, voxels, "0 0 0")),
                    std::format(L"voxel at {} {} {}", voxel.x, voxel.y, voxel.z));
    }
  }

  TEST_METHOD(RefusesDuplicateVoxels)
  {
    const std::array<FileVoxel, 3> voxels{{{1, 1, 1, 1}, {0, 0, 0, 1}, {1, 1, 1, 2}}};
    ExpectRefusal(VoxError::DuplicateVoxel, VoxFile(OneModelChunks(L_SIZE, voxels, "0 0 0")), L"two voxels at 1 1 1");
  }

  TEST_METHOD(RefusesColorsOutsideTheSixteen)
  {
    for (const std::uint8_t color : {std::uint8_t{0}, std::uint8_t{17}, std::uint8_t{255}})
    {
      const std::array<FileVoxel, 1> voxels{{{0, 0, 0, color}}};
      ExpectRefusal(VoxError::ColorOutOfRange, VoxFile(OneModelChunks(L_SIZE, voxels, "0 0 0")), std::format(L"entry {}", color));
    }
  }

  TEST_METHOD(RequiresAPaletteAndASceneGraph)
  {
    std::vector<Bytes> chunks = LChunks();
    chunks.erase(chunks.begin() + PALETTE_CHUNK);
    ExpectRefusal(VoxError::MissingPalette, VoxFile(chunks), L"no RGBA");

    chunks = LChunks();
    chunks.erase(chunks.begin() + ROOT_CHUNK);
    ExpectRefusal(VoxError::MissingSceneGraph, VoxFile(chunks), L"no node 0");

    chunks = LChunks();
    chunks.erase(chunks.begin() + ROOT_CHUNK, chunks.begin() + PALETTE_CHUNK);
    ExpectRefusal(VoxError::MissingSceneGraph, VoxFile(chunks), L"no scene graph at all");
  }

  TEST_METHOD(RefusesRotations)
  {
    // _r packs a signed permutation; 4 is the identity: row 0 picks x, row 1 picks y, and nothing is negated. The L is even
    // in x, so the identity is the only rotation it may have (Design/NeuronVoxelFormat.md §6.1).
    const NeuronCore::VoxModel identity = ExpectAccepted(WithPlacement({}, 0, {{{"_t", "0 0 0"}, {"_r", "4"}}}), L"_r 4");
    Assert::AreEqual(std::size_t{1}, identity.instances.size());

    for (const std::string_view rotation : {"20", "36", "68", "1", "5"})
    {
      ExpectRefusal(VoxError::UnsupportedRotation, WithPlacement({}, 0, {{{"_t", "0 0 0"}, {"_r", std::string(rotation)}}}),
                    std::format(L"_r {}", Widen(rotation)));
    }

    std::vector<Bytes> chunks = LChunks();
    chunks[ROOT_CHUNK] = TransformChunk(0, {}, 1, -1, {{{"_r", "20"}}});
    ExpectRefusal(VoxError::UnsupportedRotation, VoxFile(chunks), L"_r on the root");
  }

  // Design/NeuronVoxelFormat.md §6.1: a model odd in every dimension may be turned by any of the cube's 24 rotations,
  // which the reader conjugates into the engine's axes. A model with an even dimension may not, and a reflection or a
  // value that names one column twice is refused on either. All 128 values of the seven bits are tried.
  TEST_METHOD(TurnsOnlyModelsOddInEveryDimension)
  {
    const std::array<FileVoxel, 1> middle{{{1, 0, 2, 7}}};
    std::size_t turned = 0;
    std::size_t reflections = 0;
    std::size_t malformed = 0;
    for (std::uint32_t bits = 0; bits < 128; ++bits)
    {
      const std::string rotation = std::to_string(bits);
      const std::wstring what = std::format(L"_r {}", bits);
      const std::optional<std::array<std::array<float, 3>, 3>> rows = DecodeRotation(bits);
      const bool proper =
        rows && NeuronCore::Dot(NeuronCore::Cross(Multiply(*rows, {1.0f, 0.0f, 0.0f}), Multiply(*rows, {0.0f, 1.0f, 0.0f})),
                                Multiply(*rows, {0.0f, 0.0f, 1.0f})) > 0.0f;
      reflections += rows && !proper ? 1 : 0;
      malformed += rows ? 0 : 1;

      const Bytes odd = TurnedModel({3, 1, 5}, middle, "10 20 30", rotation);
      if (!proper)
      {
        ExpectRefusal(VoxError::UnsupportedRotation, odd, what + L" on a model odd in every dimension");
        ExpectRefusal(VoxError::UnsupportedRotation, TurnedModel(L_SIZE, L_VOXELS, "0 0 0", rotation), what + L" on the L");
        continue;
      }
      ++turned;
      const NeuronCore::VoxModel model = ExpectAccepted(odd, what);
      const NeuronCore::ModelInstance& instance = model.instances.front();
      const NeuronCore::Rotation expected = EngineRotation(*rows);
      AreEqualFloat3(expected.axisX, instance.rotation.axisX, (what + L": the image of +X").c_str());
      AreEqualFloat3(expected.axisY, instance.rotation.axisY, (what + L": the image of +Y").c_str());
      AreEqualFloat3(expected.axisZ, instance.rotation.axisZ, (what + L": the image of +Z").c_str());
      // The origin is where the unturned model's corner lies, whatever the turn: (10, 30, 20) minus floor((3, 5, 1) / 2).
      AreEqualInt3({9, 28, 20}, instance.origin, what.c_str());

      // Even in one dimension only, each dimension in turn: in MagicaVoxel's axes x, then z, then y, which are the
      // engine's x, y and z.
      const std::array<FileVoxel, 1> corner{{{0, 0, 0, 7}}};
      for (const Int3 size : {Int3{2, 1, 1}, Int3{1, 1, 2}, Int3{1, 2, 1}})
      {
        const Bytes even = TurnedModel(size, corner, "0 0 0", rotation);
        const std::wstring evenWhat = std::format(L"{} on a model of {} x {} x {}", what, size.x, size.y, size.z);
        if (bits == 4)
        {
          static_cast<void>(ExpectAccepted(even, evenWhat + L": the identity"));
        }
        else
        {
          ExpectRefusal(VoxError::UnsupportedRotation, even, evenWhat);
        }
      }
    }
    Assert::AreEqual(std::size_t{24}, turned, L"the cube's rotations");
    Assert::AreEqual(std::size_t{24}, reflections, L"the cube's reflections");
    Assert::AreEqual(std::size_t{80}, malformed, L"values that name a column twice or out of range");

    for (const std::string_view text : {"", "4 ", "+4", "-4", "128", "260", "four", "4.0"})
    {
      ExpectRefusal(VoxError::UnsupportedRotation, TurnedModel({3, 1, 5}, middle, "0 0 0", text), L"_r \"" + Widen(text) + L"\"");
    }
  }

  // A turned group would turn its models about the group's own pivot, which has no tested answer, so only the transform
  // directly above a model may turn it (§6.1).
  TEST_METHOD(RefusesTurnedGroups)
  {
    std::vector<Bytes> chunks = OneModelChunks(ARROW_SIZE, ARROW_VOXELS, "0 0 0");
    chunks[ROOT_CHUNK] = TransformChunk(0, {}, 1, -1, {{{"_r", std::string(Y_TO_X_ROTATION)}}});
    ExpectRefusal(VoxError::UnsupportedRotation, VoxFile(chunks), L"the root turned");

    chunks = OneModelChunks(ARROW_SIZE, ARROW_VOXELS, "0 0 0");
    chunks[PLACEMENT_CHUNK] = TransformChunk(2, {}, 4, 0, {{{"_r", std::string(Y_TO_X_ROTATION)}}});
    chunks.push_back(GroupChunk(4, {}, {5}));
    chunks.push_back(TransformChunk(5, {}, 3, 0, {{}}));
    ExpectRefusal(VoxError::UnsupportedRotation, VoxFile(chunks), L"a group turned");
  }

  // Design/NeuronVoxelFormat.md §6.1: each model carries the _name of the transform that places it, which is how the
  // importer tells parts from markers. A name on a transform above a group names no model, and is not read.
  TEST_METHOD(ReadsTheNamesOfPlacingNodes)
  {
    const std::vector<Bytes> chunks{
      SizeChunk(L_SIZE),
      VoxelsChunk(L_VOXELS),
      SizeChunk(DOT_SIZE),
      VoxelsChunk(DOT_VOXELS),
      TransformChunk(0, {{"_name", "scene"}}, 1, -1, {{}}),
      GroupChunk(1, {}, {2, 4, 6}),
      TransformChunk(2, {{"_name", "hull"}}, 3, 0, {{{"_t", "0 0 0"}}}),
      ShapeChunk(3, {0}),
      TransformChunk(4, {{"_name", "hull@engine.main"}}, 5, 0, {{{"_t", "5 0 0"}}}),
      ShapeChunk(5, {1}),
      TransformChunk(6, {}, 7, 0, {{{"_t", "0 9 0"}}}),
      ShapeChunk(7, {1}),
      PaletteChunk(),
    };
    const NeuronCore::VoxModel model = ExpectAccepted(VoxFile(chunks), L"three instances, two named");
    Assert::AreEqual(std::size_t{3}, model.instances.size());
    Assert::AreEqual(std::string("hull"), model.instances[0].name);
    Assert::AreEqual(std::string("hull@engine.main"), model.instances[1].name);
    Assert::AreEqual(std::string(), model.instances[2].name, L"an unnamed node");
    for (const NeuronCore::ModelInstance& instance : model.instances)
    {
      AreEqualFloat3({1.0f, 0.0f, 0.0f}, instance.rotation.axisX, L"unturned");
      AreEqualFloat3({0.0f, 1.0f, 0.0f}, instance.rotation.axisY, L"unturned");
      AreEqualFloat3({0.0f, 0.0f, 1.0f}, instance.rotation.axisZ, L"unturned");
    }
  }

  // Design/NeuronVoxelFormat.md §9: an asymmetric model with a voxel only at MagicaVoxel's (1, 2, 3), and a marker turned
  // to point along +X. The voxel lands at the engine's (1, 3, 2) and the marker's forward, +Z in its own frame, at +X.
  TEST_METHOD(ReadsAMarkerInTheEnginesAxes)
  {
    const std::array<FileVoxel, 1> single{{{1, 2, 3, 9}}};
    const std::vector<Bytes> chunks{
      SizeChunk(L_SIZE),
      VoxelsChunk(single),
      SizeChunk(ARROW_SIZE),
      VoxelsChunk(ARROW_VOXELS),
      TransformChunk(0, {}, 1, -1, {{}}),
      GroupChunk(1, {}, {2, 4}),
      TransformChunk(2, {{"_name", "main"}}, 3, 0, {{{"_t", "0 0 0"}}}),
      ShapeChunk(3, {0}),
      TransformChunk(4, {{"_name", "main@weapon.front"}}, 5, 0, {{{"_t", "10 20 30"}, {"_r", std::string(Y_TO_X_ROTATION)}}}),
      ShapeChunk(5, {1}),
      PaletteChunk(),
    };
    const NeuronCore::VoxModel model = ExpectAccepted(VoxFile(chunks), L"a model and a turned marker");
    Assert::AreEqual(NeuronCore::PackVoxelRecord({1, 3, 2, 8}), model.records[model.instances[0].firstRecord], L"the voxel, swapped");

    const NeuronCore::ModelInstance& marker = model.instances[1];
    AreEqualFloat3({1.0f, 0.0f, 0.0f}, NeuronCore::RotateVector(marker.rotation, {0.0f, 0.0f, 1.0f}), L"forward points along +X");
    AreEqualFloat3({0.0f, 1.0f, 0.0f}, NeuronCore::RotateVector(marker.rotation, {0.0f, 1.0f, 0.0f}), L"up stays +Y");
    // Its centre voxel stays at its translation, (10, 30, 20) in the engine's axes, whatever the turn.
    AreEqualInt3({10, 30, 20}, marker.origin + Int3{marker.size.x / 2, marker.size.y / 2, marker.size.z / 2}, L"the centre voxel's cell");

    // The arrow runs along +X now, through its centre voxel: from (9, 30, 20) to (11, 30, 20), where unturned it ran along
    // +Z from (10, 30, 19). The box around the model and the arrow holds the arrow where its turn puts it.
    NeuronCore::VoxModel arrow = model;
    arrow.instances.erase(arrow.instances.begin());
    const std::optional<NeuronCore::VoxelBounds> bounds = NeuronCore::OccupiedBounds(arrow);
    Assert::IsTrue(bounds.has_value(), L"the arrow has bounds");
    AreEqualInt3({9, 30, 20}, bounds.value_or(NeuronCore::VoxelBounds{}).lower, L"the turned arrow's lower corner");
    AreEqualInt3({12, 31, 21}, bounds.value_or(NeuronCore::VoxelBounds{}).upper, L"the turned arrow's upper corner");
  }

  TEST_METHOD(RefusesAnimation)
  {
    ExpectRefusal(VoxError::UnsupportedAnimation, WithPlacement({}, 0, {{{"_t", "0 0 0"}}, {{"_f", "10"}, {"_t", "1 0 0"}}}),
                  L"two frames");
    ExpectRefusal(VoxError::UnsupportedAnimation, WithPlacement({}, 0, {}), L"no frame");

    std::vector<Bytes> chunks = LChunks();
    chunks[SHAPE_CHUNK] = ShapeChunk(3, {0, 0});
    ExpectRefusal(VoxError::UnsupportedAnimation, VoxFile(chunks), L"a shape of two models");
    chunks[SHAPE_CHUNK] = ShapeChunk(3, {});
    ExpectRefusal(VoxError::UnsupportedAnimation, VoxFile(chunks), L"a shape of no model");
  }

  TEST_METHOD(RefusesBrokenSceneGraphs)
  {
    const auto refuseWith = [](std::size_t _index, const Bytes& _chunk, const wchar_t* _case)
    {
      std::vector<Bytes> chunks = LChunks();
      chunks[_index] = _chunk;
      ExpectRefusal(VoxError::BadSceneGraph, VoxFile(chunks), _case);
    };
    refuseWith(GROUP_CHUNK, GroupChunk(1, {}, {7}), L"a child that does not exist");
    refuseWith(GROUP_CHUNK, GroupChunk(1, {}, {2, 0}), L"a cycle back to the root");
    refuseWith(GROUP_CHUNK, GroupChunk(1, {}, {2, 2}), L"a node reached twice");
    refuseWith(SHAPE_CHUNK, ShapeChunk(3, {5}), L"a model that does not exist");
    refuseWith(SHAPE_CHUNK, GroupChunk(1, {}, {}), L"two nodes with one id");

    // A chain of transforms deeper than any scene MagicaVoxel writes.
    std::vector<Bytes> chunks{SizeChunk(L_SIZE), VoxelsChunk(L_VOXELS), PaletteChunk()};
    constexpr std::int32_t CHAIN_LENGTH = 70;
    for (std::int32_t node = 0; node < CHAIN_LENGTH; ++node)
    {
      chunks.push_back(TransformChunk(node, {}, node + 1, -1, {{}}));
    }
    chunks.push_back(ShapeChunk(CHAIN_LENGTH, {0}));
    ExpectRefusal(VoxError::BadSceneGraph, VoxFile(chunks), L"70 nested transforms");
  }

  TEST_METHOD(SkipsHiddenNodesAndLayers)
  {
    const std::vector<Bytes> chunks{
      SizeChunk(L_SIZE),
      VoxelsChunk(L_VOXELS),
      SizeChunk(DOT_SIZE),
      VoxelsChunk(DOT_VOXELS),
      TransformChunk(0, {}, 1, -1, {{}}),
      GroupChunk(1, {}, {2, 4, 6, 8}),
      TransformChunk(2, {}, 3, 0, {{{"_t", "0 0 0"}}}),
      ShapeChunk(3, {0}),
      // A hidden node is not read further, so not even its rotation is refused.
      TransformChunk(4, {{"_hidden", "1"}}, 5, 0, {{{"_t", "5 5 5"}, {"_r", "20"}}}),
      ShapeChunk(5, {1}),
      TransformChunk(6, {}, 7, 2, {{{"_t", "0 9 0"}}}),
      ShapeChunk(7, {1}),
      TransformChunk(8, {{"_hidden", "0"}}, 9, 1, {{}}),
      GroupChunk(9, {{"_hidden", "1"}}, {10}),
      TransformChunk(10, {}, 11, 1, {{}}),
      ShapeChunk(11, {1}),
      LayerChunk(0, {{"_name", "shown"}}),
      LayerChunk(1, {{"_hidden", "0"}}),
      LayerChunk(2, {{"_hidden", "1"}}),
      PaletteChunk(),
    };
    const NeuronCore::VoxModel model = ExpectAccepted(VoxFile(chunks), L"hidden parts");
    Assert::AreEqual(std::size_t{1}, model.instances.size(), L"only the L is visible");
    Assert::AreEqual(3u, model.instances.front().recordCount);
    Assert::AreEqual(std::size_t{3}, model.records.size());
  }

  TEST_METHOD(AppliesNestedTranslations)
  {
    const std::array<FileVoxel, 1> voxels{{{2, 3, 4, 1}}};
    std::vector<Bytes> chunks = OneModelChunks({3, 4, 5}, voxels, "10 20 30");
    chunks[ROOT_CHUNK] = TransformChunk(0, {}, 1, -1, {{{"_t", "1 2 3"}}});
    const NeuronCore::VoxModel model = ExpectAccepted(VoxFile(chunks), L"two translations");
    // In the engine's axes the translations sum to (11, 33, 22) and the size is (3, 5, 4), whose half is (1, 2, 2).
    AreEqualInt3({10, 31, 20}, model.instances.front().origin, L"origin");

    const NeuronCore::VoxModel negative = ExpectAccepted(VoxFile(OneModelChunks({3, 4, 5}, voxels, "-7 -8 -9")), L"negative");
    AreEqualInt3({-8, -11, -10}, negative.instances.front().origin, L"negative origin");
  }

  TEST_METHOD(RefusesFarTranslations)
  {
    const NeuronCore::VoxModel edge = ExpectAccepted(VoxFile(OneModelChunks(DOT_SIZE, DOT_VOXELS, "1048576 -1048576 0")), L"the limit");
    AreEqualInt3({1048576, 0, -1048576}, edge.instances.front().origin, L"origin at the limit");

    for (const std::string_view translation : {"1048577 0 0", "0 -1048577 0", "0 0 2147483647", "-2147483648 0 0"})
    {
      ExpectRefusal(VoxError::TranslationOutOfRange, VoxFile(OneModelChunks(DOT_SIZE, DOT_VOXELS, translation)),
                    std::format(L"_t {}", Widen(translation)));
    }

    std::vector<Bytes> chunks = OneModelChunks(DOT_SIZE, DOT_VOXELS, "1 0 0");
    chunks[ROOT_CHUNK] = TransformChunk(0, {}, 1, -1, {{{"_t", "1048576 0 0"}}});
    ExpectRefusal(VoxError::TranslationOutOfRange, VoxFile(chunks), L"a sum beyond the limit");
  }

  TEST_METHOD(PlacesInstancesInSceneOrder)
  {
    const NeuronCore::VoxModel model = ExpectAccepted(ThreeInstanceFile(), L"three instances");
    Assert::AreEqual(std::size_t{3}, model.instances.size());
    const std::array<std::uint32_t, 3> firsts{0, 1, 4};
    const std::array<std::uint32_t, 3> counts{1, 3, 1};
    const std::array<Int3, 3> origins{Int3{0, 0, 0}, Int3{3, -2, -1}, Int3{0, 0, 9}};
    for (std::size_t i = 0; i < firsts.size(); ++i)
    {
      Assert::AreEqual(firsts[i], model.instances[i].firstRecord);
      Assert::AreEqual(counts[i], model.instances[i].recordCount);
      AreEqualInt3(origins[i], model.instances[i].origin, L"origin");
    }
    Assert::AreEqual(std::size_t{5}, model.records.size());
    Assert::AreEqual(NeuronCore::PackVoxelRecord({0, 0, 0, 4}), model.records[0]);
    Assert::AreEqual(NeuronCore::PackVoxelRecord({1, 3, 2, 15}), model.records[3]);
    Assert::AreEqual(model.records[0], model.records[4]);
  }

  TEST_METHOD(BoundsEveryVoxelOfEveryPart)
  {
    // The dot's cell at the origin, the L's three from (3, -2, -1) to (4, 1, 1), and the second dot's at (0, 0, 9).
    const NeuronCore::VoxModel model = ExpectAccepted(ThreeInstanceFile(), L"three instances");
    const std::optional<NeuronCore::VoxelBounds> bounds = NeuronCore::OccupiedBounds(model);
    Assert::IsTrue(bounds.has_value(), L"a model with voxels has bounds");
    AreEqualInt3({0, -2, -1}, bounds.value_or(NeuronCore::VoxelBounds{}).lower, L"lower");
    AreEqualInt3({5, 2, 10}, bounds.value_or(NeuronCore::VoxelBounds{}).upper, L"upper");

    NeuronCore::VoxModel empty = model;
    empty.records.clear();
    for (NeuronCore::ModelInstance& instance : empty.instances)
    {
      instance.firstRecord = 0;
      instance.recordCount = 0;
    }
    Assert::IsFalse(NeuronCore::OccupiedBounds(empty).has_value(), L"a model without voxels has none");
  }

  TEST_METHOD(ReadsEmissiveMaterials)
  {
    std::vector<Bytes> chunks = LChunks();
    chunks.push_back(MaterialChunk(5, {{"_type", "_emit"}, {"_emit", "0.25"}, {"_flux", "3"}}));
    chunks.push_back(MaterialChunk(6, {{"_type", "_metal"}, {"_rough", "0.1"}}));
    chunks.push_back(MaterialChunk(16, {{"_type", "_emit"}}));
    chunks.push_back(MaterialChunk(1, {{"_rough", "0.1"}, {"_ior", "0.3"}}));
    chunks.push_back(MaterialChunk(17, {{"_type", "_emit"}, {"_emit", "1"}}));
    chunks.push_back(MaterialChunk(0, {{"_type", "_emit"}, {"_emit", "1"}}));
    const NeuronCore::VoxModel model = ExpectAccepted(VoxFile(chunks), L"materials");

    Assert::IsTrue(model.palette[4].emissive, L"entry 5");
    Assert::AreEqual(0.25f, model.palette[4].emit, L"entry 5");
    Assert::AreEqual(3.0f, model.palette[4].flux, L"entry 5");
    Assert::IsFalse(model.palette[5].emissive, L"entry 6 is metal");
    Assert::IsTrue(model.palette[15].emissive, L"entry 16");
    Assert::AreEqual(0.0f, model.palette[15].emit, L"entry 16 has no _emit");
    Assert::IsFalse(model.palette[0].emissive, L"entry 1 is diffuse");
    for (std::size_t i = 0; i < model.palette.size(); ++i)
    {
      if (i != 4 && i != 15)
      {
        Assert::IsFalse(model.palette[i].emissive, std::format(L"entry {}", i + 1).c_str());
      }
    }
  }

  TEST_METHOD(KeepsRenderObjectsVerbatim)
  {
    const NeuronCore::VoxAttributes sun{{"_type", "_inf"}, {"_i", "0.7"}, {"_angle", "50 50"}};
    const NeuronCore::VoxAttributes lens{{"_type", "_lens"}, {"_fov", "45"}};
    std::vector<Bytes> chunks = LChunks();
    chunks.push_back(RenderObjectChunk(sun));
    chunks.push_back(RenderObjectChunk(lens));
    const NeuronCore::VoxModel model = ExpectAccepted(VoxFile(chunks), L"render objects");
    Assert::AreEqual(std::size_t{2}, model.renderObjects.size());
    Assert::IsTrue(model.renderObjects[0] == sun, L"the sun, in file order");
    Assert::IsTrue(model.renderObjects[1] == lens, L"the lens");
  }

  TEST_METHOD(SkipsUnknownChunks)
  {
    std::vector<Bytes> chunks = LChunks();
    chunks.insert(chunks.begin(), Chunk("META", {1, 0, 0, 0, 3, 0, 0, 0, '_', 'a', 'b', 1, 0, 0, 0, 'x'}));
    chunks.push_back(Chunk("NOTE", {0, 0, 0, 0}));
    chunks.push_back(Chunk("rCAM", Bytes(40, 7)));
    chunks.push_back(Chunk("IMAP", Bytes(256, 1)));
    chunks.push_back(Chunk("ABCD", {1, 2, 3}, SizeChunk({0, 0, 0}))); // not read, so neither are its children
    const NeuronCore::VoxModel model = ExpectAccepted(VoxFile(chunks), L"unknown chunks");
    Assert::AreEqual(std::size_t{3}, model.records.size());
  }

  TEST_METHOD(NamesEveryError)
  {
    const std::array<std::pair<VoxError, const char*>, 16> names{{
      {VoxError::FileNotFound, "FileNotFound"},
      {VoxError::ReadFailed, "ReadFailed"},
      {VoxError::NotAVoxFile, "NotAVoxFile"},
      {VoxError::UnsupportedVersion, "UnsupportedVersion"},
      {VoxError::Truncated, "Truncated"},
      {VoxError::MalformedChunk, "MalformedChunk"},
      {VoxError::ModelTooLarge, "ModelTooLarge"},
      {VoxError::VoxelOutOfBounds, "VoxelOutOfBounds"},
      {VoxError::DuplicateVoxel, "DuplicateVoxel"},
      {VoxError::ColorOutOfRange, "ColorOutOfRange"},
      {VoxError::MissingPalette, "MissingPalette"},
      {VoxError::MissingSceneGraph, "MissingSceneGraph"},
      {VoxError::BadSceneGraph, "BadSceneGraph"},
      {VoxError::UnsupportedRotation, "UnsupportedRotation"},
      {VoxError::UnsupportedAnimation, "UnsupportedAnimation"},
      {VoxError::TranslationOutOfRange, "TranslationOutOfRange"},
    }};
    for (const auto& [error, name] : names)
    {
      Assert::AreEqual(name, NeuronCore::VoxErrorName(error));
    }
  }

  TEST_METHOD(LoadsFromDisk)
  {
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path missing = directory / "OutpostNeuronCoreTests-Missing.vox";
    std::filesystem::remove(missing);
    const auto notFound = NeuronCore::LoadVoxModel(missing);
    Assert::IsFalse(notFound.has_value(), L"a file that does not exist");
    Assert::AreEqual("FileNotFound", NeuronCore::VoxErrorName(notFound.error()));

    const Bytes file = WithTranslation("1 2 3");
    const std::filesystem::path path = directory / "OutpostNeuronCoreTests-LoadsFromDisk.vox";
    {
      std::ofstream stream(path, std::ios::binary | std::ios::trunc);
      stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
      Assert::IsTrue(stream.good(), L"writing the file");
    }
    const auto loaded = NeuronCore::LoadVoxModel(path);
    std::filesystem::remove(path);
    Assert::IsTrue(loaded.has_value(), L"loading the file");
    Assert::IsTrue(loaded->records == ExpectAccepted(file, L"parsing the file").records, L"the same records as parsing");
  }

  TEST_METHOD(DrawsVoxelsAsUnitCubes)
  {
    const NeuronCore::ModelInstance instance{{8, -21, 28}, L_SIZE, 0, 3};
    const NeuronCore::Box box = NeuronCore::VoxelBox(instance, NeuronCore::PackVoxelRecord({1, 2, 3, 15}));
    Assert::AreEqual(9.5f, box.center.x);
    Assert::AreEqual(-18.5f, box.center.y);
    Assert::AreEqual(31.5f, box.center.z);
    for (const float radius : {box.radius.x, box.radius.y, box.radius.z})
    {
      Assert::AreEqual(0.5f, radius);
    }
    for (const float inverse : {box.invRadius.x, box.invRadius.y, box.invRadius.z})
    {
      Assert::AreEqual(2.0f, inverse);
    }
    Assert::AreEqual(1.0f, box.axisX.x);
    Assert::AreEqual(1.0f, box.axisY.y);
    Assert::AreEqual(1.0f, box.axisZ.z);
    Assert::AreEqual(0.0f, box.axisX.y + box.axisX.z + box.axisY.x + box.axisY.z + box.axisZ.x + box.axisZ.y);
  }
};

} // namespace NeuronCoreTests
