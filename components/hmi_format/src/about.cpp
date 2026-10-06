// Moved from main/about_ui.cpp (is_hex :30, names_a_tag :38, show_sha's line text :109-112, the
// MAC text of fill_static :154-155) without a change in output (REQ-FMT-10, REQ-FMT-13).

#include "hmi_format/about.hpp"

#include "text_writer.hpp"

namespace hmi::format {

namespace {

constexpr std::size_t SHA_GROUP_CHARS = 8;
constexpr std::size_t SHA_LINE_CHARS = 32;

bool is_hex(std::string_view s) noexcept {
  return !s.empty() && s.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

// fmt's "{:02X}": two upper-case hex digits.
void put_hex_byte(detail::TextWriter &text, uint8_t byte) noexcept {
  constexpr std::string_view DIGITS = "0123456789ABCDEF";
  const auto value = static_cast<std::size_t>(byte);
  text.put_char(DIGITS[value >> 4U]);
  text.put_char(DIGITS[value & 0x0FU]);
}

} // namespace

bool names_a_tag(std::string_view version) noexcept {
  if (version.empty() || version[0] != 'v' || version.find("-dirty") != std::string_view::npos) {
    return false;
  }
  const std::size_t g = version.rfind("-g");
  return g == std::string_view::npos || !is_hex(version.substr(g + 2));
}

void sha_line_text(std::string_view hex, std::size_t from, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  for (std::size_t i = 0; i < SHA_LINE_CHARS; i += SHA_GROUP_CHARS) {
    if (i != 0) {
      text.put_char(' ');
    }
    const std::size_t at = from + i;
    if (at <= hex.size()) { // past the end is outside the precondition: the group reads empty
      text.put_text(hex.substr(at, SHA_GROUP_CHARS));
    }
  }
}

void mac_text(std::span<const uint8_t, 6> mac, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  for (std::size_t i = 0; i < mac.size(); ++i) {
    if (i != 0) {
      text.put_char(':');
    }
    put_hex_byte(text, mac[i]);
  }
}

} // namespace hmi::format
