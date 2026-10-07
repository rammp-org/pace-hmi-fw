// Moved from main/internet_ui.cpp (main_refresh's signal text :123, networks_fill's row signal
// text :277) without a change in output (REQ-FMT-12, REQ-FMT-13).

#include "hmi_format/net.hpp"

#include <cstdint>

#include "text_writer.hpp"

namespace hmi::format {

void signal_text(std::optional<int> rssi_dbm, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  if (!rssi_dbm) {
    text.put_text("--");
    return;
  }
  text.put_int(static_cast<int32_t>(*rssi_dbm));
  text.put_text(" dBm");
}

void net_row_signal_text(bool secured, int rssi_dbm, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  if (!secured) {
    text.put_text("open  ");
  }
  text.put_int(static_cast<int32_t>(rssi_dbm));
  text.put_text(" dBm");
}

} // namespace hmi::format
