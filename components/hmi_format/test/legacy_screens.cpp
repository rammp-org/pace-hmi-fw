// Verbatim copies of the About, Firmware update and Internet screens' text helpers (see
// legacy_screens.hpp). Each body is the source named above it, character for character; where
// the original is an expression inside a label call, the copy returns that expression and a
// comment quotes the call. fmt is espp's header-only fmt 12.1 (format.hpp, as main/ includes it).

#include "legacy_screens.hpp"

#include <cstdio>

#include "format.hpp"

namespace legacy {

namespace {

// main/about_ui.cpp:30-32
bool is_hex(std::string_view s) {
  return !s.empty() && s.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

// main/update_ui.cpp:46-47
const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

} // namespace

// main/about_ui.cpp:38-44
bool names_a_tag(std::string_view version) {
  if (version.empty() || version[0] != 'v' || version.find("-dirty") != std::string_view::npos) {
    return false;
  }
  const size_t g = version.rfind("-g");
  return g == std::string_view::npos || !is_hex(version.substr(g + 2));
}

// main/about_ui.cpp:109-112; the loop body of show_sha for one label
std::string sha_line(const std::string &hex, size_t from) {
  std::string line;
  for (size_t i = 0; i < 32; i += 8) {
    line += (i ? " " : "") + hex.substr(from + i, 8);
  }
  return line;
}

// main/about_ui.cpp:154-155; lv_label_set_text(ui_AboutDeviceValue, fmt::format(...).c_str())
std::string mac_text(const std::array<uint8_t, 6> &mac) {
  return fmt::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", mac[0], mac[1], mac[2], mac[3],
                     mac[4], mac[5]);
}

// main/update_ui.cpp:49-56
// "2026-09-30" -> "30 Sep 2026"
std::string day(const std::string &iso) {
  int y = 0, m = 0, d = 0;
  if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) {
    return iso;
  }
  return fmt::format("{} {} {}", d, kMonths[m - 1], y);
}

// main/update_ui.cpp:64. main/ is not built with -Wconversion; on the host size_t is 64 bits,
// so its implicit size_t -> double warns here (it is exact below 2^53 bytes, and on the target's
// 32-bit size_t always). Silenced for this one verbatim line only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
std::string megabytes(size_t bytes) { return fmt::format("{:.1f} MB", bytes / 1e6); }
#pragma GCC diagnostic pop

// main/update_ui.cpp:155-156; lv_label_set_text(ui_UpdateStatus, fmt::format(...).c_str())
std::string release_count_text(size_t count) {
  return fmt::format("{} releases. Pick one to install it.", count);
}

// main/update_ui.cpp:268; `s` is the OtaStatus, whose done and total are size_t
int32_t progress_pct(size_t done, size_t total) {
  const struct {
    size_t done;
    size_t total;
  } s{done, total};
  const int32_t pct = s.total > 0 ? static_cast<int32_t>(s.done * 100 / s.total) : 0;
  return pct;
}

// main/update_ui.cpp:271-272; the text while s.total > 0, with `done ? 100 : pct` as pct
std::string progress_text(size_t done, size_t total, int32_t pct) {
  return fmt::format("{} of {}  ({}%)", megabytes(done), megabytes(total), pct);
}

// main/internet_ui.cpp:123; lv_label_set_text(ui_NetSignalValue, <this>.c_str() or "--")
std::string signal_text(std::optional<int> rssi) {
  return rssi ? fmt::format("{} dBm", *rssi).c_str() : "--";
}

// main/internet_ui.cpp:277; lv_label_set_text(<the row's signal label>, fmt::format(...).c_str())
std::string net_row_signal_text(bool secured, int rssi) {
  return fmt::format("{}{} dBm", secured ? "" : "open  ", rssi);
}

} // namespace legacy
