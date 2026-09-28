#include "pch.h"

#include "Bloom.h"
#include "Float3.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{
namespace
{

using NeuronCore::BloomImage;
using NeuronCore::Float3;

[[nodiscard]] BloomImage FilledImage(std::uint32_t _widthPixels, std::uint32_t _heightPixels, Float3 _color)
{
  return {_widthPixels, _heightPixels, std::vector<Float3>(static_cast<std::size_t>(_widthPixels) * _heightPixels, _color)};
}

[[nodiscard]] Float3 At(const BloomImage& _image, std::uint32_t _x, std::uint32_t _y) noexcept
{
  return _image.texels[static_cast<std::size_t>(_y) * _image.widthPixels + _x];
}

// The bloom the tone map mixes into every pixel of a view of _hdr's size: the tent over the up chain's first level.
[[nodiscard]] BloomImage Bloom(const BloomImage& _hdr)
{
  std::vector<BloomImage> levels = NeuronCore::BloomDownChain(_hdr);
  NeuronCore::BloomUpChain(levels);
  BloomImage bloom = FilledImage(_hdr.widthPixels, _hdr.heightPixels, {0.0f, 0.0f, 0.0f});
  for (std::uint32_t y = 0; y < bloom.heightPixels; ++y)
  {
    for (std::uint32_t x = 0; x < bloom.widthPixels; ++x)
    {
      bloom.texels[static_cast<std::size_t>(y) * bloom.widthPixels + x] = NeuronCore::BloomTent(levels.front(), x, y);
    }
  }
  return bloom;
}

// Relative to the brightest texel of the two images: the difference between halves rounded one way and the other.
void AreCloseImages(const BloomImage& _expected, const BloomImage& _actual, const std::wstring& _what)
{
  Assert::AreEqual(_expected.widthPixels, _actual.widthPixels, _what.c_str());
  Assert::AreEqual(_expected.heightPixels, _actual.heightPixels, _what.c_str());
  float brightest = 0.0f;
  for (const Float3 texel : _expected.texels)
  {
    brightest = std::max(brightest, NeuronCore::MaxComponent(texel));
  }
  for (std::uint32_t y = 0; y < _expected.heightPixels; ++y)
  {
    for (std::uint32_t x = 0; x < _expected.widthPixels; ++x)
    {
      const Float3 expected = At(_expected, x, y);
      const Float3 actual = At(_actual, x, y);
      const std::wstring where = std::format(L"{}, texel {} {}", _what, x, y);
      Assert::AreEqual(expected.x, actual.x, 1.0e-3f * brightest, where.c_str());
      Assert::AreEqual(expected.y, actual.y, 1.0e-3f * brightest, where.c_str());
      Assert::AreEqual(expected.z, actual.z, 1.0e-3f * brightest, where.c_str());
    }
  }
}

} // namespace

// Bloom's twin of Design/SpaceScene.md §12.2 against §15's list: the GPU side is compared with it, level by level, in
// NeuronClientTests.
TEST_CLASS(BloomTests)
{
public:
  // Six levels at 1080p and one more for each doubling, down to the last level at least 16 texels across, and never
  // fewer than one or more than eight; each level half the one above it, rounded up.
  TEST_METHOD(CountsItsLevelsByTheView)
  {
    Assert::AreEqual(6u, NeuronCore::BloomLevelCount(1920, 1080));
    Assert::AreEqual(7u, NeuronCore::BloomLevelCount(3840, 2160));
    Assert::AreEqual(5u, NeuronCore::BloomLevelCount(1280, 720));
    Assert::AreEqual(6u, NeuronCore::BloomLevelCount(1080, 1920), L"the smaller dimension decides");
    Assert::AreEqual(8u, NeuronCore::BloomLevelCount(7680, 4320));
    Assert::AreEqual(8u, NeuronCore::BloomLevelCount(15360, 8640), L"no more than eight");
    Assert::AreEqual(1u, NeuronCore::BloomLevelCount(8, 8), L"no fewer than one");
    Assert::AreEqual(1u, NeuronCore::BloomLevelCount(1, 1));
    Assert::AreEqual(540u, NeuronCore::BloomLevelPixels(1080));
    Assert::AreEqual(68u, NeuronCore::BloomLevelPixels(135));
    Assert::AreEqual(1u, NeuronCore::BloomLevelPixels(1));
  }

  // §15: a constant image comes back from the chain unchanged: every level on the way down and up, the tent the tone map
  // takes, and the tone map's mix; with odd sizes, where the levels' edges are clamped.
  TEST_METHOD(KeepsAConstantImage)
  {
    const Float3 color{0.75f, 1.5f, 3.0f};
    const BloomImage hdr = FilledImage(257, 129, color);
    std::vector<BloomImage> levels = NeuronCore::BloomDownChain(hdr);
    Assert::AreEqual(std::size_t{3}, levels.size());
    const auto check = [&color](const BloomImage& _level, const wchar_t* _what)
    {
      for (const Float3 texel : _level.texels)
      {
        Assert::AreEqual(color.x, texel.x, _what);
        Assert::AreEqual(color.y, texel.y, _what);
        Assert::AreEqual(color.z, texel.z, _what);
      }
    };
    Assert::AreEqual(129u, levels[0].widthPixels);
    Assert::AreEqual(65u, levels[0].heightPixels);
    for (const BloomImage& level : levels)
    {
      check(level, L"down");
    }
    NeuronCore::BloomUpChain(levels);
    for (const BloomImage& level : levels)
    {
      check(level, L"up");
    }
    check(Bloom(hdr), L"the tone map's tent");
    const Float3 mixed = NeuronCore::MixBloom(color, color);
    Assert::AreEqual(color.y, mixed.y, L"the mix");
  }

  // §15: one bright texel spreads symmetrically about itself. Its mirror image spreads as its mirror image, across
  // either axis; one on the diagonal spreads alike along both axes; and its light, spread, keeps its sum and its
  // centre. Karis's average only reshapes a texel much brighter than its surroundings, so a dim one shows the centre.
  TEST_METHOD(SpreadsABrightTexelSymmetrically)
  {
    // Sizes that stay even at every level, so that a mirror image of the view is a mirror image of every level.
    constexpr std::uint32_t SIZE = 128;
    constexpr std::uint32_t AT = 45;
    const Float3 bright{100.0f, 100.0f, 100.0f};
    BloomImage hdr = FilledImage(SIZE, SIZE, {0.0f, 0.0f, 0.0f});
    hdr.texels[static_cast<std::size_t>(AT) * SIZE + AT] = bright;
    BloomImage mirrored = FilledImage(SIZE, SIZE, {0.0f, 0.0f, 0.0f});
    mirrored.texels[static_cast<std::size_t>(AT) * SIZE + (SIZE - 1 - AT)] = bright;
    const BloomImage bloom = Bloom(hdr);
    const BloomImage mirroredBloom = Bloom(mirrored);
    BloomImage unmirrored = bloom;
    BloomImage transposed = bloom;
    for (std::uint32_t y = 0; y < SIZE; ++y)
    {
      for (std::uint32_t x = 0; x < SIZE; ++x)
      {
        unmirrored.texels[static_cast<std::size_t>(y) * SIZE + x] = At(mirroredBloom, SIZE - 1 - x, y);
        transposed.texels[static_cast<std::size_t>(y) * SIZE + x] = At(bloom, y, x);
      }
    }
    AreCloseImages(bloom, unmirrored, L"mirrored");
    AreCloseImages(bloom, transposed, L"on the diagonal");

    // A dim texel's light, spread: its sum and its centre.
    BloomImage dim = FilledImage(SIZE, SIZE, {0.0f, 0.0f, 0.0f});
    dim.texels[static_cast<std::size_t>(AT) * SIZE + AT] = {1.0f / 64.0f, 1.0f / 64.0f, 1.0f / 64.0f};
    const BloomImage spread = Bloom(dim);
    double sum = 0.0;
    double column = 0.0;
    for (std::uint32_t y = 0; y < SIZE; ++y)
    {
      for (std::uint32_t x = 0; x < SIZE; ++x)
      {
        const double texel = At(spread, x, y).y;
        sum += texel;
        column += texel * (static_cast<double>(x) + 0.5);
      }
    }
    Logger::WriteMessage(std::format(L"the dim texel's light, spread: {} in all, centred on column {}\n", sum, column / sum).c_str());
    Assert::AreEqual(1.0 / 64.0, sum, 0.01 / 64.0, L"its sum");
    Assert::AreEqual(AT + 0.5, column / sum, 0.02, L"its centre");
  }

  // Karis's average keeps one brilliant texel from flaring: the first halving gives it far less than a plain average
  // would, and the halvings after it are plain.
  TEST_METHOD(KarisAverageTamesABrilliantTexel)
  {
    BloomImage hdr = FilledImage(32, 32, {0.0f, 0.0f, 0.0f});
    hdr.texels[static_cast<std::size_t>(16) * 32 + 16] = {10000.0f, 10000.0f, 10000.0f};
    const Float3 plain = NeuronCore::BloomDownsample(hdr, 8, 8, false);
    const Float3 karis = NeuronCore::BloomDownsample(hdr, 8, 8, true);
    Logger::WriteMessage(std::format(L"the texel's level-one neighbour: {} plain, {} with Karis's average\n", plain.y, karis.y).c_str());
    Assert::IsTrue(plain.y > 100.0f, L"a plain average flares");
    Assert::IsTrue(karis.y < 1.0f, L"Karis's does not");
  }
};

} // namespace NeuronCoreTests
