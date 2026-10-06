#pragma once
// The About, Firmware update and Internet text helpers under test, behind one seam so the golden
// cases (FMT-101..110) never change. Before the move this seam calls the verbatim pre-move
// copies in legacy_screens.cpp; the move points it at the hmi_format component.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "legacy_screens.hpp"

namespace sut {

inline bool names_a_tag(const char *version) { return legacy::names_a_tag(version); }

inline std::string sha_line(const std::string &hex, std::size_t from) {
  return legacy::sha_line(hex, from);
}

inline std::string mac_text(const std::array<uint8_t, 6> &mac) { return legacy::mac_text(mac); }

inline std::string day(const std::string &iso) { return legacy::day(iso); }

inline std::string megabytes(std::size_t bytes) { return legacy::megabytes(bytes); }

inline std::string release_count_text(std::size_t count) {
  return legacy::release_count_text(count);
}

inline int32_t progress_pct(std::size_t done, std::size_t total) {
  return legacy::progress_pct(done, total);
}

inline std::string progress_text(std::size_t done, std::size_t total, int32_t pct) {
  return legacy::progress_text(done, total, pct);
}

inline std::string signal_text(std::optional<int> rssi_dbm) {
  return legacy::signal_text(rssi_dbm);
}

inline std::string net_row_signal_text(bool secured, int rssi_dbm) {
  return legacy::net_row_signal_text(secured, rssi_dbm);
}

} // namespace sut
