#include "pch.h"

#include <objbase.h>
#include <wincodec.h>

#include "CapturedFrame.h"

#include "ComApartment.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
#include <vector>

namespace NeuronClient
{

void WritePng(const CapturedFrame& _frame, const std::filesystem::path& _file)
{
  if (_frame.widthPixels == 0 || _frame.heightPixels == 0)
  {
    throw std::invalid_argument(std::format("A frame of {} by {} pixels has none to write.", _frame.widthPixels, _frame.heightPixels));
  }
  const std::uint64_t pixelCount = std::uint64_t{_frame.widthPixels} * _frame.heightPixels;
  if (_frame.pixels.size() != pixelCount * CAPTURED_BYTES_PER_PIXEL)
  {
    throw std::invalid_argument(std::format("A frame of {} by {} pixels takes {} bytes, not {}.", _frame.widthPixels, _frame.heightPixels,
                                            pixelCount * CAPTURED_BYTES_PER_PIXEL, _frame.pixels.size()));
  }
  // WIC takes the pixels in one call, whose sizes are 32-bit.
  if (pixelCount * 3 > std::numeric_limits<UINT>::max())
  {
    throw std::invalid_argument(
      std::format("A frame of {} by {} pixels is more than WIC writes at once.", _frame.widthPixels, _frame.heightPixels));
  }

  // The PNG encoder writes 24-bit BGR as it is: each pixel's red and blue change places, and its alpha is left out.
  const std::size_t strideBytes = std::size_t{3} * _frame.widthPixels;
  std::vector<BYTE> bgr(strideBytes * _frame.heightPixels);
  for (std::size_t pixel = 0; pixel < bgr.size() / 3; ++pixel)
  {
    const std::byte* rgba = _frame.pixels.data() + pixel * CAPTURED_BYTES_PER_PIXEL;
    bgr[3 * pixel] = std::to_integer<BYTE>(rgba[2]);
    bgr[3 * pixel + 1] = std::to_integer<BYTE>(rgba[1]);
    bgr[3 * pixel + 2] = std::to_integer<BYTE>(rgba[0]);
  }

  // The apartment outlives every object of WIC's below.
  const ComApartment apartment;
  winrt::com_ptr<IWICImagingFactory> factory;
  winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())));
  winrt::com_ptr<IWICStream> stream;
  winrt::check_hresult(factory->CreateStream(stream.put()));
  winrt::check_hresult(stream->InitializeFromFilename(_file.c_str(), GENERIC_WRITE));
  winrt::com_ptr<IWICBitmapEncoder> encoder;
  winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
  winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
  winrt::com_ptr<IWICBitmapFrameEncode> frame;
  winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
  winrt::check_hresult(frame->Initialize(nullptr));
  winrt::check_hresult(frame->SetSize(_frame.widthPixels, _frame.heightPixels));
  WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
  winrt::check_hresult(frame->SetPixelFormat(&format));
  if (format != GUID_WICPixelFormat24bppBGR)
  {
    throw std::runtime_error("WIC's PNG encoder did not take 24-bit BGR pixels.");
  }
  winrt::check_hresult(frame->WritePixels(_frame.heightPixels, static_cast<UINT>(strideBytes), static_cast<UINT>(bgr.size()), bgr.data()));
  winrt::check_hresult(frame->Commit());
  winrt::check_hresult(encoder->Commit());
}

} // namespace NeuronClient
