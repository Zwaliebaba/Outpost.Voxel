#include "pch.h"

#include "GraphicsDevice.h"
#include "TestSupport.h"
#include "VoxelScene.h"

#include "RigidTransform.h"
#include "VoxModel.h"

#include <cstdint>
#include <stdexcept>
#include <string>

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
};

} // namespace NeuronClientTests
