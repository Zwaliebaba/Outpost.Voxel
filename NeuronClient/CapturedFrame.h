#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace NeuronClient
{

// The bytes of a captured frame's pixel: red, green, blue and alpha, eight bits each, as the swap chain's buffers hold
// them, encoded as sRGB by the view the frame was drawn through (SwapChain::BUFFER_FORMAT, SwapChain::VIEW_FORMAT).
inline constexpr std::uint32_t CAPTURED_BYTES_PER_PIXEL = 4;

// A frame as it was presented (Design/ADR/ADR-031): its rows from the top, each CAPTURED_BYTES_PER_PIXEL times its width.
struct CapturedFrame
{
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
  std::vector<std::byte> pixels;
};

// Writes _frame to _file as a PNG image through WIC: its red, green and blue at eight bits each, still encoded as sRGB, and
// not its alpha. A file already there is replaced. Throws std::invalid_argument for an empty frame or pixels of another
// count than its size, and winrt::hresult_error when WIC fails, as it does for a folder that does not exist.
void WritePng(const CapturedFrame& _frame, const std::filesystem::path& _file);

} // namespace NeuronClient
