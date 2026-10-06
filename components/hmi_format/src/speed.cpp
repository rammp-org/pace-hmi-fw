// Moved from main/frag_drive_band.inc (speed_display_tenths :56, the text of
// speed_label_observer :66) without a change in output (REQ-FMT-01).

#include "hmi_format/speed.hpp"

#include <algorithm>
#include <cmath>

#include "text_writer.hpp"

namespace hmi::format {

int32_t speed_display_tenths(float mps) noexcept {
  if (!std::isfinite(mps) || mps <= 0.0f) {
    return 0;
  }
  const auto tenths = std::lround(mps * MPH_PER_MPS * 10.0f);
  return static_cast<int32_t>(std::clamp<long>(tenths, 0, SPEED_MAX_TENTHS));
}

void speed_text(int32_t tenths, std::span<char> out) noexcept {
  const int32_t shown = std::clamp<int32_t>(tenths, 0, SPEED_MAX_TENTHS);
  detail::TextWriter text(out);
  text.put_int(shown / 10);
  text.put_char('.');
  text.put_int(shown % 10);
}

} // namespace hmi::format
