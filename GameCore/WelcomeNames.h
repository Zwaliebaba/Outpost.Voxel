#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace GameCore
{

// The game's part of a skirmish's welcome (Design/ADR/ADR-029, Design/ADR/ADR-030): the name of each composite, a design's
// or an asteroid's, and of each side, which the client shows in its figures. The engine carries the bytes and never reads
// them. Little-endian: a u8 version, then a u32 composite count and each name, then a u32 side count and each name, a
// name being a u8 length and that many letters and digits.
struct WelcomeNames
{
  std::vector<std::string> composites;
  std::vector<std::string> sides;
};

inline constexpr std::uint8_t WELCOME_NAMES_VERSION = 1;

// Why a payload was refused.
enum class NamesError : std::uint8_t
{
  Truncated,          // fewer bytes than the counts and lengths say
  UnsupportedVersion, // another version
  BadName,            // an empty name, or one of anything but letters and digits
  TrailingBytes       // bytes after the last name
};

[[nodiscard]] const char* NamesErrorName(NamesError _error) noexcept;

// The bytes of _names. Throws std::invalid_argument for a name longer than 255 bytes.
[[nodiscard]] std::vector<std::uint8_t> EncodeWelcomeNames(const WelcomeNames& _names);

// The names _bytes hold. An empty payload, from a world that sends none, holds none, and is not refused.
[[nodiscard]] std::expected<WelcomeNames, NamesError> DecodeWelcomeNames(std::span<const std::uint8_t> _bytes);

} // namespace GameCore
