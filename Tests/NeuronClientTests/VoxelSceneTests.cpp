#include "pch.h"

#include "GraphicsDevice.h"
#include "TestSupport.h"
#include "VoxelScene.h"

#include "Fragmentation.h"
#include "RigidTransform.h"
#include "VoxModel.h"

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
};

} // namespace NeuronClientTests
