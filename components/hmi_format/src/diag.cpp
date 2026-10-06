// Moved from main/frag_diag.inc (the live text of diag_freq_observer :123) without a change in
// output (REQ-FMT-08).

#include "hmi_format/diag.hpp"

#include "text_writer.hpp"

namespace hmi::format {

void diag_rate_text(int32_t rate_tenths_hz, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  text.put_int(rate_tenths_hz / 10);
  text.put_char('.');
  text.put_int(rate_tenths_hz % 10);
  text.put_text(" Hz - Live");
}

} // namespace hmi::format
