#include "pch.h"

#include <objbase.h>
#include <wincodec.h>

#include "CapturedFrame.h"
#include "ComApartment.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

// A file as WIC reads it back: its container, its size, its pixel format and, when that is 24-bit BGR, its pixels.
struct ReadBack
{
  GUID container;
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  WICPixelFormatGUID format;
  std::vector<BYTE> bgr;
};

[[nodiscard]] ReadBack ReadImage(const std::filesystem::path& _file)
{
  const NeuronClient::ComApartment apartment;
  winrt::com_ptr<IWICImagingFactory> factory;
  winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())));
  winrt::com_ptr<IWICBitmapDecoder> decoder;
  winrt::check_hresult(
    factory->CreateDecoderFromFilename(_file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put()));
  ReadBack read{};
  winrt::check_hresult(decoder->GetContainerFormat(&read.container));
  winrt::com_ptr<IWICBitmapFrameDecode> image;
  winrt::check_hresult(decoder->GetFrame(0, image.put()));
  winrt::check_hresult(image->GetSize(&read.widthPixels, &read.heightPixels));
  winrt::check_hresult(image->GetPixelFormat(&read.format));
  if (read.format == GUID_WICPixelFormat24bppBGR)
  {
    const UINT strideBytes = 3 * read.widthPixels;
    read.bgr.resize(std::size_t{strideBytes} * read.heightPixels);
    winrt::check_hresult(image->CopyPixels(nullptr, strideBytes, static_cast<UINT>(read.bgr.size()), read.bgr.data()));
  }
  return read;
}

} // namespace

TEST_CLASS(CapturedFrameTests)
{
public:
  // Design/ADR/ADR-031: what WritePng writes, WIC reads back as a PNG image of 8-bit RGB at the frame's size, every pixel's
  // red, green and blue as the frame held them, whatever its alpha.
  TEST_METHOD(WritesItsPixelsAsAPng)
  {
    constexpr std::uint32_t WIDTH = 5;
    constexpr std::uint32_t HEIGHT = 3;
    constexpr std::uint32_t PIXELS = WIDTH * HEIGHT;
    NeuronClient::CapturedFrame frame{WIDTH, HEIGHT, std::vector<std::byte>(std::size_t{PIXELS} * NeuronClient::CAPTURED_BYTES_PER_PIXEL)};
    for (std::uint32_t pixel = 0; pixel < PIXELS; ++pixel)
    {
      std::byte* rgba = frame.pixels.data() + std::size_t{pixel} * NeuronClient::CAPTURED_BYTES_PER_PIXEL;
      rgba[0] = static_cast<std::byte>(16 * pixel + 1);
      rgba[1] = static_cast<std::byte>(250 - 7 * pixel);
      rgba[2] = static_cast<std::byte>(100 + 3 * pixel);
      rgba[3] = static_cast<std::byte>(17 * pixel);
    }
    const std::filesystem::path file = std::filesystem::temp_directory_path() / L"CapturedFrameTests.png";
    static_cast<void>(std::filesystem::remove(file));
    NeuronClient::WritePng(frame, file);
    const ReadBack read = ReadImage(file);
    static_cast<void>(std::filesystem::remove(file));

    Assert::IsTrue(read.container == GUID_ContainerFormatPng, L"a PNG image");
    Assert::AreEqual(WIDTH, read.widthPixels, L"its width");
    Assert::AreEqual(HEIGHT, read.heightPixels, L"its height");
    Assert::IsTrue(read.format == GUID_WICPixelFormat24bppBGR, L"8-bit RGB, without alpha");
    for (std::uint32_t pixel = 0; pixel < PIXELS; ++pixel)
    {
      const std::byte* rgba = frame.pixels.data() + std::size_t{pixel} * NeuronClient::CAPTURED_BYTES_PER_PIXEL;
      const std::size_t at = std::size_t{3} * pixel;
      const bool same = read.bgr[at] == std::to_integer<BYTE>(rgba[2]) && read.bgr[at + 1] == std::to_integer<BYTE>(rgba[1]) &&
                        read.bgr[at + 2] == std::to_integer<BYTE>(rgba[0]);
      Assert::IsTrue(same, std::format(L"pixel {}", pixel).c_str());
    }
  }

  // An empty frame, or pixels of another count than its size, is refused before anything is written; a folder that does
  // not exist is WIC's failure, and says so.
  TEST_METHOD(RefusesWhatItCannotWrite)
  {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / L"CapturedFrameTestsRefused.png";
    static_cast<void>(std::filesystem::remove(file));
    const auto refused = [&file](const NeuronClient::CapturedFrame& _frame)
    {
      try
      {
        NeuronClient::WritePng(_frame, file);
      }
      catch (const std::invalid_argument&)
      {
        return true;
      }
      return false;
    };
    Assert::IsTrue(refused({0, 2, {}}), L"no pixels");
    Assert::IsTrue(refused({2, 2, std::vector<std::byte>(15)}), L"15 bytes for 4 pixels");
    Assert::IsFalse(std::filesystem::exists(file), L"nothing is written");

    const std::filesystem::path nowhere = std::filesystem::temp_directory_path() / L"CapturedFrameTestsNowhere";
    static_cast<void>(std::filesystem::remove_all(nowhere));
    bool failed = false;
    try
    {
      NeuronClient::WritePng({2, 2, std::vector<std::byte>(16)}, nowhere / L"Frame.png");
    }
    catch (const winrt::hresult_error&)
    {
      failed = true;
    }
    Assert::IsTrue(failed, L"a folder that does not exist");
  }
};

} // namespace NeuronClientTests
