#include "pch.h"

#include "Catalogue.h"
#include "Design.h"
#include "DesignComposite.h"
#include "TestSupport.h"

#include "Composite.h"
#include "NvfModel.h"
#include "Quaternion.h"
#include "RigidTransform.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{
namespace
{

using NeuronCore::Int3;

// A design's hull and every module of its fit, flattened, by name: what the skirmish's welcome would name.
struct DesignModels
{
  std::vector<std::string> names;
  std::vector<NeuronCore::VoxModel> models;

  [[nodiscard]] GameCore::NamedModels Named() const noexcept
  {
    return {names, models};
  }
};

[[nodiscard]] DesignModels ModelsOf(const GameCore::Design& _design)
{
  DesignModels models;
  const auto add = [&models](std::string_view _name)
  {
    if (std::ranges::find(models.names, _name) == models.names.end())
    {
      models.names.emplace_back(_name);
      models.models.push_back(NeuronCore::FlattenNvfModel(LoadModel(_name)));
    }
  };
  add(_design.spec->hull);
  for (const GameCore::Mount& mount : _design.mounts)
  {
    add(mount.module->name);
  }
  return models;
}

using Cell = std::tuple<std::int32_t, std::int32_t, std::int32_t>;

// The cell a voxel of cell _cell in its model fills in the composite, under _component: its center's image, less half a
// voxel. Exact, since the component turns by one of the cube's rotations and moves by whole voxels.
[[nodiscard]] Int3 PlacedCell(const NeuronCore::CompositeComponent& _component, Int3 _cell)
{
  const NeuronCore::Float3 center = NeuronCore::TransformPoint(
    NeuronCore::ComponentTransform(_component),
    {static_cast<float>(_cell.x) + 0.5f, static_cast<float>(_cell.y) + 0.5f, static_cast<float>(_cell.z) + 0.5f});
  return {static_cast<std::int32_t>(std::floor(center.x)), static_cast<std::int32_t>(std::floor(center.y)),
          static_cast<std::int32_t>(std::floor(center.z))};
}

} // namespace

// Design/ADR/ADR-030: a design's composite, its hull and the module at each mount, as the client draws it.
TEST_CLASS(DesignCompositeTests)
{
public:
  // Design/MvpPlan.md §5, phase 2: every module of every MVP design fills its mount's box and nothing else, its center
  // cell on the mount's, turned the way the mount faces; and the hull stands where its model puts it.
  TEST_METHOD(FitsEachModuleAtItsMount)
  {
    for (const GameCore::DesignSpec& spec : GameCore::Designs())
    {
      const GameCore::Design design = LoadMvpDesign(spec.name);
      const DesignModels models = ModelsOf(design);
      const auto composite = GameCore::DesignComposite(design, models.Named());
      Assert::IsTrue(composite.has_value(), Widen(composite ? std::string() : composite.error()).c_str());
      const NeuronCore::CompositeModel fitted = composite.value_or(NeuronCore::CompositeModel{});
      Assert::AreEqual(design.mounts.size() + 1, fitted.components.size(),
                       Widen(std::format("{}: the hull and a module a mount", spec.name)).c_str());
      Assert::IsTrue(NeuronCore::IsIdentityComponent(fitted.components.front()), L"the hull where it is");
      Assert::AreEqual(std::uint16_t{0}, fitted.components.front().model, L"the hull first");
      for (std::size_t index = 0; index < design.mounts.size(); ++index)
      {
        const GameCore::Mount& mount = design.mounts[index];
        const NeuronCore::CompositeComponent& component = fitted.components[index + 1];
        const std::wstring what = Widen(std::format("{}, {}", spec.name, mount.name));
        Assert::AreEqual(std::string(mount.module->name), models.names[component.model], (what + L": its module").c_str());
        Assert::IsTrue(NeuronCore::IsCubeSymmetry(NeuronCore::RotationOf(component.rotation)),
                       (what + L": turned by quarter turns").c_str());
        std::set<Cell> box;
        for (const Int3 cell : GameCore::MountCells(mount))
        {
          box.emplace(cell.x, cell.y, cell.z);
        }
        const NeuronCore::VoxModel& module = models.models[component.model];
        const NeuronCore::ModelInstance& part = module.instances.front();
        for (std::uint32_t voxel = 0; voxel < part.recordCount; ++voxel)
        {
          const NeuronCore::VoxelRecord record = NeuronCore::UnpackVoxelRecord(module.records[part.firstRecord + voxel]);
          const Int3 placed = PlacedCell(component, part.origin + Int3{record.x, record.y, record.z});
          Assert::IsTrue(box.contains({placed.x, placed.y, placed.z}),
                         (what + std::format(L": voxel {} in the mount's box", voxel)).c_str());
        }
        const Int3 center = PlacedCell(component, part.origin + Int3{part.size.x / 2, part.size.y / 2, part.size.z / 2});
        Assert::IsTrue(center.x == mount.centerCell.x && center.y == mount.centerCell.y && center.z == mount.centerCell.z,
                       (what + L": its center cell on the mount's").c_str());
        const NeuronCore::Float3 front = NeuronCore::RotateVector(NeuronCore::RotationOf(component.rotation), {0.0f, 0.0f, 1.0f});
        const Int3 facing = GameCore::Facing(mount);
        Assert::IsTrue(front.x == static_cast<float>(facing.x) && front.y == static_cast<float>(facing.y) &&
                         front.z == static_cast<float>(facing.z),
                       (what + L": facing the way the mount faces").c_str());
      }
    }
  }

  // A composite whose models the welcome lacks is refused by the missing model's name.
  TEST_METHOD(NamesAMissingModel)
  {
    const GameCore::Design design = LoadMvpDesign("Gunship");
    DesignModels models = ModelsOf(design);
    const auto thruster = std::ranges::find(models.names, "Thruster");
    Assert::IsTrue(thruster != models.names.end(), L"the gunship has thrusters");
    *thruster = "Nothing";
    const auto composite = GameCore::DesignComposite(design, models.Named());
    Assert::IsFalse(composite.has_value(), L"refused");
    Assert::AreEqual(std::string("Gunship: no model is named Thruster"), composite ? std::string() : composite.error());
  }
};

} // namespace GameCoreTests
