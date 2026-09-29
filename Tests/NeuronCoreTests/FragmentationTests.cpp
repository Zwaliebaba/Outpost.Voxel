#include "pch.h"

#include "SeededRandom.h"
#include "RepositoryFile.h"

#include "Explosion.h"
#include "Float3.h"
#include "Fragmentation.h"
#include "VoxModel.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <map>
#include <set>
#include <tuple>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;

// A model of _parts parts, each of up to _draws distinct random voxels in a _cells-wide cube at a random origin.
[[nodiscard]] NeuronCore::VoxModel RandomModel(SeededRandom& _random, std::uint32_t _parts, std::uint32_t _draws, std::uint32_t _cells)
{
  NeuronCore::VoxModel model{};
  model.version = 150;
  for (std::uint32_t part = 0; part < _parts; ++part)
  {
    const auto firstRecord = static_cast<std::uint32_t>(model.records.size());
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> cells;
    for (std::uint32_t draw = 0; draw < _draws; ++draw)
    {
      const std::uint32_t x = _random.Below(_cells);
      const std::uint32_t y = _random.Below(_cells);
      const std::uint32_t z = _random.Below(_cells);
      if (cells.emplace(x, y, z).second)
      {
        model.records.push_back(NeuronCore::PackVoxelRecord(
          {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(z), static_cast<std::uint8_t>(0u)}));
      }
    }
    const auto size = static_cast<std::int32_t>(_cells);
    const NeuronCore::Int3 origin{static_cast<std::int32_t>(_random.Below(64u)) - 32, static_cast<std::int32_t>(_random.Below(64u)) - 32,
                                  static_cast<std::int32_t>(_random.Below(64u)) - 32};
    model.instances.push_back({origin, {size, size, size}, firstRecord, static_cast<std::uint32_t>(model.records.size()) - firstRecord});
  }
  return model;
}

[[nodiscard]] Float3 CenterOf(std::uint32_t _record) noexcept
{
  const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(_record);
  return {static_cast<float>(voxel.x) + 0.5f, static_cast<float>(voxel.y) + 0.5f, static_cast<float>(voxel.z) + 0.5f};
}

// Whether the records _members of _records are face-connected among themselves.
[[nodiscard]] bool FaceConnected(const std::vector<std::uint32_t>& _records, const std::vector<std::uint32_t>& _members)
{
  std::map<std::tuple<std::int32_t, std::int32_t, std::int32_t>, std::uint32_t> cells;
  for (const std::uint32_t member : _members)
  {
    const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(_records[member]);
    cells.emplace(std::tuple<std::int32_t, std::int32_t, std::int32_t>{voxel.x, voxel.y, voxel.z}, member);
  }
  std::set<std::uint32_t> reached{_members.front()};
  std::vector<std::uint32_t> queue{_members.front()};
  constexpr std::array<std::array<std::int32_t, 3>, 6> STEPS{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
  for (std::size_t head = 0; head < queue.size(); ++head)
  {
    const NeuronCore::VoxelRecord voxel = NeuronCore::UnpackVoxelRecord(_records[queue[head]]);
    for (const std::array<std::int32_t, 3>& step : STEPS)
    {
      const auto found = cells.find({voxel.x + step[0], voxel.y + step[1], voxel.z + step[2]});
      if (found != cells.end() && reached.insert(found->second).second)
      {
        queue.push_back(found->second);
      }
    }
  }
  return reached.size() == _members.size();
}

} // namespace

// How models break when they detonate (Design/ADR/ADR-024): the fragments the oriented splat and its twin pose.
TEST_CLASS(FragmentationTests)
{
public:
  // Every voxel is in one fragment of its own part; a fragment is face-connected, its pivot is the mean of its centers,
  // its size scale its voxel count to the power -1/3, and no center lies farther from its pivot than the part's radius,
  // which is at most twice the growth steps.
  TEST_METHOD(FragmentsAreConnectedPiecesOfOnePart)
  {
    SeededRandom random(20261105u);
    std::uint32_t fragmentCount = 0;
    std::uint32_t largest = 0;
    for (std::uint32_t sample = 0; sample < 16u; ++sample)
    {
      const NeuronCore::VoxModel model = RandomModel(random, 1u + random.Below(3u), 800u, 6u + random.Below(14u));
      const NeuronCore::FragmentationParameters parameters{.blastOrigin = random.InBox({-32.0f, -32.0f, -32.0f}, {40.0f, 40.0f, 40.0f}),
                                                           .shatterDistance = random.Uniform(0.5f, 30.0f),
                                                           .growthSteps = random.Below(12u)};
      const NeuronCore::ModelFragments broken = NeuronCore::FragmentModel(model, parameters);
      Assert::AreEqual(model.records.size(), broken.fragmentOf.size());
      Assert::AreEqual(model.instances.size(), broken.partRadius.size());

      std::uint32_t nextFragment = 0;
      for (std::uint32_t part = 0; part < model.instances.size(); ++part)
      {
        const NeuronCore::ModelInstance& instance = model.instances[part];
        std::map<std::uint32_t, std::vector<std::uint32_t>> members;
        for (std::uint32_t i = 0; i < instance.recordCount; ++i)
        {
          members[broken.fragmentOf[instance.firstRecord + i]].push_back(instance.firstRecord + i);
        }
        // The part's fragments are numbered on from the part before's, with none skipped.
        Assert::AreEqual(nextFragment, members.begin()->first, std::format(L"sample {}, part {}", sample, part).c_str());
        Assert::AreEqual(nextFragment + static_cast<std::uint32_t>(members.size()) - 1u, members.rbegin()->first);
        nextFragment += static_cast<std::uint32_t>(members.size());
        for (const auto& [fragment, records] : members)
        {
          const std::wstring what = std::format(L"sample {}, part {}, fragment {}", sample, part, fragment);
          Assert::IsTrue(FaceConnected(model.records, records), what.c_str());
          double sumX = 0.0;
          double sumY = 0.0;
          double sumZ = 0.0;
          for (const std::uint32_t record : records)
          {
            const Float3 center = CenterOf(model.records[record]);
            sumX += center.x;
            sumY += center.y;
            sumZ += center.z;
          }
          const auto count = static_cast<double>(records.size());
          const NeuronCore::Fragment& shape = broken.fragments[fragment];
          Assert::AreEqual(static_cast<float>(sumX / count), shape.pivot.x, what.c_str());
          Assert::AreEqual(static_cast<float>(sumY / count), shape.pivot.y, what.c_str());
          Assert::AreEqual(static_cast<float>(sumZ / count), shape.pivot.z, what.c_str());
          Assert::AreEqual(static_cast<float>(1.0 / std::cbrt(count)), shape.sizeScale, what.c_str());
          for (const std::uint32_t record : records)
          {
            Assert::IsTrue(NeuronCore::Length(CenterOf(model.records[record]) - shape.pivot) <= broken.partRadius[part], what.c_str());
          }
          largest = std::max(largest, static_cast<std::uint32_t>(records.size()));
        }
        Assert::IsTrue(broken.partRadius[part] <= 2.0f * static_cast<float>(parameters.growthSteps) + 1.0e-4f,
                       std::format(L"sample {}, part {}: radius {}", sample, part, broken.partRadius[part]).c_str());
      }
      Assert::AreEqual(static_cast<std::size_t>(nextFragment), broken.fragments.size());
      fragmentCount += nextFragment;
    }
    Logger::WriteMessage(std::format(L"{} fragments checked, the largest of {} voxels\n", fragmentCount, largest).c_str());
  }

  // The same model and parameters break the same way, on every client (D3).
  TEST_METHOD(BreaksTheSameWayEveryTime)
  {
    SeededRandom random(20261106u);
    const NeuronCore::VoxModel model = RandomModel(random, 2u, 1500u, 14u);
    const NeuronCore::FragmentationParameters parameters = NeuronCore::DefaultFragmentationParameters(model);
    const NeuronCore::ModelFragments first = NeuronCore::FragmentModel(model, parameters);
    const NeuronCore::ModelFragments second = NeuronCore::FragmentModel(model, parameters);
    Assert::IsTrue(first.fragmentOf == second.fragmentOf);
    Assert::IsTrue(first.partRadius == second.partRadius);
    Assert::AreEqual(first.fragments.size(), second.fragments.size());
    for (std::size_t i = 0; i < first.fragments.size(); ++i)
    {
      Assert::IsTrue(
        first.fragments[i].pivot.x == second.fragments[i].pivot.x && first.fragments[i].pivot.y == second.fragments[i].pivot.y &&
        first.fragments[i].pivot.z == second.fragments[i].pivot.z && first.fragments[i].sizeScale == second.fragments[i].sizeScale);
    }
  }

  // The scene's fragments lie model after model as its records do, and a part reads its own records' fragments and its
  // model's fragments.
  TEST_METHOD(SceneFragmentsLieModelAfterModel)
  {
    SeededRandom random(20261107u);
    const std::vector<NeuronCore::VoxModel> models{RandomModel(random, 2u, 300u, 9u), RandomModel(random, 3u, 200u, 12u)};
    const NeuronCore::SceneFragments scene(models);
    std::uint32_t firstRecord = 0;
    std::uint32_t firstFragment = 0;
    for (std::uint32_t model = 0; model < models.size(); ++model)
    {
      const NeuronCore::ModelFragments broken =
        NeuronCore::FragmentModel(models[model], NeuronCore::DefaultFragmentationParameters(models[model]));
      for (std::uint32_t part = 0; part < models[model].instances.size(); ++part)
      {
        const NeuronCore::ModelInstance& instance = models[model].instances[part];
        const NeuronCore::PartFragments fragments = scene.Part(model, part);
        const std::wstring what = std::format(L"model {}, part {}", model, part);
        Assert::AreEqual(firstFragment, fragments.firstFragment, what.c_str());
        Assert::AreEqual(broken.fragments.size(), fragments.fragments.size(), what.c_str());
        Assert::AreEqual(static_cast<std::size_t>(instance.recordCount), fragments.fragmentOf.size(), what.c_str());
        Assert::AreEqual(broken.partRadius[part], fragments.radius, what.c_str());
        for (std::uint32_t i = 0; i < instance.recordCount; ++i)
        {
          Assert::AreEqual(broken.fragmentOf[instance.firstRecord + i], fragments.fragmentOf[i], what.c_str());
          Assert::AreEqual(broken.fragmentOf[instance.firstRecord + i], scene.FragmentOf()[firstRecord + instance.firstRecord + i],
                           what.c_str());
        }
        Assert::IsTrue(fragments.fragments.data() == scene.Fragments().data() + firstFragment, what.c_str());
      }
      firstRecord += static_cast<std::uint32_t>(models[model].records.size());
      firstFragment += static_cast<std::uint32_t>(broken.fragments.size());
    }
    Assert::AreEqual(static_cast<std::size_t>(firstRecord), scene.FragmentOf().size());
    Assert::AreEqual(static_cast<std::size_t>(firstFragment), scene.Fragments().size());
  }

  // ADR-024: each of the game's models shatters finest about its blast origin, and breaks into ever larger chunks
  // farther out.
  TEST_METHOD(ModelsShatterFinestNearTheBlast)
  {
    for (const char* const name : {"MilitaryStation.vox", "CapitalShip.vox", "Frigate.vox"})
    {
      ExpectFinestNearTheBlast(name);
    }
  }

private:
  static void ExpectFinestNearTheBlast(const char* _name)
  {
    const std::vector<std::uint8_t> bytes = ReadRepositoryFile(std::filesystem::path("GameData") / _name);
    const auto parsed = NeuronCore::ParseVoxModel(bytes);
    Assert::IsTrue(parsed.has_value(), L"the model reads");
    const NeuronCore::VoxModel& model = *parsed;
    const NeuronCore::FragmentationParameters parameters = NeuronCore::DefaultFragmentationParameters(model);
    const Float3 centroid = parameters.blastOrigin;
    const auto start = std::chrono::steady_clock::now();
    const NeuronCore::ModelFragments broken = NeuronCore::FragmentModel(model, parameters);
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    std::vector<std::uint32_t> sizes(broken.fragments.size(), 0u);
    for (const std::uint32_t fragment : broken.fragmentOf)
    {
      ++sizes[fragment];
    }
    // The mean size of the fragment a voxel is in, by its distance from the blast origin, in eight bands out to the
    // farthest, which the shatter distance is a fraction of.
    constexpr std::uint32_t BANDS = 8;
    float extent = 0.0f;
    for (const NeuronCore::ModelInstance& instance : model.instances)
    {
      for (std::uint32_t i = instance.firstRecord; i < instance.firstRecord + instance.recordCount; ++i)
      {
        extent = std::max(extent, NeuronCore::Length(NeuronCore::VoxelBox(instance, model.records[i]).center - centroid));
      }
    }
    std::array<double, BANDS> sizeSums{};
    std::array<std::uint32_t, BANDS> voxelCounts{};
    std::uint32_t lone = 0;
    float radius = 0.0f;
    for (std::uint32_t part = 0; part < model.instances.size(); ++part)
    {
      const NeuronCore::ModelInstance& instance = model.instances[part];
      radius = std::max(radius, broken.partRadius[part]);
      for (std::uint32_t i = instance.firstRecord; i < instance.firstRecord + instance.recordCount; ++i)
      {
        const Float3 center = NeuronCore::VoxelBox(instance, model.records[i]).center;
        const auto band =
          std::min(static_cast<std::uint32_t>(NeuronCore::Length(center - centroid) / extent * static_cast<float>(BANDS)), BANDS - 1u);
        const std::uint32_t size = sizes[broken.fragmentOf[i]];
        sizeSums[band] += size;
        ++voxelCounts[band];
        lone += size == 1u ? 1u : 0u;
      }
    }
    std::wstring bands;
    for (std::uint32_t band = 0; band < BANDS; ++band)
    {
      bands += std::format(L" {:.1f}", voxelCounts[band] > 0u ? sizeSums[band] / voxelCounts[band] : 0.0);
    }
    const std::wstring name(_name, _name + std::char_traits<char>::length(_name));
    Logger::WriteMessage(std::format(L"{}: {} voxels into {} fragments in {:.1f} ms; {} voxels fly alone; the largest fragment holds {}; "
                                     L"radius {}; shatter distance {} of {}; mean fragment size by eighth of that from the blast:{}\n",
                                     name, model.records.size(), broken.fragments.size(), milliseconds, lone,
                                     *std::max_element(sizes.begin(), sizes.end()), radius, parameters.shatterDistance, extent, bands)
                           .c_str());
    Assert::IsTrue(voxelCounts[0] > 0u && voxelCounts[BANDS / 2u] > 0u, name.c_str());
    Assert::IsTrue(sizeSums[0] / voxelCounts[0] < sizeSums[BANDS / 2u] / voxelCounts[BANDS / 2u],
                   (name + L": finer near the blast than halfway out").c_str());
  }
};

} // namespace NeuronCoreTests
