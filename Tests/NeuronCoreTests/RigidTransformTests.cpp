#include "pch.h"

#include "CubeSymmetry.h"
#include "SeededRandom.h"

#include "Float3.h"
#include "Quaternion.h"
#include "RigidTransform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::Float3;
using NeuronCore::Quaternion;
using NeuronCore::Rotation;

[[nodiscard]] bool SameRotation(const Rotation& _a, const Rotation& _b) noexcept
{
  const auto same = [](Float3 _u, Float3 _v) noexcept { return _u.x == _v.x && _u.y == _v.y && _u.z == _v.z; };
  return same(_a.axisX, _b.axisX) && same(_a.axisY, _b.axisY) && same(_a.axisZ, _b.axisZ);
}

// Entry _entry of _rotation, column by column: 0 to 2 are axisX, 3 to 5 axisY, 6 to 8 axisZ.
[[nodiscard]] float& Entry(Rotation& _rotation, std::uint32_t _entry) noexcept
{
  Float3& axis = _entry < 3u ? _rotation.axisX : (_entry < 6u ? _rotation.axisY : _rotation.axisZ);
  const std::uint32_t row = _entry % 3u;
  return row == 0u ? axis.x : (row == 1u ? axis.y : axis.z);
}

struct DoubleQuaternion
{
  double x;
  double y;
  double z;
  double w;
};

// A quaternion of _rotation in double precision, by Shepperd's method: from whichever of w, x, y and z is largest, so
// that nothing is divided by a small number.
[[nodiscard]] DoubleQuaternion QuaternionOfInDouble(const Rotation& _rotation) noexcept
{
  const double m00 = _rotation.axisX.x;
  const double m10 = _rotation.axisX.y;
  const double m20 = _rotation.axisX.z;
  const double m01 = _rotation.axisY.x;
  const double m11 = _rotation.axisY.y;
  const double m21 = _rotation.axisY.z;
  const double m02 = _rotation.axisZ.x;
  const double m12 = _rotation.axisZ.y;
  const double m22 = _rotation.axisZ.z;
  const double trace = m00 + m11 + m22;
  if (trace > 0.0)
  {
    const double s = 2.0 * std::sqrt(1.0 + trace);
    return {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s};
  }
  if (m00 > m11 && m00 > m22)
  {
    const double s = 2.0 * std::sqrt(1.0 + m00 - m11 - m22);
    return {0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
  }
  if (m11 > m22)
  {
    const double s = 2.0 * std::sqrt(1.0 + m11 - m00 - m22);
    return {(m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s};
  }
  const double s = 2.0 * std::sqrt(1.0 + m22 - m00 - m11);
  return {(m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s};
}

// The rotation of a unit quaternion, in double precision.
void RotationOfInDouble(const DoubleQuaternion& _q, std::array<double, 9>& _entries) noexcept
{
  const double x = _q.x;
  const double y = _q.y;
  const double z = _q.z;
  const double w = _q.w;
  _entries = {1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z),       2.0 * (x * z - w * y),
              2.0 * (x * y - w * z),       1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + w * x),
              2.0 * (x * z + w * y),       2.0 * (y * z - w * x),       1.0 - 2.0 * (x * x + y * y)};
}

} // namespace

// Design/SpaceScene.md §7.2 and §15: rotations, quaternions and rigid transforms, against the cube's symmetries and
// double-precision references.
TEST_CLASS(RigidTransformTests)
{
public:
  TEST_METHOD(AcceptsTheCubeRotationsAndRefusesItsReflections)
  {
    std::uint32_t proper = 0;
    for (const CubeSymmetry& symmetry : CubeSymmetries())
    {
      Assert::AreEqual(symmetry.proper, NeuronCore::IsCubeSymmetry(symmetry.rotation));
      proper += symmetry.proper ? 1u : 0u;
    }
    Assert::AreEqual(24u, proper, L"the cube has 24 rotations");
    Assert::IsTrue(NeuronCore::IsCubeSymmetry(NeuronCore::IDENTITY_ROTATION));
  }

  // One entry a float's last bit away from 0 or ±1, either way, is not a symmetry.
  TEST_METHOD(RefusesEveryNearMiss)
  {
    for (const CubeSymmetry& symmetry : CubeSymmetries())
    {
      if (!symmetry.proper)
      {
        continue;
      }
      for (std::uint32_t entry = 0; entry < 9u; ++entry)
      {
        for (const float toward : {-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()})
        {
          Rotation missed = symmetry.rotation;
          Entry(missed, entry) = std::nextafter(Entry(missed, entry), toward);
          Assert::IsFalse(NeuronCore::IsCubeSymmetry(missed), std::format(L"entry {} moved towards {}", entry, toward).c_str());
        }
      }
    }
  }

  // Each of the 24 rotations comes back exactly from its quaternion rounded to float: the 12 whose quaternion holds only
  // 0, ±½ and ±1 because every product is exact, and the 12 that need √½ because the scale of 2 / |q|² takes up the
  // rounding of √½ squared. With either quaternion of the pair, q or -q. So a whole station stands aligned (§5.1).
  TEST_METHOD(QuaternionsOfTheRotationsGiveThemExactly)
  {
    std::uint32_t plain = 0;
    for (const CubeSymmetry& symmetry : CubeSymmetries())
    {
      if (!symmetry.proper)
      {
        continue;
      }
      const DoubleQuaternion q = QuaternionOfInDouble(symmetry.rotation);
      const Quaternion rounded{static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)};
      const Quaternion negated{-rounded.x, -rounded.y, -rounded.z, -rounded.w};
      Assert::IsTrue(SameRotation(symmetry.rotation, NeuronCore::RotationOf(rounded)), L"its quaternion gives it exactly");
      Assert::IsTrue(SameRotation(symmetry.rotation, NeuronCore::RotationOf(negated)), L"and so does the negated one");
      bool exactEntries = true;
      for (const float entry : {rounded.x, rounded.y, rounded.z, rounded.w})
      {
        const float magnitude = std::abs(entry);
        exactEntries = exactEntries && (magnitude == 0.0f || magnitude == 0.5f || magnitude == 1.0f);
      }
      plain += exactEntries ? 1u : 0u;
    }
    Assert::AreEqual(12u, plain, L"the identity, three half turns about the axes and eight thirds of a turn about the diagonals");
  }

  // QuaternionOf inverts RotationOf, and stores the rotation as NVF and the protocol do: of unit length, w >= 0 (§6.2).
  TEST_METHOD(QuaternionOfInvertsRotationOf)
  {
    SeededRandom random(20261017u);
    float worst = 0.0f;
    for (std::uint32_t sample = 0; sample < 4096u; ++sample)
    {
      Quaternion q{};
      float length = 0.0f;
      do
      {
        q = {random.Uniform(-1.0f, 1.0f), random.Uniform(-1.0f, 1.0f), random.Uniform(-1.0f, 1.0f), random.Uniform(-1.0f, 1.0f)};
        length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
      } while (length < 0.1f || length > 1.0f);
      const float sign = q.w < 0.0f ? -1.0f : 1.0f;
      q = {sign * q.x / length, sign * q.y / length, sign * q.z / length, sign * q.w / length};
      const Quaternion back = NeuronCore::QuaternionOf(NeuronCore::RotationOf(q));
      Assert::IsTrue(NeuronCore::IsUnitRotation(back), std::format(L"sample {} is stored as a unit rotation", sample).c_str());
      worst = std::max({worst, std::abs(back.x - q.x), std::abs(back.y - q.y), std::abs(back.z - q.z), std::abs(back.w - q.w)});
    }
    Logger::WriteMessage(std::format(L"QuaternionOf's worst component is {} from the quaternion it came from\n", worst).c_str());
    Assert::IsTrue(worst < 2.0e-6f, L"every component within 2 x 10^-6");
  }

  // A station's quarter turn survives the wire: each of the cube's rotations comes back exactly from QuaternionOf, and
  // the quaternion's entries are those of the float rounding of its exact value (§5.1, §7.2).
  TEST_METHOD(CubeRotationsSurviveTheirQuaternions)
  {
    for (const CubeSymmetry& symmetry : CubeSymmetries())
    {
      if (!symmetry.proper)
      {
        continue;
      }
      const Quaternion q = NeuronCore::QuaternionOf(symmetry.rotation);
      Assert::IsTrue(NeuronCore::IsUnitRotation(q), L"stored as a unit rotation");
      Assert::IsTrue(SameRotation(symmetry.rotation, NeuronCore::RotationOf(q)), L"and turned back exactly");
    }
  }

  TEST_METHOD(StoresRotationsAsNvfDoes)
  {
    Assert::IsTrue(NeuronCore::IsUnitRotation({0.0f, 0.0f, 0.0f, 1.0f}), L"the identity");
    Assert::IsTrue(NeuronCore::IsUnitRotation({1.0f, 0.0f, 0.0f, 0.0f}), L"a half turn, w = 0");
    Assert::IsTrue(NeuronCore::IsUnitRotation({0.0f, 0.0f, 0.0f, 1.00009f}), L"within 10^-4 of unit length");
    Assert::IsFalse(NeuronCore::IsUnitRotation({0.0f, 0.0f, 0.0f, 1.00011f}), L"beyond it");
    Assert::IsFalse(NeuronCore::IsUnitRotation({0.0f, 0.0f, 0.6f, -0.8f}), L"w below 0");
    Assert::IsFalse(NeuronCore::IsUnitRotation({0.0f, 0.0f, 0.0f, 0.0f}), L"nothing at all");
  }

  // RotationOf against the double-precision matrix of the normalized quaternion, for quaternions up to 10^-4 from unit
  // length, as the protocol lets through (§6.2); and every rotation it gives is orthonormal and right-handed within
  // rounding.
  TEST_METHOD(RotationOfMatchesADoubleReference)
  {
    SeededRandom random(20261002u);
    double worst = 0.0;
    for (std::uint32_t sample = 0; sample < 4096u; ++sample)
    {
      DoubleQuaternion q{};
      double length = 0.0;
      do
      {
        q = {random.Uniform(-1.0f, 1.0f), random.Uniform(-1.0f, 1.0f), random.Uniform(-1.0f, 1.0f), random.Uniform(-1.0f, 1.0f)};
        length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
      } while (length < 0.1 || length > 1.0);
      q = {q.x / length, q.y / length, q.z / length, q.w / length};
      const double scale = 1.0 + static_cast<double>(random.Uniform(-1.0e-4f, 1.0e-4f));
      const Quaternion stored{static_cast<float>(q.x * scale), static_cast<float>(q.y * scale), static_cast<float>(q.z * scale),
                              static_cast<float>(q.w * scale)};
      std::array<double, 9> expected{};
      RotationOfInDouble(q, expected);
      Rotation rotation = NeuronCore::RotationOf(stored);
      for (std::uint32_t entry = 0; entry < 9u; ++entry)
      {
        worst = std::max(worst, std::abs(static_cast<double>(Entry(rotation, entry)) - expected[entry]));
      }
      const Float3 handed = NeuronCore::Cross(rotation.axisX, rotation.axisY);
      Assert::AreEqual(0.0f, NeuronCore::Dot(rotation.axisX, rotation.axisY), 1.0e-6f);
      Assert::AreEqual(1.0f, NeuronCore::Length(rotation.axisX), 1.0e-6f);
      Assert::AreEqual(1.0f, NeuronCore::Length(rotation.axisY), 1.0e-6f);
      Assert::AreEqual(1.0f, NeuronCore::Dot(handed, rotation.axisZ), 2.0e-6f);
    }
    Logger::WriteMessage(std::format(L"RotationOf's worst entry is {} from the double reference\n", worst).c_str());
    // The quaternion's own rounding to float moves an entry by up to a few times 10^-8; the products add a few more.
    Assert::IsTrue(worst < 1.0e-6, L"every entry within 10^-6 of the reference");
  }

  // TransformPoint against double precision on the same float inputs, over the world's bound (§7.5): within a few units
  // in the last place of the largest term.
  TEST_METHOD(TransformsMatchADoubleReference)
  {
    SeededRandom random(20261003u);
    for (std::uint32_t sample = 0; sample < 4096u; ++sample)
    {
      NeuronCore::RigidTransform transform{};
      random.Rotation(transform.rotation.axisX, transform.rotation.axisY, transform.rotation.axisZ);
      transform.translation = random.InBox({-16384.0f, -16384.0f, -16384.0f}, {16384.0f, 16384.0f, 16384.0f});
      const Float3 point = random.InBox({0.0f, 0.0f, 0.0f}, {256.0f, 256.0f, 256.0f});
      const Float3 moved = NeuronCore::TransformPoint(transform, point);
      const Rotation& r = transform.rotation;
      const auto expected = [&](float _translation, float _x, float _y, float _z) noexcept
      {
        return static_cast<double>(_translation) + static_cast<double>(_x) * point.x + static_cast<double>(_y) * point.y +
               static_cast<double>(_z) * point.z;
      };
      const std::array<double, 3> reference{expected(transform.translation.x, r.axisX.x, r.axisY.x, r.axisZ.x),
                                            expected(transform.translation.y, r.axisX.y, r.axisY.y, r.axisZ.y),
                                            expected(transform.translation.z, r.axisX.z, r.axisY.z, r.axisZ.z)};
      const std::array<float, 3> actual{moved.x, moved.y, moved.z};
      const std::array<float, 3> translation{transform.translation.x, transform.translation.y, transform.translation.z};
      for (std::size_t axis = 0; axis < 3; ++axis)
      {
        // The largest term is at most |t| + √3 × 256 and a float holds it to 2^-24 of that; four such roundings.
        const double allowed = 4.0 * (std::abs(static_cast<double>(translation[axis])) + 444.0) / 16777216.0;
        Assert::IsTrue(std::abs(static_cast<double>(actual[axis]) - reference[axis]) <= allowed,
                       std::format(L"sample {}, axis {}: {} against {}", sample, axis, actual[axis], reference[axis]).c_str());
      }

      // And back: within the same roundings again, and exactly under the identity.
      const Float3 back = NeuronCore::InverseTransformPoint(transform, moved);
      const double magnitude =
        std::max({std::abs(static_cast<double>(transform.translation.x)), std::abs(static_cast<double>(transform.translation.y)),
                  std::abs(static_cast<double>(transform.translation.z))}) +
        444.0;
      Assert::IsTrue(NeuronCore::Length(back - point) <= static_cast<float>(16.0 * magnitude / 16777216.0),
                     std::format(L"sample {} comes back", sample).c_str());
      const NeuronCore::RigidTransform shift{NeuronCore::IDENTITY_ROTATION, transform.translation};
      const Float3 unshifted = NeuronCore::InverseTransformPoint(shift, moved);
      const Float3 exactly = moved - transform.translation;
      Assert::IsTrue(unshifted.x == exactly.x && unshifted.y == exactly.y && unshifted.z == exactly.z, L"the identity turns nothing");
    }
  }
};

} // namespace NeuronCoreTests
