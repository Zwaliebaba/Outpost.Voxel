#include "pch.h"

#include "GraphicsDevice.h"
#include "TestSupport.h"
#include "VoxelScene.h"

#include "ColorSpace.h"
#include "Fragmentation.h"
#include "Lighting.h"
#include "Message.h"
#include "RigidTransform.h"
#include "SidePalette.h"
#include "VoxModel.h"

#include <array>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

TEST_CLASS(VoxelSceneTests)
{
public:
  // Design/Archive/NeuronVoxelFormat.md §6.1: the .vox reader accepts a turned model only for a marker, and the renderer, which
  // never draws one, refuses a scene that holds one, by the model's name, rather than drawing it unturned.
  TEST_METHOD(RefusesTurnedModels)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        NeuronCore::VoxModel model = RandomBlock();
        const NeuronClient::VoxelScene whole(_device, {&model, 1});
        Assert::AreEqual(static_cast<std::uint32_t>(model.records.size()), whole.RecordCount(), L"an unturned model is drawn");

        model.instances.front().rotation = {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}};
        model.instances.front().name = "main@weapon.front";
        std::string refusal;
        try
        {
          const NeuronClient::VoxelScene turned(_device, {&model, 1});
          refusal = "drawn, with " + std::to_string(turned.RecordCount()) + " records";
        }
        catch (const std::invalid_argument& error)
        {
          refusal = error.what();
        }
        Assert::IsTrue(refusal.find("main@weapon.front") != std::string::npos, L"a turned model is refused by its name");
      });
  }

  // Design/ADR/ADR-024: the scene takes the fragments the client poses with, and refuses fragments of other models, whose
  // records the shaders would index past.
  TEST_METHOD(RefusesAnotherScenesFragments)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel block = RandomBlock();
        const NeuronCore::VoxModel station = LoadMilitaryStation();
        const std::vector<NeuronCore::VoxModel> both{block, station};
        const NeuronCore::SceneFragments fragments(both);
        const NeuronClient::VoxelScene shared(_device, both, fragments);
        Assert::AreEqual(static_cast<std::uint32_t>(fragments.Fragments().size()), shared.FragmentCount(), L"the fragments it was given");

        bool refused = false;
        try
        {
          const NeuronClient::VoxelScene mismatched(_device, {&block, 1}, fragments);
          Assert::Fail(std::format(L"drawn, with {} records", mismatched.RecordCount()).c_str());
        }
        catch (const std::invalid_argument&)
        {
          refused = true;
        }
        Assert::IsTrue(refused, L"fragments of more records than the scene's are refused");
      });
  }

  // Design/ADR/ADR-029: each model's own palette, then its variant for each side, model after model, where
  // NeuronCore::SidePaletteIndex finds them. A side's variant differs from the model's own in the side's entry alone.
  TEST_METHOD(HoldsEachSidesPalettes)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        std::vector<NeuronCore::VoxModel> models{RandomBlock(), RandomBlock()};
        models[1].palette[3].red = 7;
        const std::array<NeuronCore::SideColor, 2> sides{{{40, 120, 220}, {220, 80, 60}}};
        const NeuronClient::VoxelScene scene(_device, models, NeuronCore::SceneFragments(models), sides);
        Assert::AreEqual(6u, scene.PaletteCount(), L"three for each model");
        for (std::uint32_t model = 0; model < models.size(); ++model)
        {
          for (std::uint32_t side = 0; side <= sides.size(); ++side)
          {
            const NeuronClient::PaletteConstants& palette = scene.PaletteValues(NeuronCore::SidePaletteIndex(model, side, sides.size()));
            for (std::uint32_t entry = 0; entry < NeuronCore::PALETTE_ENTRY_COUNT; ++entry)
            {
              NeuronCore::PaletteEntry expected = models[model].palette[entry];
              if (side > 0 && entry + 1 == NeuronCore::SIDE_PALETTE_ENTRY)
              {
                expected.red = sides[side - 1].red;
                expected.green = sides[side - 1].green;
                expected.blue = sides[side - 1].blue;
              }
              const NeuronCore::Float3 albedo = palette.materials[entry].albedo;
              const std::wstring what = std::format(L"model {}, side {}, entry {}", model, side, entry + 1);
              Assert::AreEqual(NeuronCore::SrgbToLinear(expected.red), albedo.x, (what + L", red").c_str());
              Assert::AreEqual(NeuronCore::SrgbToLinear(expected.green), albedo.y, (what + L", green").c_str());
              Assert::AreEqual(NeuronCore::SrgbToLinear(expected.blue), albedo.z, (what + L", blue").c_str());
              Assert::AreEqual(NeuronCore::EmissiveScale(expected), palette.materials[entry].emissiveScale, (what + L", glow").c_str());
            }
          }
        }
      });
  }
};

} // namespace NeuronClientTests
