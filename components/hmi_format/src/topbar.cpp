// Moved from main/frag_clock.inc (clock_plausible :8, the text of clock_poll_cb :46-51,
// link_text :65) without a change in output (REQ-FMT-06, REQ-FMT-07).

#include "hmi_format/topbar.hpp"

#include "text_writer.hpp"

namespace hmi::format {

namespace {

// printf's "%02d": at least two characters, a 0 in front of a single digit.
void put_two_digits(detail::TextWriter &text, int value) noexcept {
  if (value >= 0 && value <= 9) {
    text.put_char('0');
  }
  text.put_int(static_cast<int32_t>(value));
}

} // namespace

bool clock_plausible(const std::tm &t) noexcept { return t.tm_year >= CLOCK_MIN_TM_YEAR; }

void clock_text(const std::tm &t, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  put_two_digits(text, t.tm_hour);
  text.put_char(':');
  put_two_digits(text, t.tm_min);
}

const char *link_text(Link link) noexcept {
  switch (link) {
  case Link::WIFI:
    return "BT · WI-FI";
  case Link::ETHERNET:
    return "BT · ETH";
  }
  return "BT · ETH"; // not an enumerator: as before the move, anything but WiFi reads Ethernet
}

} // namespace hmi::format
