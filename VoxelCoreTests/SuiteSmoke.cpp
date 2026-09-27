#include "pch.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VoxelCoreTests
{

// vstest reports a suite without tests as a pass, so every suite ships this placeholder until its
// first real test lands (AGENTS.md §3). Delete it then, never before.
TEST_CLASS(SuiteSmoke)
{
public:
  TEST_METHOD(Runs)
  {
    Assert::IsTrue(true);
  }
};

} // namespace VoxelCoreTests
