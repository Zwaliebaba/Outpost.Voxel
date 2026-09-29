#include "pch.h"

#include "WelcomeNames.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace GameCore
{
namespace
{

constexpr std::size_t MAX_NAME_BYTES = 255;

[[nodiscard]] bool IsName(std::span<const std::uint8_t> _name) noexcept
{
  return !_name.empty() && std::ranges::all_of(_name,
                                               [](std::uint8_t _char) {
                                                 return (_char >= '0' && _char <= '9') || (_char >= 'A' && _char <= 'Z') ||
                                                        (_char >= 'a' && _char <= 'z');
                                               });
}

void PutCount(std::vector<std::uint8_t>& _bytes, std::size_t _count)
{
  for (std::size_t i = 0; i < 4; ++i)
  {
    _bytes.push_back(static_cast<std::uint8_t>(_count >> (8 * i)));
  }
}

void PutNames(std::vector<std::uint8_t>& _bytes, const std::vector<std::string>& _names)
{
  PutCount(_bytes, _names.size());
  for (const std::string& name : _names)
  {
    if (name.size() > MAX_NAME_BYTES)
    {
      throw std::invalid_argument("A name of " + std::to_string(name.size()) + " bytes is longer than a welcome's names can carry.");
    }
    _bytes.push_back(static_cast<std::uint8_t>(name.size()));
    _bytes.insert(_bytes.end(), name.begin(), name.end());
  }
}

// Reads a count and that many names from _bytes at _offset, moving _offset past them.
[[nodiscard]] std::expected<std::vector<std::string>, NamesError> TakeNames(std::span<const std::uint8_t> _bytes, std::size_t& _offset)
{
  if (_bytes.size() - _offset < 4)
  {
    return std::unexpected(NamesError::Truncated);
  }
  std::uint32_t count = 0;
  for (std::size_t i = 0; i < 4; ++i)
  {
    count |= std::uint32_t{_bytes[_offset + i]} << (8 * i);
  }
  _offset += 4;
  // Each name takes at least its length byte and one letter, so a count beyond that is refused before anything grows.
  if (count > (_bytes.size() - _offset) / 2)
  {
    return std::unexpected(NamesError::Truncated);
  }
  std::vector<std::string> names;
  names.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i)
  {
    if (_offset == _bytes.size())
    {
      return std::unexpected(NamesError::Truncated);
    }
    const std::size_t length = _bytes[_offset++];
    if (_bytes.size() - _offset < length)
    {
      return std::unexpected(NamesError::Truncated);
    }
    const std::span<const std::uint8_t> name = _bytes.subspan(_offset, length);
    if (!IsName(name))
    {
      return std::unexpected(NamesError::BadName);
    }
    names.emplace_back(name.begin(), name.end());
    _offset += length;
  }
  return names;
}

} // namespace

const char* NamesErrorName(NamesError _error) noexcept
{
  switch (_error)
  {
  case NamesError::Truncated:
    return "Truncated";
  case NamesError::UnsupportedVersion:
    return "UnsupportedVersion";
  case NamesError::BadName:
    return "BadName";
  case NamesError::TrailingBytes:
    return "TrailingBytes";
  }
  return "Unknown";
}

std::vector<std::uint8_t> EncodeWelcomeNames(const WelcomeNames& _names)
{
  std::vector<std::uint8_t> bytes{WELCOME_NAMES_VERSION};
  PutNames(bytes, _names.composites);
  PutNames(bytes, _names.sides);
  return bytes;
}

std::expected<WelcomeNames, NamesError> DecodeWelcomeNames(std::span<const std::uint8_t> _bytes)
{
  if (_bytes.empty())
  {
    return WelcomeNames{};
  }
  if (_bytes.front() != WELCOME_NAMES_VERSION)
  {
    return std::unexpected(NamesError::UnsupportedVersion);
  }
  std::size_t offset = 1;
  auto composites = TakeNames(_bytes, offset);
  if (!composites)
  {
    return std::unexpected(composites.error());
  }
  auto sides = TakeNames(_bytes, offset);
  if (!sides)
  {
    return std::unexpected(sides.error());
  }
  if (offset != _bytes.size())
  {
    return std::unexpected(NamesError::TrailingBytes);
  }
  return WelcomeNames{std::move(*composites), std::move(*sides)};
}

} // namespace GameCore
