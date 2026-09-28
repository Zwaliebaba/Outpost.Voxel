#include "pch.h"

#include "Float3.h"
#include "OctahedralNormal.h"
#include "SeededRandom.h"
#include "VoxelRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;

[[nodiscard]] double AngleRadians(Float3 _a, Float3 _b) noexcept
{
  const double dot = static_cast<double>(_a.x) * _b.x + static_cast<double>(_a.y) * _b.y + static_cast<double>(_a.z) * _b.z;
  const double cross = std::hypot(static_cast<double>(_a.y) * _b.z - static_cast<double>(_a.z) * _b.y,
                                  static_cast<double>(_a.z) * _b.x - static_cast<double>(_a.x) * _b.z,
                                  static_cast<double>(_a.x) * _b.y - static_cast<double>(_a.y) * _b.x);
  return std::atan2(cross, dot);
}

} // namespace

// The packing twins of Design/Archive/SampleRenderer.md §7.1 and §7.3 (R14, R15).
TEST_CLASS(PackingTests)
{
public:
  TEST_METHOD(VoxelRecordLayout)
  {
    Assert::AreEqual(0x04030201u, NeuronCore::PackVoxelRecord({1, 2, 3, 4}), L"x, y, z and color, lowest byte first");
    Assert::AreEqual(0x0FFFFFFFu, NeuronCore::PackVoxelRecord({255, 255, 255, 15}), L"bits 28-31 stay zero");
    Assert::AreEqual(0x0F000000u, NeuronCore::PackVoxelRecord({0, 0, 0, 0xFF}), L"a color beyond 15 is masked to its four bits");
  }

  TEST_METHOD(VoxelRecordRoundTrips)
  {
    const std::array<std::uint8_t, 7> coordinates{0, 1, 2, 127, 128, 254, 255};
    for (const std::uint8_t x : coordinates)
    {
      for (const std::uint8_t y : coordinates)
      {
        for (const std::uint8_t z : coordinates)
        {
          for (std::uint32_t entry = 0; entry < NeuronCore::PALETTE_ENTRY_COUNT; ++entry)
          {
            const auto color = static_cast<std::uint8_t>(entry);
            const std::uint32_t packed = NeuronCore::PackVoxelRecord({x, y, z, color});
            const NeuronCore::VoxelRecord record = NeuronCore::UnpackVoxelRecord(packed);
            const std::wstring what = std::format(L"{} {} {} {}", x, y, z, color);
            Assert::AreEqual(x, record.x, what.c_str());
            Assert::AreEqual(y, record.y, what.c_str());
            Assert::AreEqual(z, record.z, what.c_str());
            Assert::AreEqual(color, record.color, what.c_str());
            Assert::AreEqual(0u, packed >> 28u, what.c_str());
          }
        }
      }
    }
  }

  TEST_METHOD(AxisNormalsSurviveExactly)
  {
    // Every normal of the intact station is one of these six, so the visibility buffer holds them without error.
    const std::array<std::pair<Float3, std::uint32_t>, 6> axes{{
      {{1.0f, 0.0f, 0.0f}, 0x00007FFFu},
      {{-1.0f, 0.0f, 0.0f}, 0x00008001u},
      {{0.0f, 1.0f, 0.0f}, 0x7FFF0000u},
      {{0.0f, -1.0f, 0.0f}, 0x80010000u},
      {{0.0f, 0.0f, 1.0f}, 0x00000000u},
      {{0.0f, 0.0f, -1.0f}, 0x7FFF7FFFu},
    }};
    for (const auto& [normal, bits] : axes)
    {
      const std::wstring what = std::format(L"{} {} {}", normal.x, normal.y, normal.z);
      Assert::AreEqual(bits, NeuronCore::PackOctahedralNormal(normal), what.c_str());
      const Float3 unpacked = NeuronCore::UnpackOctahedralNormal(bits);
      Assert::AreEqual(normal.x, unpacked.x, what.c_str());
      Assert::AreEqual(normal.y, unpacked.y, what.c_str());
      Assert::AreEqual(normal.z, unpacked.z, what.c_str());
    }
  }

  TEST_METHOD(OctahedralNormalsRoundTrip)
  {
    // Sixteen bits per coordinate hold a unit vector to within about 1e-4 radians everywhere on the sphere, including
    // the folded lower hemisphere and the equator.
    SeededRandom random(21u);
    double worst = 0.0;
    for (std::uint32_t i = 0; i < 20000; ++i)
    {
      Float3 normal = random.Direction();
      if (i % 10 == 0)
      {
        normal.z = 0.0f; // the equator, where the fold begins
        normal = NeuronCore::Normalize(normal);
      }
      const Float3 unpacked = NeuronCore::UnpackOctahedralNormal(NeuronCore::PackOctahedralNormal(normal));
      const std::wstring what = std::format(L"{} {} {}", normal.x, normal.y, normal.z);
      Assert::AreEqual(1.0, static_cast<double>(NeuronCore::Length(unpacked)), 1.0e-6, what.c_str());
      const double angle = AngleRadians(normal, unpacked);
      Assert::IsTrue(angle < 1.0e-4, what.c_str());
      worst = std::max(worst, angle);
    }
    Logger::WriteMessage(std::format(L"worst round-trip error {} radians", worst).c_str());
  }
};

} // namespace NeuronCoreTests
