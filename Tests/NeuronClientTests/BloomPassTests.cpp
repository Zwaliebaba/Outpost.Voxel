#include "pch.h"

#include "BloomChain.h"
#include "BloomPass.h"
#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "TestSupport.h"
#include "ToneMapPass.h"
#include "ViewTargets.h"

#include "Bloom.h"
#include "Float3.h"
#include "Half.h"
#include "ToneMap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::BloomImage;
using NeuronCore::Float3;

// An odd view, so that the levels round up and their edges clamp: 390 × 210 halves to 195 × 105, 98 × 53 and 49 × 27.
constexpr std::uint32_t WIDTH_PIXELS = 390;
constexpr std::uint32_t HEIGHT_PIXELS = 210;
constexpr std::uint32_t LEVELS = 3;

constexpr DXGI_FORMAT COLOR_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr std::uint32_t COLOR_BYTES_PER_PIXEL = 16;

// Each level is stored in half precision, which keeps 11 significant bits, truncated as the twin truncates it; but the
// GPU sums a texel's taps in its own order, so a texel that lands within a float's rounding of a half may be stored one
// step the other way, and carry the step up the chain. Four steps, and a floor for texels near zero; the test logs the
// most any texel strays.
constexpr float LEVEL_RELATIVE_TOLERANCE = 2.0e-3f;
constexpr float LEVEL_ABSOLUTE_TOLERANCE = 1.0e-5f;

// The tone map computes a few single-precision operations on what the GPU stored, as the twin does on the same values.
constexpr float DISPLAY_TOLERANCE = 1.0e-5f;

// A view's worth of light for bloom to spread: a dim gradient, noise, a bright block on the right edge, and three
// brilliant texels, a firefly among them in the corner, each stored as a half.
[[nodiscard]] BloomImage TestImage()
{
  BloomImage image{WIDTH_PIXELS, HEIGHT_PIXELS, std::vector<Float3>(static_cast<std::size_t>(WIDTH_PIXELS) * HEIGHT_PIXELS)};
  for (std::uint32_t y = 0; y < HEIGHT_PIXELS; ++y)
  {
    for (std::uint32_t x = 0; x < WIDTH_PIXELS; ++x)
    {
      const float gradient = 0.02f + 0.3f * static_cast<float>(x) / WIDTH_PIXELS;
      const float noise = static_cast<float>((x * 7919u + y * 104729u) % 101u) / 400.0f;
      Float3 texel{gradient + noise, 0.5f * gradient + noise * 0.5f, 0.1f + noise};
      if (x >= WIDTH_PIXELS - 12 && y >= 80 && y < 110)
      {
        texel = {40.0f, 30.0f, 20.0f};
      }
      image.texels[static_cast<std::size_t>(y) * WIDTH_PIXELS + x] = {NeuronCore::RoundToHalf(texel.x), NeuronCore::RoundToHalf(texel.y),
                                                                      NeuronCore::RoundToHalf(texel.z)};
    }
  }
  image.texels[static_cast<std::size_t>(100) * WIDTH_PIXELS + 200] = {3000.0f, 3000.0f, 3000.0f};
  image.texels[static_cast<std::size_t>(37) * WIDTH_PIXELS + 61] = {500.0f, 200.0f, 50.0f};
  image.texels[0] = {20000.0f, 20000.0f, 20000.0f};
  return image;
}

// _image as an R16G16B16A16_FLOAT texture holds it, alpha one.
[[nodiscard]] std::vector<std::byte> HalfTexels(const BloomImage& _image)
{
  std::vector<std::uint16_t> halves;
  halves.reserve(_image.texels.size() * 4);
  for (const Float3 texel : _image.texels)
  {
    halves.push_back(NeuronCore::FloatToHalf(texel.x));
    halves.push_back(NeuronCore::FloatToHalf(texel.y));
    halves.push_back(NeuronCore::FloatToHalf(texel.z));
    halves.push_back(NeuronCore::FloatToHalf(1.0f));
  }
  std::vector<std::byte> bytes(halves.size() * sizeof(std::uint16_t));
  std::memcpy(bytes.data(), halves.data(), bytes.size());
  return bytes;
}

// Every level of _chain, read back.
[[nodiscard]] std::vector<BloomImage> ReadLevels(NeuronClient::GraphicsDevice& _device, const NeuronClient::BloomChain& _chain)
{
  std::vector<BloomImage> levels;
  for (std::uint32_t level = 0; level < _chain.LevelCount(); ++level)
  {
    const std::vector<std::byte> bytes = NeuronClient::ReadTexture2D(_device, _chain.Texture(level), NeuronClient::BloomChain::READABLE,
                                                                     NeuronClient::BloomChain::BYTES_PER_TEXEL);
    std::vector<std::uint16_t> halves(bytes.size() / sizeof(std::uint16_t));
    std::memcpy(halves.data(), bytes.data(), bytes.size());
    BloomImage image{_chain.WidthPixels(level), _chain.HeightPixels(level), {}};
    image.texels.reserve(halves.size() / 4);
    for (std::size_t i = 0; i < halves.size(); i += 4)
    {
      image.texels.push_back(
        {NeuronCore::HalfToFloat(halves[i]), NeuronCore::HalfToFloat(halves[i + 1]), NeuronCore::HalfToFloat(halves[i + 2])});
    }
    levels.push_back(std::move(image));
  }
  return levels;
}

[[nodiscard]] bool Close(float _expected, float _actual) noexcept
{
  return std::abs(_actual - _expected) <= LEVEL_RELATIVE_TOLERANCE * std::abs(_expected) + LEVEL_ABSOLUTE_TOLERANCE;
}

// Every texel of every level against the twin's; how many match to the bit, and the most halves any other strays.
void ExpectLevels(const std::vector<BloomImage>& _expected, const std::vector<BloomImage>& _actual, const wchar_t* _way)
{
  Assert::AreEqual(_expected.size(), _actual.size(), _way);
  for (std::size_t level = 0; level < _expected.size(); ++level)
  {
    Assert::AreEqual(_expected[level].widthPixels, _actual[level].widthPixels, _way);
    Assert::AreEqual(_expected[level].heightPixels, _actual[level].heightPixels, _way);
    std::size_t exact = 0;
    std::uint32_t worst = 0;
    for (std::size_t i = 0; i < _expected[level].texels.size(); ++i)
    {
      const Float3 expected = _expected[level].texels[i];
      const Float3 actual = _actual[level].texels[i];
      Assert::IsTrue(Close(expected.x, actual.x) && Close(expected.y, actual.y) && Close(expected.z, actual.z),
                     std::format(L"{}, level {}, texel {} {}: ({}, {}, {}), the twin's ({}, {}, {})", _way, level + 1,
                                 i % _expected[level].widthPixels, i / _expected[level].widthPixels, actual.x, actual.y, actual.z,
                                 expected.x, expected.y, expected.z)
                       .c_str());
      exact += expected.x == actual.x && expected.y == actual.y && expected.z == actual.z ? 1u : 0u;
      worst = std::max({worst, HalfSteps(expected.x, actual.x), HalfSteps(expected.y, actual.y), HalfSteps(expected.z, actual.z)});
    }
    Logger::WriteMessage(std::format(L"{}, level {}: {} of {} texels match the twin to the bit, and none strays more than {} halves\n",
                                     _way, level + 1, exact, _expected[level].texels.size(), worst)
                           .c_str());
  }
}

} // namespace

// Bloom against its twin (Design/SpaceScene.md §12.2, §15, R15): the chain texel by texel at every level, on the way down
// from an HDR color the test writes and on the way back up from what the GPU wrote going down; and the tone map's mix,
// from the HDR color and the first level the GPU holds.
TEST_CLASS(BloomPassTests)
{
public:
  TEST_METHOD(BloomsAsTheTwinDoes)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const BloomImage hdr = TestImage();
        NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3, false, L"Test render target views");
        NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false, L"Test depth stencil views");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 7 + 2 * NeuronCore::BLOOM_MAX_LEVELS, true,
                                                L"Test shader views");
        NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false, L"Test CPU-only views");
        NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
        targets.Resize(_device, WIDTH_PIXELS, HEIGHT_PIXELS);
        NeuronClient::BloomChain chain(shaderHeap);
        chain.Resize(_device, WIDTH_PIXELS, HEIGHT_PIXELS);
        Assert::AreEqual(LEVELS, chain.LevelCount());
        const NeuronClient::BloomPass bloom(_device);
        const NeuronClient::ToneMapPass toneMap(_device, COLOR_FORMAT);
        const winrt::com_ptr<ID3D12Resource> color =
          NeuronClient::CreateTexture2D(_device, COLOR_FORMAT, WIDTH_PIXELS, HEIGHT_PIXELS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, L"Test color");
        const std::uint32_t colorView = rtvHeap.Allocate();
        _device.Device()->CreateRenderTargetView(color.get(), nullptr, rtvHeap.Cpu(colorView));
        WriteTexture2D(_device, targets.HdrColor(), NeuronClient::ViewTargets::READABLE, HalfTexels(hdr),
                       NeuronClient::ViewTargets::HDR_BYTES_PER_PIXEL);
        const std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};

        // Down: from the HDR color.
        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            bloom.RecordDown(_list, targets, chain);
          });
        const std::vector<BloomImage> down = ReadLevels(_device, chain);
        ExpectLevels(NeuronCore::BloomDownChain(hdr), down, L"down");

        // Up: from the levels the GPU wrote going down.
        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            bloom.RecordUp(_list, chain);
          });
        const std::vector<BloomImage> up = ReadLevels(_device, chain);
        std::vector<BloomImage> expectedUp = down;
        NeuronCore::BloomUpChain(expectedUp);
        ExpectLevels(expectedUp, up, L"up");

        // The tone map mixes the tent over the first level into the HDR color.
        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            const D3D12_CPU_DESCRIPTOR_HANDLE target = rtvHeap.Cpu(colorView);
            _list->OMSetRenderTargets(1, &target, FALSE, nullptr);
            const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(WIDTH_PIXELS), static_cast<float>(HEIGHT_PIXELS), 0.0f, 1.0f};
            const D3D12_RECT scissor{0, 0, static_cast<LONG>(WIDTH_PIXELS), static_cast<LONG>(HEIGHT_PIXELS)};
            _list->RSSetViewports(1, &viewport);
            _list->RSSetScissorRects(1, &scissor);
            toneMap.Record(_list, targets, chain, 1.0f);
          });
        const std::vector<std::byte> displayBytes =
          NeuronClient::ReadTexture2D(_device, color.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, COLOR_BYTES_PER_PIXEL);
        std::vector<float> display(displayBytes.size() / sizeof(float));
        std::memcpy(display.data(), displayBytes.data(), displayBytes.size());
        float brightest = 0.0f;
        float worst = 0.0f;
        for (std::uint32_t y = 0; y < HEIGHT_PIXELS; ++y)
        {
          for (std::uint32_t x = 0; x < WIDTH_PIXELS; ++x)
          {
            const std::size_t pixel = static_cast<std::size_t>(y) * WIDTH_PIXELS + x;
            const Float3 bloomed = NeuronCore::BloomTent(up.front(), x, y);
            brightest = std::max(brightest, bloomed.y);
            const Float3 mapped = NeuronCore::ToneMap(NeuronCore::MixBloom(hdr.texels[pixel], bloomed), 1.0f);
            worst = std::max({worst, std::abs(display[4 * pixel] - mapped.x), std::abs(display[4 * pixel + 1] - mapped.y),
                              std::abs(display[4 * pixel + 2] - mapped.z)});
            const bool close = std::abs(display[4 * pixel] - mapped.x) <= DISPLAY_TOLERANCE &&
                               std::abs(display[4 * pixel + 1] - mapped.y) <= DISPLAY_TOLERANCE &&
                               std::abs(display[4 * pixel + 2] - mapped.z) <= DISPLAY_TOLERANCE;
            Assert::IsTrue(close, std::format(L"({}, {}): displayed ({}, {}, {}), the twin's ({}, {}, {})", x, y, display[4 * pixel],
                                              display[4 * pixel + 1], display[4 * pixel + 2], mapped.x, mapped.y, mapped.z)
                                    .c_str());
          }
        }
        Logger::WriteMessage(std::format(L"the tone map strays from the twin's by {} at most\n", worst).c_str());
        Assert::IsTrue(brightest > 1.0f, L"the bright texels bloom");
      });
  }
};

} // namespace NeuronClientTests
