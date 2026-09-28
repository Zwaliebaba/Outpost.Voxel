#include "pch.h"

#include "DescriptorHeap.h"
#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "SkyConstants.h"
#include "SkyPass.h"
#include "SplatPass.h"
#include "TestSupport.h"
#include "UploadRing.h"
#include "ViewConstants.h"
#include "ViewTargets.h"
#include "VoxelScene.h"

#include "Float3.h"
#include "Half.h"
#include "Message.h"
#include "PerspectiveView.h"
#include "Placement.h"
#include "Quaternion.h"
#include "RigidTransform.h"
#include "Sky.h"
#include "StarCatalog.h"
#include "VoxModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronCore::Float2;
using NeuronCore::Float3;

// The HDR color is stored in half precision, 11 significant bits, truncated as the twin truncates it; but the GPU's exp,
// log and sqrt differ from the CPU's by a few ulps (Design/SpaceScene.md §17), so a pixel within a float's rounding of a
// half may be stored one step the other way, and each star it adds may add another. Four steps, and a floor for the
// faintest galaxy; the test logs the most any pixel strays.
constexpr float RELATIVE_TOLERANCE = 2.0e-3f;
constexpr float ABSOLUTE_TOLERANCE = 1.0e-5f;

// Within this distance of a quad's edge, a pixel's centre may fall either way once the rasterizer has snapped the quad's
// corners to its 1/256 of a pixel: whatever the star adds there is allowed on top of the tolerance.
constexpr float EDGE_PIXELS = 1.0f / 128.0f;

// How many of the pixels beyond the bounds a failure names.
constexpr std::uint32_t STRAYS_NAMED = 8;

// What voxels leave in the HDR color here: the sky pass must not change it.
constexpr std::array<float, 4> UNDER_THE_SKY{0.25f, 0.5f, 0.75f, 1.0f};

// An HDR color of _pixels pixels, every one UNDER_THE_SKY, as its halves hold it. The test writes it rather than clearing
// the target, which the debug layer warns is slow without a clear value the target was made with; the renderer never
// clears it, since the lighting writes every pixel.
[[nodiscard]] std::vector<std::byte> UnderTheSky(std::size_t _pixels)
{
  std::vector<std::uint16_t> halves;
  halves.reserve(_pixels * UNDER_THE_SKY.size());
  for (std::size_t pixel = 0; pixel < _pixels; ++pixel)
  {
    for (const float channel : UNDER_THE_SKY)
    {
      halves.push_back(NeuronCore::FloatToHalf(channel));
    }
  }
  std::vector<std::byte> bytes(halves.size() * sizeof(std::uint16_t));
  std::memcpy(bytes.data(), halves.data(), bytes.size());
  return bytes;
}

// The rotation whose x axis is _core and whose y axis is as near _pole as a perpendicular one can be.
[[nodiscard]] NeuronCore::Quaternion GalaxyFacing(Float3 _core, Float3 _pole) noexcept
{
  const Float3 x = NeuronCore::Normalize(_core);
  const Float3 y = NeuronCore::Normalize(_pole - x * NeuronCore::Dot(_pole, x));
  return NeuronCore::QuaternionOf({x, y, NeuronCore::Cross(x, y)});
}

[[nodiscard]] Float3 Direction(const NeuronCore::PerspectiveView& _view, std::uint32_t _x, std::uint32_t _y) noexcept
{
  return NeuronCore::Normalize(NeuronCore::PerspectiveRay(_view, _x, _y).direction);
}

// Whether a pixel's centre lies within EDGE_PIXELS of the edge of a star's quad.
[[nodiscard]] bool NearQuadEdge(const NeuronCore::PerspectiveView& _view, const NeuronCore::StarRecord& _star, float _starGain,
                                std::uint32_t _x, std::uint32_t _y) noexcept
{
  const std::optional<Float2> position = NeuronCore::StarPosition(_view, _star.direction);
  const float radius = NeuronCore::StarQuadRadius(_star.flux * _starGain);
  if (!position || radius == 0.0f)
  {
    return false;
  }
  const Float2 offset = Float2{static_cast<float>(_x) + 0.5f, static_cast<float>(_y) + 0.5f} - *position;
  const auto onEdge = [radius](float _along) { return std::abs(std::abs(_along) - radius) <= EDGE_PIXELS; };
  const bool within = std::abs(offset.x) <= radius + EDGE_PIXELS && std::abs(offset.y) <= radius + EDGE_PIXELS;
  return within && (onEdge(offset.x) || onEdge(offset.y));
}

} // namespace

// The sky pass against its twins (Design/SpaceScene.md §11, §15, R15), on the depth the view splat itself wrote: the galaxy
// and the sun in every pixel no voxel covers, the catalog's stars added over them, a bright star a voxel hides left out,
// and every pixel a voxel covers left as it was.
TEST_CLASS(SkyPassTests)
{
public:
  TEST_METHOD(DrawsTheSkyAndItsStarsAsTheTwinsDo)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const NeuronCore::VoxModel model = LoadMilitaryStation();
        const NeuronClient::VoxelScene scene(_device, {&model, 1});
        const std::vector<NeuronCore::Placement> placements = WholePlacements(model);
        const NeuronClient::SplatPass viewSplat(_device, NeuronClient::SplatPass::Kind::View);
        const Float3 center{0.5f, 127.5f, 0.5f};
        const NeuronCore::PerspectiveView view = NeuronCore::MakePerspectiveView(
          center + Float3{-318.43f, 260.0f, -318.43f}, center, {0.0f, 1.0f, 0.0f}, TEST_FOV_Y_RADIANS, TEST_NEAR_PLANE, 161, 91);

        // Where the station is: the view splat's depth, before any sky.
        const SplatImage splat = RenderSplat(_device, scene, placements, viewSplat, view);
        const auto open = [&splat, &view](std::uint32_t _x, std::uint32_t _y)
        { return NeuronCore::IsFarPerspectiveDepth(splat.depth[static_cast<std::size_t>(_y) * view.widthPixels + _x]); };
        Assert::IsTrue(open(20, 12) && open(150, 80) && !open(80, 45), L"the station fills the middle and leaves the corners open");

        // The sun near the top left, the galaxy's core toward the right with its band across the view, and the
        // catalog's stars with two of the test's own: a bright one in the open, and a brighter one behind the station.
        NeuronCore::WorldSettings world = TestWorld();
        world.toSun = Direction(view, 20, 12);
        world.galacticPlane = GalaxyFacing(Direction(view, 140, 50), view.up + view.right * 0.3f);
        const NeuronCore::SkyParameters sky = NeuronCore::MakeSkyParameters(world);
        std::vector<NeuronCore::StarRecord> stars = NeuronCore::MakeStarCatalog(world.skySeed, world.galacticPlane, NeuronCore::STAR_COUNT);
        const NeuronCore::StarRecord seen{Direction(view, 150, 80), 3.0f, NeuronCore::StarColor(9000.0f)};
        const NeuronCore::StarRecord hidden{Direction(view, 80, 45), 40.0f, NeuronCore::StarColor(4000.0f)};
        stars.push_back(seen);
        stars.push_back(hidden);
        const NeuronClient::SkyPass pass(_device, stars);

        NeuronClient::DescriptorHeap rtvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false, L"Test render target views");
        NeuronClient::DescriptorHeap dsvHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false, L"Test depth stencil views");
        NeuronClient::DescriptorHeap shaderHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 12, true, L"Test shader views");
        NeuronClient::DescriptorHeap cpuHeap(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, false, L"Test CPU-only views");
        NeuronClient::ViewTargets targets(rtvHeap, dsvHeap, shaderHeap, cpuHeap);
        targets.Resize(_device, view.widthPixels, view.heightPixels);
        NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Test constants");
        const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = constants.Push(NeuronClient::MakeViewConstants(view));
        const D3D12_GPU_VIRTUAL_ADDRESS skyConstants = constants.Push(NeuronClient::MakeSkyConstants(sky));
        const NeuronClient::SplatPlacements pushed = PushTestPlacements(constants, placements);
        WriteTexture2D(_device, targets.HdrColor(), NeuronClient::ViewTargets::READABLE,
                       UnderTheSky(static_cast<std::size_t>(view.widthPixels) * view.heightPixels),
                       NeuronClient::ViewTargets::HDR_BYTES_PER_PIXEL);

        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            std::array<ID3D12DescriptorHeap*, 1> heaps{shaderHeap.Heap()};
            _list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
            targets.BeginSplat(_list);
            viewSplat.Record(_list, scene, viewConstants, pushed.constants, pushed.draws);
            targets.EndSplat(_list);
            targets.BeginSky(_list);
            pass.Record(_list, viewConstants, skyConstants);
            targets.EndSky(_list);
          });

        const std::size_t pixels = static_cast<std::size_t>(view.widthPixels) * view.heightPixels;
        std::vector<float> depth(pixels);
        const std::vector<std::byte> depthBytes =
          NeuronClient::ReadTexture2D(_device, targets.Depth(), NeuronClient::ViewTargets::READABLE, sizeof(float));
        std::memcpy(depth.data(), depthBytes.data(), depthBytes.size());
        std::vector<std::uint16_t> hdr(pixels * 4);
        const std::vector<std::byte> hdrBytes = NeuronClient::ReadTexture2D(
          _device, targets.HdrColor(), NeuronClient::ViewTargets::READABLE, NeuronClient::ViewTargets::HDR_BYTES_PER_PIXEL);
        std::memcpy(hdr.data(), hdrBytes.data(), hdrBytes.size());

        // For each pixel, in the order the GPU draws them, the stars whose quads reach it or come near it.
        std::vector<std::vector<std::uint32_t>> reaching(pixels);
        for (std::uint32_t i = 0; i < stars.size(); ++i)
        {
          const std::optional<Float2> position = NeuronCore::StarPosition(view, stars[i].direction);
          const float radius = NeuronCore::StarQuadRadius(stars[i].flux * sky.starGain);
          if (!position || radius == 0.0f)
          {
            continue;
          }
          const auto first = [radius](float _center) { return std::max(static_cast<int>(std::floor(_center - radius)) - 1, 0); };
          const auto last = [radius](float _center, std::uint32_t _size)
          { return std::min(static_cast<int>(std::ceil(_center + radius)) + 1, static_cast<int>(_size) - 1); };
          for (int y = first(position->y); y <= last(position->y, view.heightPixels); ++y)
          {
            for (int x = first(position->x); x <= last(position->x, view.widthPixels); ++x)
            {
              reaching[static_cast<std::size_t>(y) * view.widthPixels + static_cast<std::size_t>(x)].push_back(i);
            }
          }
        }

        std::uint32_t skyPixels = 0;
        std::uint32_t starPixels = 0;
        std::uint32_t sunPixels = 0;
        std::uint32_t hiddenPixels = 0;
        std::uint32_t exactPixels = 0;
        std::uint32_t worstSteps = 0;
        // Every pixel beyond the bounds, so that one run shows them all; the first few are named.
        std::uint32_t strayPixels = 0;
        std::wstring strays;
        for (std::uint32_t y = 0; y < view.heightPixels; ++y)
        {
          for (std::uint32_t x = 0; x < view.widthPixels; ++x)
          {
            const std::size_t pixel = static_cast<std::size_t>(y) * view.widthPixels + x;
            const Float3 actual{NeuronCore::HalfToFloat(hdr[4 * pixel]), NeuronCore::HalfToFloat(hdr[4 * pixel + 1]),
                                NeuronCore::HalfToFloat(hdr[4 * pixel + 2])};
            // What the triangle writes, stored as a half, then each star added in the order the GPU draws them, and
            // stored again; or, where a voxel is, the color left under the sky.
            Float3 expected{UNDER_THE_SKY[0], UNDER_THE_SKY[1], UNDER_THE_SKY[2]};
            float allowance = 0.0f;
            if (NeuronCore::IsFarPerspectiveDepth(depth[pixel]))
            {
              ++skyPixels;
              const Float3 skyColor = NeuronCore::SkyPixel(view, x, y, sky);
              sunPixels += NeuronCore::SunRadiance(Direction(view, x, y), sky, NeuronCore::PixelRadians(view)).y > 0.0f ? 1u : 0u;
              expected = {NeuronCore::RoundToHalf(skyColor.x), NeuronCore::RoundToHalf(skyColor.y), NeuronCore::RoundToHalf(skyColor.z)};
              bool lit = false;
              for (const std::uint32_t index : reaching[pixel])
              {
                const NeuronCore::StarRecord& star = stars[index];
                const Float3 added = NeuronCore::StarPixel(view, star, sky.starGain, x, y);
                if (NearQuadEdge(view, star, sky.starGain, x, y))
                {
                  allowance += NeuronCore::MaxComponent(added) + NeuronCore::STAR_DARKEST_VISIBLE;
                }
                if (added.x != 0.0f || added.y != 0.0f || added.z != 0.0f)
                {
                  lit = true;
                  expected = {NeuronCore::RoundToHalf(expected.x + added.x), NeuronCore::RoundToHalf(expected.y + added.y),
                              NeuronCore::RoundToHalf(expected.z + added.z)};
                }
              }
              starPixels += lit ? 1u : 0u;
              if (allowance == 0.0f)
              {
                const std::uint32_t steps =
                  std::max({HalfSteps(expected.x, actual.x), HalfSteps(expected.y, actual.y), HalfSteps(expected.z, actual.z)});
                exactPixels += steps == 0 ? 1u : 0u;
                worstSteps = std::max(worstSteps, steps);
              }
            }
            else if (NeuronCore::MaxComponent(NeuronCore::StarPixel(view, hidden, sky.starGain, x, y)) > 0.0f)
            {
              ++hiddenPixels;
            }
            const auto close = [allowance](float _expected, float _actual)
            { return std::abs(_actual - _expected) <= RELATIVE_TOLERANCE * std::abs(_expected) + ABSOLUTE_TOLERANCE + allowance; };
            if (!close(expected.x, actual.x) || !close(expected.y, actual.y) || !close(expected.z, actual.z))
            {
              if (++strayPixels <= STRAYS_NAMED)
              {
                strays += std::format(L"; ({}, {}): ({}, {}, {}), the twins' ({}, {}, {})", x, y, actual.x, actual.y, actual.z, expected.x,
                                      expected.y, expected.z);
              }
            }
          }
        }
        Logger::WriteMessage(std::format(L"{} sky pixels, {} of them lit by stars and {} by the sun; {} pixels hide the bright star\n",
                                         skyPixels, starPixels, sunPixels, hiddenPixels)
                               .c_str());
        Logger::WriteMessage(
          std::format(L"away from a quad's edge, {} sky pixels match the twins to the bit, and none strays more than {} halves\n",
                      exactPixels, worstSteps)
            .c_str());
        Assert::AreEqual(0u, strayPixels, std::format(L"pixels beyond the bounds{}", strays).c_str());
        Assert::IsTrue(skyPixels > 0 && starPixels > 0 && sunPixels > 0, L"the view shows the sky, stars and the sun");
        Assert::IsTrue(hiddenPixels > 0, L"a voxel hides the bright star");
        const std::size_t seenPixel = static_cast<std::size_t>(80) * view.widthPixels + 150;
        Assert::IsTrue(NeuronCore::HalfToFloat(hdr[4 * seenPixel + 1]) > 1.0f, L"the bright star in the open shows");
      });
  }
};

} // namespace NeuronClientTests
