#pragma once
// The ABOUT screen's texts that need no device call: whether the version names a release tag,
// the SHA-256 digest's two lines and the device MAC. Plain C++, no LVGL (CS-UI-03).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hmi::format {

/// A SHA-256 line's buffer: four groups of 8 characters and three spaces, plus the terminator.
inline constexpr std::size_t SHA_LINE_TEXT_SIZE = 36;
/// The MAC text's buffer: "XX:XX:XX:XX:XX:XX" plus the terminator.
inline constexpr std::size_t MAC_TEXT_SIZE = 18;

/// @brief Whether a firmware version (`git describe --tags --dirty` at build time) names a
/// release tag: "v4.0.0-alpha" does; "v4.0.0-alpha-3-ga4c1d47" (commits after the tag), anything
/// with "-dirty", and a bare hash do not.
/// @param version the version string
/// @return true when it starts with 'v', has no "-dirty", and what follows its last "-g" is not
/// lower-case hex (an empty rest is not hex)
[[nodiscard]] bool names_a_tag(std::string_view version) noexcept;

/// @brief One line of the digest as the ABOUT screen shows it: the four groups of 8 characters
/// from `from`, joined by one space ("01234567 89abcdef 10325476 98badcfe").
/// @param hex the digest in hex, 64 characters
/// @param from where the line starts: 0 for the first line, 32 for the second
/// @param out the buffer, SHA_LINE_TEXT_SIZE fits the line; always terminated (unless empty)
/// @pre from + 24 <= hex.size() (a group that starts past the end reads empty; the code before
/// the move threw there, and the digest is always 64 characters)
void sha_line_text(std::string_view hex, std::size_t from, std::span<char> out) noexcept;

/// @brief The device MAC, six upper-case hex pairs joined by ':' ("30:ED:A0:1B:2C:3D").
/// @param mac the six bytes, first byte first
/// @param out the buffer, MAC_TEXT_SIZE fits the text; always terminated (unless empty)
void mac_text(std::span<const uint8_t, 6> mac, std::span<char> out) noexcept;

} // namespace hmi::format
