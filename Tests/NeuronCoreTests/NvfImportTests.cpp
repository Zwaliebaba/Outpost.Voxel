#include "pch.h"

#include "NvfGolden.h"
#include "NvfImport.h"
#include "NvfModel.h"
#include "Quaternion.h"
#include "RepositoryFile.h"
#include "RigidTransform.h"
#include "VoxFile.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <set>
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

using NeuronCore::Float3;
using NeuronCore::Int3;
using NeuronCore::NvfImportRefusal;
using NeuronCore::NvfModel;

// One model of a .vox scene, placed by a transform of its own under the root group, as MagicaVoxel writes a scene.
// Sizes, voxels and translations are in MagicaVoxel's axes, as the file stores them.
struct SceneNode
{
  std::string name;
  Int3 size;
  std::vector<FileVoxel> voxels;
  std::string translation;
  std::string rotation; // _r; none when empty
};

[[nodiscard]] Bytes SceneFile(const std::vector<SceneNode>& _nodes)
{
  std::vector<Bytes> chunks;
  std::vector<std::int32_t> children;
  for (const SceneNode& node : _nodes)
  {
    chunks.push_back(SizeChunk(node.size));
    chunks.push_back(VoxelsChunk(node.voxels));
  }
  chunks.push_back(TransformChunk(0, {}, 1, -1, {{}}));
  children.reserve(_nodes.size());
  for (std::size_t i = 0; i < _nodes.size(); ++i)
  {
    children.push_back(static_cast<std::int32_t>(2 + 2 * i));
  }
  chunks.push_back(GroupChunk(1, {}, children));
  for (std::size_t i = 0; i < _nodes.size(); ++i)
  {
    const SceneNode& node = _nodes[i];
    NeuronCore::VoxAttributes attributes;
    if (!node.name.empty())
    {
      attributes.emplace("_name", node.name);
    }
    NeuronCore::VoxAttributes frame{{"_t", node.translation}};
    if (!node.rotation.empty())
    {
      frame.emplace("_r", node.rotation);
    }
    const auto transform = static_cast<std::int32_t>(2 + 2 * i);
    chunks.push_back(TransformChunk(transform, attributes, transform + 1, 0, {frame}));
    chunks.push_back(ShapeChunk(transform + 1, {static_cast<std::int32_t>(i)}));
  }
  chunks.push_back(PaletteChunk());
  chunks.push_back(MaterialChunk(10, {{"_type", "_emit"}, {"_emit", "0.6"}, {"_flux", "2"}}));
  return VoxFile(chunks);
}

[[nodiscard]] std::wstring Widen(std::string_view _text)
{
  return {_text.begin(), _text.end()};
}

[[nodiscard]] NeuronCore::VoxModel ReadScene(const std::vector<SceneNode>& _nodes)
{
  auto model = NeuronCore::ParseVoxModel(SceneFile(_nodes));
  if (!model)
  {
    Assert::Fail(std::format(L"the scene was refused: {}", Widen(NeuronCore::VoxErrorName(model.error()))).c_str());
  }
  return std::move(*model);
}

[[nodiscard]] std::wstring Describe(const std::vector<NeuronCore::NvfImportError>& _errors)
{
  std::wstring text;
  for (const NeuronCore::NvfImportError& error : _errors)
  {
    text += std::format(L"{}{}: {}", text.empty() ? L"" : L"; ", Widen(error.node), Widen(NeuronCore::NvfImportRefusalName(error.refusal)));
  }
  return text;
}

[[nodiscard]] NvfModel ExpectImported(const std::vector<SceneNode>& _nodes, const NvfModel* _previous, const std::wstring& _case)
{
  auto model = NeuronCore::ImportVoxModel(ReadScene(_nodes), _previous);
  if (!model)
  {
    Assert::Fail(std::format(L"{}: refused: {}", _case, Describe(model.error())).c_str());
  }
  return std::move(*model);
}

// The import of _nodes is refused with exactly _expected, in any order: each refusal and the node it names.
void ExpectRefusals(const std::vector<SceneNode>& _nodes, const NvfModel* _previous,
                    const std::vector<std::pair<NvfImportRefusal, std::string>>& _expected, const std::wstring& _case)
{
  const auto model = NeuronCore::ImportVoxModel(ReadScene(_nodes), _previous);
  Assert::IsFalse(model.has_value(), (_case + L": the scene was imported").c_str());
  std::multiset<std::pair<std::string, std::string>> actual;
  for (const NeuronCore::NvfImportError& error : model.error())
  {
    actual.emplace(NeuronCore::NvfImportRefusalName(error.refusal), error.node);
  }
  std::multiset<std::pair<std::string, std::string>> expected;
  for (const auto& [refusal, node] : _expected)
  {
    expected.emplace(NeuronCore::NvfImportRefusalName(refusal), node);
  }
  Assert::IsTrue(actual == expected, (_case + L": refused " + Describe(model.error())).c_str());
}

void AreEqualInt3(Int3 _expected, Int3 _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _what);
  Assert::AreEqual(_expected.y, _actual.y, _what);
  Assert::AreEqual(_expected.z, _actual.z, _what);
}

void AreEqualFloat3(Float3 _expected, Float3 _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _what);
  Assert::AreEqual(_expected.y, _actual.y, _what);
  Assert::AreEqual(_expected.z, _actual.z, _what);
}

void AreEqualQuaternion(NeuronCore::Quaternion _expected, NeuronCore::Quaternion _actual, const wchar_t* _what)
{
  Assert::AreEqual(_expected.x, _actual.x, _what);
  Assert::AreEqual(_expected.y, _actual.y, _what);
  Assert::AreEqual(_expected.z, _actual.z, _what);
  Assert::AreEqual(_expected.w, _actual.w, _what);
}

[[nodiscard]] const NeuronCore::NvfHardpoint& HardpointNamed(const NvfModel& _model, std::string_view _name)
{
  const auto found = std::ranges::find(_model.hardpoints, _name, &NeuronCore::NvfHardpoint::name);
  Assert::IsTrue(found != _model.hardpoints.end(), (L"no hardpoint " + Widen(_name)).c_str());
  return *found;
}

// A hull of 5 x 3 x 7 in MagicaVoxel's axes, which the engine sees as 5 x 7 x 3, its corners filled.
[[nodiscard]] SceneNode Hull(std::string _name = "hull")
{
  return {std::move(_name), {5, 3, 7}, {{0, 0, 0, 1}, {4, 2, 6, 2}, {2, 1, 3, 10}}, "0 0 0", {}};
}

// A marker drawn as §5 draws one: an arrow along MagicaVoxel's +Y, the engine's forward.
[[nodiscard]] SceneNode Marker(std::string _name, std::string _translation, std::string _rotation = {})
{
  return {std::move(_name), {1, 3, 1}, {{0, 0, 0, 5}, {0, 1, 0, 5}, {0, 2, 0, 5}}, std::move(_translation), std::move(_rotation)};
}

// _r decoded as MagicaVoxel-file-format-vox-extension.txt describes it: the matrix row by row, or nothing when a column
// is named twice or out of range.
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

[[nodiscard]] Float3 Multiply(const std::array<std::array<float, 3>, 3>& _rows, Float3 _vector) noexcept
{
  const auto row = [&_rows, _vector](std::size_t _row)
  { return _rows[_row][0] * _vector.x + _rows[_row][1] * _vector.y + _rows[_row][2] * _vector.z; };
  return {row(0), row(1), row(2)};
}

[[nodiscard]] constexpr Float3 Swap(Float3 _vector) noexcept
{
  return {_vector.x, _vector.z, _vector.y};
}

[[nodiscard]] bool SameRotation(const NeuronCore::Rotation& _a, const NeuronCore::Rotation& _b) noexcept
{
  const auto same = [](Float3 _x, Float3 _y) { return _x.x == _y.x && _x.y == _y.y && _x.z == _y.z; };
  return same(_a.axisX, _b.axisX) && same(_a.axisY, _b.axisY) && same(_a.axisZ, _b.axisZ);
}

} // namespace

TEST_CLASS(NvfImportTests)
{
public:
  // §5: an unnamed model is the part `main` when it is the file's only part, as the three assets are. The part keeps the
  // .vox's records in their order, its origin as its translation, and its geometric centre as its pivot.
  TEST_METHOD(ImportsOneUnnamedModelAsMain)
  {
    const NeuronCore::VoxModel vox = ReadScene({{"", {5, 3, 7}, {{0, 0, 0, 1}, {4, 2, 6, 2}, {2, 1, 3, 10}}, "10 20 30", {}}});
    const auto imported = NeuronCore::ImportVoxModel(vox, nullptr);
    Assert::IsTrue(imported.has_value(), L"one unnamed model");
    const NvfModel& model = *imported;
    Assert::AreEqual(std::size_t{1}, model.parts.size());
    const NeuronCore::NvfPart& part = model.parts.front();
    Assert::AreEqual(std::string("main"), part.path);
    Assert::AreEqual(NeuronCore::NVF_NO_PARENT, part.parent);
    AreEqualInt3({5, 7, 3}, part.size, L"size, in the engine's axes");
    AreEqualInt3(vox.instances.front().origin, part.translation, L"part 0 sits at its instance's origin");
    AreEqualFloat3({2.5f, 3.5f, 1.5f}, part.pivot, L"the geometric centre");
    Assert::IsFalse(part.pivotAuthored, L"a default pivot");
    Assert::IsTrue(model.records == vox.records, L"the records, in the .vox's order");
    Assert::AreEqual(0u, part.firstVoxel);
    Assert::AreEqual(3u, part.voxelCount);
    Assert::IsTrue(model.palette[9].emissive && model.palette[9].emit == 0.6f, L"the palette, materials and all");
    Assert::IsTrue(model.hardpoints.empty(), L"no hardpoints");
    Assert::IsTrue(NeuronCore::SerializeNvfModel(model).has_value(), L"the model is one the writer takes");
  }

  // §6.2: parts are ordered by path, which puts every parent first, and each is translated in its parent's space.
  TEST_METHOD(BuildsThePartTreeByPath)
  {
    const std::vector<SceneNode> nodes{
      {"hull/turret/barrel", {1, 4, 1}, {{0, 0, 0, 3}, {0, 3, 0, 4}}, "4 5 12", {}},
      {"hull", {5, 3, 7}, {{0, 0, 0, 1}, {4, 2, 6, 2}}, "0 0 0", {}},
      {"hull/turret", {3, 3, 2}, {{1, 1, 0, 6}}, "2 1 8", {}},
      {"hull/wing", {1, 1, 1}, {{0, 0, 0, 7}}, "-6 0 2", {}},
    };
    const NeuronCore::VoxModel vox = ReadScene(nodes);
    const NvfModel model = ExpectImported(nodes, nullptr, L"four parts");
    const std::array<const char*, 4> paths{"hull", "hull/turret", "hull/turret/barrel", "hull/wing"};
    const std::array<std::uint32_t, 4> parents{NeuronCore::NVF_NO_PARENT, 0, 1, 0};
    const std::array<std::size_t, 4> instances{1, 2, 0, 3};
    Assert::AreEqual(paths.size(), model.parts.size());
    std::uint32_t firstVoxel = 0;
    for (std::size_t i = 0; i < paths.size(); ++i)
    {
      const NeuronCore::NvfPart& part = model.parts[i];
      const NeuronCore::ModelInstance& instance = vox.instances[instances[i]];
      const std::wstring what = Widen(paths[i]);
      Assert::AreEqual(std::string(paths[i]), part.path, what.c_str());
      Assert::AreEqual(parents[i], part.parent, what.c_str());
      const Int3 parentOrigin = part.parent == NeuronCore::NVF_NO_PARENT ? Int3{0, 0, 0} : vox.instances[instances[part.parent]].origin;
      AreEqualInt3(instance.origin - parentOrigin, part.translation, (what + L": translation in the parent's space").c_str());
      Assert::AreEqual(firstVoxel, part.firstVoxel, what.c_str());
      Assert::AreEqual(instance.recordCount, part.voxelCount, what.c_str());
      Assert::IsTrue(std::ranges::equal(std::span(model.records).subspan(part.firstVoxel, part.voxelCount),
                                        std::span(vox.records).subspan(instance.firstRecord, instance.recordCount)),
                     (what + L": the part's records, in their order").c_str());
      firstVoxel += part.voxelCount;
    }
    Assert::IsTrue(NeuronCore::SerializeNvfModel(model).has_value(), L"the tree is one the writer takes");
  }

  // §6.2: a marker's position is the centre of its centre voxel in its part's space, its rotation comes from the table,
  // and the hardpoint carries FromVox. A pivot marker sets its part's pivot; the other parts keep the default.
  TEST_METHOD(TurnsMarkersIntoHardpointsAndPivots)
  {
    // The hull's origin is (-2, -3, -1) in the engine's axes: its translation, (0, 0, 0), less floor((5, 7, 3) / 2).
    const std::vector<SceneNode> nodes{
      Hull(),
      Marker("hull@weapon.front", "3 -2 4", "33"),
      Marker("hull@engine.main", "0 -4 1"),
      Marker("hull@pivot", "1 1 1", "33"),
      {"hull/turret", {3, 3, 3}, {{1, 1, 1, 9}}, "0 0 9", {}},
    };
    const NvfModel model = ExpectImported(nodes, nullptr, L"a hull with markers");
    Assert::AreEqual(std::size_t{2}, model.hardpoints.size(), L"two hardpoints; the pivot marker is not one");

    // weapon.front: its centre voxel is at its translation, (3, 4, -2) in the engine's axes, whatever its turn. Its
    // centre, in the hull's space, is (3, 4, -2) + ½ - (-2, -3, -1). _r 33 turns MagicaVoxel's +Y, its forward, to +X.
    const NeuronCore::NvfHardpoint& weapon = HardpointNamed(model, "weapon.front");
    Assert::AreEqual(0u, weapon.part);
    AreEqualFloat3({5.5f, 7.5f, -0.5f}, weapon.position, L"weapon.front's position");
    const NeuronCore::Rotation turned = NeuronCore::RotationOf(weapon.rotation);
    AreEqualFloat3({1.0f, 0.0f, 0.0f}, NeuronCore::RotateVector(turned, {0.0f, 0.0f, 1.0f}), L"weapon.front fires along +X");
    AreEqualFloat3({0.0f, 1.0f, 0.0f}, NeuronCore::RotateVector(turned, {0.0f, 1.0f, 0.0f}), L"weapon.front's up");
    Assert::IsTrue(weapon.fromVox, L"from a marker");
    Assert::IsTrue(NeuronCore::HardpointType(weapon) == "weapon", L"its type");

    const NeuronCore::NvfHardpoint& engine = HardpointNamed(model, "engine.main");
    AreEqualFloat3({2.5f, 4.5f, -2.5f}, engine.position, L"engine.main's position");
    AreEqualQuaternion({0.0f, 0.0f, 0.0f, 1.0f}, engine.rotation, L"an unturned marker");

    // The pivot marker sits at (1, 1, 1) in the engine's axes; its turn is not read, as a pivot has no orientation.
    AreEqualFloat3({3.5f, 4.5f, 2.5f}, model.parts[0].pivot, L"the hull's pivot, from its marker");
    Assert::IsFalse(model.parts[0].pivotAuthored, L"a marker's pivot is not Blender's");
    AreEqualFloat3({1.5f, 1.5f, 1.5f}, model.parts[1].pivot, L"the turret's default pivot");
    Assert::IsTrue(NeuronCore::SerializeNvfModel(model).has_value(), L"the model is one the writer takes");
  }

  // Design/Archive/NeuronVoxelFormat.md §9: every one of the 24 proper _r values reaches the table's quaternion, which turns the
  // hardpoint's forward, +Z, to where MagicaVoxel's matrix, swapped into the engine's axes, sends MagicaVoxel's +Y. The
  // reader refuses the 24 reflections (VoxModelTests).
  TEST_METHOD(MapsEveryRotationThroughTheTable)
  {
    std::size_t proper = 0;
    std::vector<NeuronCore::Quaternion> seen;
    for (std::uint32_t bits = 0; bits < 128; ++bits)
    {
      const std::optional<std::array<std::array<float, 3>, 3>> rows = DecodeRotation(bits);
      const bool isProper =
        rows && NeuronCore::Dot(NeuronCore::Cross(Multiply(*rows, {1.0f, 0.0f, 0.0f}), Multiply(*rows, {0.0f, 1.0f, 0.0f})),
                                Multiply(*rows, {0.0f, 0.0f, 1.0f})) > 0.0f;
      if (!isProper)
      {
        continue;
      }
      ++proper;
      const std::wstring what = std::format(L"_r {}", bits);
      const NvfModel model = ExpectImported({Hull(), Marker("hull@probe.forward", "0 0 0", std::to_string(bits))}, nullptr, what);
      const NeuronCore::Quaternion quaternion = HardpointNamed(model, "probe.forward").rotation;

      const NeuronCore::Rotation engine{Swap(Multiply(*rows, Swap({1.0f, 0.0f, 0.0f}))), Swap(Multiply(*rows, Swap({0.0f, 1.0f, 0.0f}))),
                                        Swap(Multiply(*rows, Swap({0.0f, 0.0f, 1.0f})))};
      const std::optional<NeuronCore::Quaternion> table = NeuronCore::CubeRotationQuaternion(engine);
      Assert::IsTrue(table.has_value(), (what + L": in the table").c_str());
      AreEqualQuaternion(table.value_or(NeuronCore::Quaternion{}), quaternion, (what + L": the table's quaternion").c_str());
      Assert::IsTrue(SameRotation(engine, NeuronCore::RotationOf(quaternion)),
                     (what + L": the quaternion gives the rotation exactly").c_str());
      AreEqualFloat3(Swap(Multiply(*rows, {0.0f, 1.0f, 0.0f})),
                     NeuronCore::RotateVector(NeuronCore::RotationOf(quaternion), {0.0f, 0.0f, 1.0f}), (what + L": forward").c_str());
      Assert::IsTrue(NeuronCore::IsUnitRotation(quaternion), (what + L": unit, w >= 0").c_str());
      // One spelling of each rotation: w > 0, or for a half turn w = 0 and the first nonzero of x, y and z positive.
      const bool firstPositive =
        quaternion.x > 0.0f || (quaternion.x == 0.0f && (quaternion.y > 0.0f || (quaternion.y == 0.0f && quaternion.z > 0.0f)));
      Assert::IsTrue(quaternion.w > 0.0f || (quaternion.w == 0.0f && firstPositive), (what + L": the table's spelling").c_str());
      Assert::IsTrue(std::ranges::none_of(seen,
                                          [quaternion](const NeuronCore::Quaternion& _other) {
                                            return _other.x == quaternion.x && _other.y == quaternion.y && _other.z == quaternion.z &&
                                                   _other.w == quaternion.w;
                                          }),
                     (what + L": one quaternion for each rotation").c_str());
      seen.push_back(quaternion);
    }
    Assert::AreEqual(std::size_t{24}, proper, L"the cube's rotations");
    Assert::IsFalse(NeuronCore::CubeRotationQuaternion({{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}).has_value(),
                    L"a reflection is not in the table");
    Assert::IsFalse(NeuronCore::CubeRotationQuaternion({{0.6f, 0.8f, 0.0f}, {-0.8f, 0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}}).has_value(),
                    L"a rotation off the cube's is not in the table");
  }

  // §6.2: re-importing keeps what Blender authored and replaces what came from markers. A pivot set in Blender survives
  // unless a marker now sets it, and goes with its part.
  TEST_METHOD(MergesWithThePreviousFile)
  {
    const std::vector<SceneNode> before{Hull(),
                                        {"hull/turret", {3, 3, 3}, {{1, 1, 1, 9}}, "0 0 9", {}},
                                        {"hull/wing", {1, 1, 1}, {{0, 0, 0, 7}}, "-6 0 2", {}},
                                        Marker("hull@engine.old", "0 -4 1")};
    NvfModel previous = ExpectImported(before, nullptr, L"the first import");
    // What Blender added: a hardpoint on the turret, and pivots on the turret, the hull and the wing.
    previous.hardpoints.push_back(
      {.name = "sensor.top", .part = 1, .position = {1.0f, 3.0f, 0.25f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .fromVox = false});
    previous.parts[1].pivot = {1.5f, 0.0f, 1.5f};
    previous.parts[1].pivotAuthored = true;
    previous.parts[0].pivot = {9.0f, 9.0f, 9.0f};
    previous.parts[0].pivotAuthored = true;
    previous.parts[2].pivot = {0.25f, 0.25f, 0.25f};
    previous.parts[2].pivotAuthored = true;

    // The voxel edit: the wing is gone, a part is added before the turret in path order, the engine marker is renamed,
    // and a pivot marker now sets the hull's pivot.
    const std::vector<SceneNode> after{Hull(),
                                       {"hull/antenna", {1, 3, 1}, {{0, 2, 0, 11}}, "0 1 6", {}},
                                       {"hull/turret", {3, 3, 3}, {{1, 1, 1, 9}, {0, 0, 0, 8}}, "0 0 9", {}},
                                       Marker("hull@engine.main", "0 -4 1"),
                                       Marker("hull@pivot", "1 1 1")};
    const NvfModel model = ExpectImported(after, &previous, L"the re-import");
    const std::array<const char*, 3> paths{"hull", "hull/antenna", "hull/turret"};
    Assert::AreEqual(paths.size(), model.parts.size());
    for (std::size_t i = 0; i < paths.size(); ++i)
    {
      Assert::AreEqual(std::string(paths[i]), model.parts[i].path);
    }

    Assert::AreEqual(std::size_t{2}, model.hardpoints.size(), L"engine.old is dropped for engine.main");
    const NeuronCore::NvfHardpoint& sensor = HardpointNamed(model, "sensor.top");
    Assert::AreEqual(2u, sensor.part, L"sensor.top follows the turret to its new index");
    AreEqualFloat3({1.0f, 3.0f, 0.25f}, sensor.position, L"sensor.top keeps its place in its part");
    Assert::IsFalse(sensor.fromVox, L"sensor.top is still Blender's");
    Assert::IsTrue(HardpointNamed(model, "engine.main").fromVox, L"engine.main comes from its marker");

    AreEqualFloat3({3.5f, 4.5f, 2.5f}, model.parts[0].pivot, L"the hull's pivot is its marker's, not Blender's");
    Assert::IsFalse(model.parts[0].pivotAuthored, L"a marker's pivot clears the flag");
    AreEqualFloat3({0.5f, 0.5f, 1.5f}, model.parts[1].pivot, L"the new antenna's default pivot");
    AreEqualFloat3({1.5f, 0.0f, 1.5f}, model.parts[2].pivot, L"the turret keeps Blender's pivot");
    Assert::IsTrue(model.parts[2].pivotAuthored, L"and its flag");

    // Importing again over the result changes nothing: what --check relies on.
    const auto first = NeuronCore::SerializeNvfModel(model);
    Assert::IsTrue(first.has_value(), L"the merged model is one the writer takes");
    const NvfModel again = ExpectImported(after, &model, L"a second re-import");
    const auto second = NeuronCore::SerializeNvfModel(again);
    Assert::IsTrue(second.has_value() && *first == *second, L"a second import writes the same bytes");
  }

  // §6.2: a hardpoint Blender authored on a part that is gone is an error, not a silent drop, and so is one that shares a
  // marker's name. The clash names the marker: the owner's answer is that Blender owns a refined hardpoint, so the marker
  // is what goes (§11, question 6).
  TEST_METHOD(RefusesOrphansAndClashes)
  {
    const std::vector<SceneNode> before{Hull(), {"hull/wing", {1, 1, 1}, {{0, 0, 0, 7}}, "-6 0 2", {}}};
    NvfModel previous = ExpectImported(before, nullptr, L"the first import");
    previous.hardpoints.push_back(
      {.name = "light.tip", .part = 1, .position = {0.5f, 0.5f, 1.0f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .fromVox = false});
    previous.hardpoints.push_back(
      {.name = "weapon.front", .part = 0, .position = {5.0f, 4.5f, 1.5f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .fromVox = false});

    ExpectRefusals({Hull(), Marker("hull@weapon.front", "3 -2 4")}, &previous,
                   {{NvfImportRefusal::OrphanHardpoint, "hull/wing@light.tip"}, {NvfImportRefusal::NameClash, "hull@weapon.front"}},
                   L"the wing gone, and weapon.front both Blender's and a marker's");

    NvfModel newer = previous;
    newer.unknownChunks = {"XTRA", "MATS"};
    ExpectRefusals({Hull(), {"hull/wing", {1, 1, 1}, {{0, 0, 0, 7}}, "-6 0 2", {}}}, &newer,
                   {{NvfImportRefusal::UnknownChunks, "XTRA, MATS"}}, L"a previous file with chunks this version does not know");
  }

  // Every refusal of §5 and §6.2 that a .vox alone can cause, each naming its node, all reported at once.
  TEST_METHOD(RefusesWhatItCannotImport)
  {
    ExpectRefusals({Hull("Hull"), Marker("hull@", "0 0 0"), Marker("hull@weapon", "0 0 0"), Marker("hull@pivot.x", "0 0 0"),
                    Marker("a@b@c.d", "0 0 0"), Marker("hull@Weapon.main", "0 0 0"), Marker("@weapon.main", "0 0 0")},
                   nullptr,
                   {{NvfImportRefusal::MalformedName, "Hull"},
                    {NvfImportRefusal::MalformedName, "hull@"},
                    {NvfImportRefusal::MalformedName, "hull@weapon"},
                    {NvfImportRefusal::MalformedName, "hull@pivot.x"},
                    {NvfImportRefusal::MalformedName, "a@b@c.d"},
                    {NvfImportRefusal::MalformedName, "hull@Weapon.main"},
                    {NvfImportRefusal::MalformedName, "@weapon.main"}},
                   L"malformed names");

    ExpectRefusals({Hull(), Hull("")}, nullptr, {{NvfImportRefusal::UnnamedPart, "(unnamed model 2)"}}, L"an unnamed model beside a part");
    ExpectRefusals({Marker("main@engine.main", "0 0 0")}, nullptr,
                   {{NvfImportRefusal::NoPart, ""}, {NvfImportRefusal::MissingPart, "main@engine.main"}}, L"markers and no part");
    ExpectRefusals({Hull(), Hull("wing"), Hull("tail")}, nullptr,
                   {{NvfImportRefusal::SecondRoot, "wing"}, {NvfImportRefusal::SecondRoot, "tail"}}, L"three roots");
    ExpectRefusals({Hull(), Hull("hull/turret/barrel"), Marker("hull/tail@light.tail", "0 0 0")}, nullptr,
                   {{NvfImportRefusal::MissingPart, "hull/turret/barrel"}, {NvfImportRefusal::MissingPart, "hull/tail@light.tail"}},
                   L"a part and a marker whose parts are missing");
    ExpectRefusals({Hull(), Hull(), Marker("hull@a.b", "0 0 0"), Marker("hull@a.b", "1 0 0"), Marker("hull@pivot", "0 0 0"),
                    Marker("hull@pivot", "0 0 1")},
                   nullptr,
                   {{NvfImportRefusal::DuplicateName, "hull"},
                    {NvfImportRefusal::DuplicateName, "hull@a.b"},
                    {NvfImportRefusal::DuplicateName, "hull@pivot"}},
                   L"a part, a hardpoint and a pivot twice");
    ExpectRefusals({Hull(), {"hull/turret", {3, 3, 3}, {{1, 1, 1, 9}}, "0 0 9", "33"}}, nullptr,
                   {{NvfImportRefusal::RotatedPart, "hull/turret"}}, L"a turned part");
    ExpectRefusals({Hull(), {"hull@light.wide", {2, 1, 1}, {{0, 0, 0, 5}}, "0 0 0", {}}}, nullptr,
                   {{NvfImportRefusal::EvenMarker, "hull@light.wide"}}, L"a marker two voxels wide");

    // Hidden models are not read (§5), so a file whose one model is hidden has no part.
    std::vector<Bytes> chunks = OneModelChunks({1, 1, 1}, std::array<FileVoxel, 1>{{{0, 0, 0, 1}}}, "0 0 0");
    chunks[4] = TransformChunk(2, {{"_hidden", "1"}}, 3, 0, {{{"_t", "0 0 0"}}});
    const auto hidden = NeuronCore::ParseVoxModel(VoxFile(chunks));
    Assert::IsTrue(hidden.has_value(), L"a file whose one model is hidden");
    const auto none = NeuronCore::ImportVoxModel(*hidden, nullptr);
    Assert::IsTrue(!none.has_value() && none.error().size() == 1 && none.error().front().refusal == NvfImportRefusal::NoPart,
                   L"no part at all");
  }

  // The limits of §4.4, which the importer checks before the writer would refuse the file.
  TEST_METHOD(RefusesMoreThanTheFormatHolds)
  {
    std::vector<SceneNode> parts{Hull()};
    for (std::uint32_t i = 0; i < NeuronCore::NVF_MAX_PARTS; ++i)
    {
      parts.push_back({std::format("hull/p{}", i), {1, 1, 1}, {{0, 0, 0, 1}}, "0 0 0", {}});
    }
    ExpectRefusals(parts, nullptr, {{NvfImportRefusal::TooManyParts, ""}}, L"1,025 parts");

    NvfModel previous = ExpectImported({Hull()}, nullptr, L"a hull");
    for (std::uint32_t i = 0; i < NeuronCore::NVF_MAX_HARDPOINTS; ++i)
    {
      previous.hardpoints.push_back({.name = std::format("light.l{}", i),
                                     .part = 0,
                                     .position = {0.0f, 0.0f, 0.0f},
                                     .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                                     .fromVox = false});
    }
    static_cast<void>(ExpectImported({Hull()}, &previous, L"4,096 hardpoints"));
    ExpectRefusals({Hull(), Marker("hull@engine.main", "0 0 0")}, &previous, {{NvfImportRefusal::TooManyHardpoints, ""}},
                   L"4,096 hardpoints and a marker");
  }

  // §3, §6.3: --dump's text of the golden file, pinned, so that a change of format is a visible diff.
  TEST_METHOD(DumpsTheGoldenFileAsText)
  {
    const auto model = NeuronCore::ParseNvfModel(ReadRepositoryFile(GOLDEN_NVF_PATH));
    Assert::IsTrue(model.has_value(), L"the golden file");
    constexpr std::string_view EXPECTED = R"(palette
   1: 000000ff
   2: 0000aaff
   3: 00aa00ff
   4: 00aaaaff
   5: aa0000ff
   6: aa00aaff
   7: aa5500ff
   8: aaaaaaff
   9: 55555580
  10: 5555ffff emissive emit 0.6 flux 2
  11: 55ff55ff
  12: 55ffffff emit 0.125 flux 1
  13: ff5555ff
  14: ff55ffff
  15: ffff55ff emissive emit 0.25 flux 3.5
  16: ffffffff emissive emit 1 flux 0
parts: 3, voxels: 16
  hull: size 5 x 3 x 7, at (-2, 0, -3) in the model, pivot (2.5, 1.5, 3.5), voxels 0 to 7
  hull/turret: size 3 x 2 x 3, at (1, 3, 2) in hull, pivot (1.5, 0, 1.5) authored, voxels 8 to 11
  hull/turret/barrel: size 1 x 1 x 4, at (1, 1, 3) in hull/turret, pivot (0.5, 0.5, 2), voxels 12 to 15
hardpoints: 4
  dock.aft: dock on hull, at (2.5, 3, 3.5), rotation (0.70710677, 0, 0, 0.70710677), from the .vox
  engine.main: engine on hull, at (2.5, 1.5, 0.5), rotation (0, 1, 0, 0), from the .vox
  sensor.top.left: sensor on hull/turret, at (0, 2, 1.5), rotation (0, 0, 0, 1)
  weapon.main: weapon on hull/turret/barrel, at (0.5, 0.5, 4), rotation (0, 0.25881904, 0, 0.9659258)
)";
    Assert::AreEqual(std::string(EXPECTED), NeuronCore::DumpNvfModel(*model));
  }

  TEST_METHOD(NamesEveryRefusal)
  {
    const std::array<std::pair<NvfImportRefusal, const char*>, 13> names{{
      {NvfImportRefusal::MalformedName, "MalformedName"},
      {NvfImportRefusal::UnnamedPart, "UnnamedPart"},
      {NvfImportRefusal::NoPart, "NoPart"},
      {NvfImportRefusal::SecondRoot, "SecondRoot"},
      {NvfImportRefusal::MissingPart, "MissingPart"},
      {NvfImportRefusal::DuplicateName, "DuplicateName"},
      {NvfImportRefusal::RotatedPart, "RotatedPart"},
      {NvfImportRefusal::EvenMarker, "EvenMarker"},
      {NvfImportRefusal::TooManyParts, "TooManyParts"},
      {NvfImportRefusal::TooManyHardpoints, "TooManyHardpoints"},
      {NvfImportRefusal::UnknownChunks, "UnknownChunks"},
      {NvfImportRefusal::OrphanHardpoint, "OrphanHardpoint"},
      {NvfImportRefusal::NameClash, "NameClash"},
    }};
    for (const auto& [refusal, name] : names)
    {
      Assert::AreEqual(name, NeuronCore::NvfImportRefusalName(refusal));
    }
  }

  // Design/Archive/NeuronVoxelFormat.md §9: each of the assets, imported over its committed .nvf, gives that file byte for byte,
  // so an .nvf stale against its .vox fails here as well as in CI's --check.
  TEST_METHOD(ImportsTheAssetsAsCommitted)
  {
    for (const char* asset : {"MilitaryStation", "CapitalShip", "Frigate"})
    {
      const std::wstring what = Widen(asset);
      const auto vox = NeuronCore::LoadVoxModel(FindRepositoryFile(std::filesystem::path("GameData") / (std::string(asset) + ".vox")));
      Assert::IsTrue(vox.has_value(), (what + L".vox").c_str());
      const Bytes committed = ReadRepositoryFile(std::filesystem::path("GameData") / (std::string(asset) + ".nvf"));
      const auto previous = NeuronCore::ParseNvfModel(committed);
      Assert::IsTrue(previous.has_value(), (what + L".nvf is read").c_str());
      const auto imported = NeuronCore::ImportVoxModel(*vox, &*previous);
      Assert::IsTrue(imported.has_value(), (what + L" is imported").c_str());
      const auto bytes = NeuronCore::SerializeNvfModel(*imported);
      Assert::IsTrue(bytes.has_value() && *bytes == committed, (what + L".nvf is what importing its .vox gives").c_str());

      // One part, main, as the assets have no names (§4.5), with every voxel of the .vox.
      Assert::AreEqual(std::size_t{1}, imported->parts.size(), what.c_str());
      Assert::AreEqual(std::string("main"), imported->parts.front().path, what.c_str());
      Assert::AreEqual(vox->records.size(), imported->records.size(), what.c_str());
    }
  }

  // Design/ADR/ADR-028: the game draws and measures each asset from its .nvf, flattened, and gets the model the .vox
  // reader gives it, voxel for voxel. A tree of parts flattens with each part at the sum of its translations.
  TEST_METHOD(FlattensAsTheVoxReaderReads)
  {
    const auto sameCell = [](Int3 _a, Int3 _b) { return _a.x == _b.x && _a.y == _b.y && _a.z == _b.z; };
    for (const char* asset : {"MilitaryStation", "CapitalShip", "Frigate"})
    {
      const std::wstring what = Widen(asset);
      const auto vox = NeuronCore::LoadVoxModel(FindRepositoryFile(std::filesystem::path("GameData") / (std::string(asset) + ".vox")));
      const auto nvf = NeuronCore::LoadNvfModel(FindRepositoryFile(std::filesystem::path("GameData") / (std::string(asset) + ".nvf")));
      Assert::IsTrue(vox.has_value() && nvf.has_value(), what.c_str());
      const NeuronCore::VoxModel flat = NeuronCore::FlattenNvfModel(*nvf);
      Assert::IsTrue(flat.records == vox->records, (what + L"'s records, in order").c_str());
      Assert::AreEqual(vox->instances.size(), flat.instances.size(), what.c_str());
      for (std::size_t i = 0; i < flat.instances.size(); ++i)
      {
        const NeuronCore::ModelInstance& expected = vox->instances[i];
        const NeuronCore::ModelInstance& actual = flat.instances[i];
        Assert::IsTrue(sameCell(expected.origin, actual.origin) && sameCell(expected.size, actual.size), (what + L"'s placement").c_str());
        Assert::AreEqual(expected.firstRecord, actual.firstRecord, what.c_str());
        Assert::AreEqual(expected.recordCount, actual.recordCount, what.c_str());
        Assert::IsTrue(NeuronCore::IsIdentityRotation(actual.rotation), what.c_str());
      }
      for (std::size_t entry = 0; entry < flat.palette.size(); ++entry)
      {
        const NeuronCore::PaletteEntry& expected = vox->palette[entry];
        const NeuronCore::PaletteEntry& actual = flat.palette[entry];
        Assert::IsTrue(expected.red == actual.red && expected.green == actual.green && expected.blue == actual.blue &&
                         expected.alpha == actual.alpha && expected.emissive == actual.emissive && expected.emit == actual.emit &&
                         expected.flux == actual.flux,
                       std::format(L"{}'s palette entry {}", what, entry + 1).c_str());
      }
      const auto voxBounds = NeuronCore::OccupiedBounds(*vox);
      const auto flatBounds = NeuronCore::OccupiedBounds(flat);
      Assert::IsTrue(voxBounds.has_value() && flatBounds.has_value() && sameCell(voxBounds->lower, flatBounds->lower) &&
                       sameCell(voxBounds->upper, flatBounds->upper),
                     (what + L"'s bounds, which place its entities").c_str());
    }

    const NvfModel golden = GoldenNvfModel();
    const NeuronCore::VoxModel flat = NeuronCore::FlattenNvfModel(golden);
    constexpr std::array<Int3, 3> ORIGINS{{{-2, 0, -3}, {-1, 3, -1}, {0, 4, 2}}};
    Assert::AreEqual(ORIGINS.size(), flat.instances.size());
    for (std::size_t part = 0; part < ORIGINS.size(); ++part)
    {
      const NeuronCore::ModelInstance& instance = flat.instances[part];
      Assert::IsTrue(sameCell(ORIGINS[part], instance.origin), Widen(golden.parts[part].path).c_str());
      Assert::AreEqual(golden.parts[part].path, instance.name);
      Assert::AreEqual(golden.parts[part].firstVoxel, instance.firstRecord);
      Assert::AreEqual(golden.parts[part].voxelCount, instance.recordCount);
    }
    Assert::IsTrue(flat.records == golden.records);
  }
};

} // namespace NeuronCoreTests
