#pragma once
// The About, Firmware update and Internet text helpers under test, behind one seam so the golden
// cases (FMT-101..110) never change: the hmi_format component, called the way main/ calls it
// (a buffer of the header's size, then the text; day() shows the input itself when it is not a
// date). (Before the move this seam called the verbatim pre-move copies in legacy_screens.cpp,
// which stay as the oracle of the differential cases.)

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "hmi_format/about.hpp"
#include "hmi_format/net.hpp"
#include "hmi_format/update.hpp"

namespace sut {

inline bool names_a_tag(const char *version) { return hmi::format::names_a_tag(version); }

inline std::string sha_line(const std::string &hex, std::size_t from) {
  std::array<char, hmi::format::SHA_LINE_TEXT_SIZE> line{};
  hmi::format::sha_line_text(hex, from, line);
  return line.data();
}

inline std::string mac_text(const std::array<uint8_t, 6> &mac) {
  std::array<char, hmi::format::MAC_TEXT_SIZE> text{};
  hmi::format::mac_text(mac, text);
  return text.data();
}

inline std::string day(const std::string &iso) {
  std::array<char, hmi::format::DAY_TEXT_SIZE> text{};
  return hmi::format::day_text(iso, text) ? std::string(text.data()) : iso;
}

inline std::string megabytes(std::size_t bytes) {
  std::array<char, hmi::format::MEGABYTES_TEXT_SIZE> text{};
  hmi::format::megabytes_text(bytes, text);
  return text.data();
}

inline std::string release_count_text(std::size_t count) {
  std::array<char, hmi::format::RELEASE_COUNT_TEXT_SIZE> text{};
  hmi::format::release_count_text(count, text);
  return text.data();
}

inline int32_t progress_pct(std::size_t done, std::size_t total) {
  return hmi::format::progress_pct(done, total);
}

inline std::string progress_text(std::size_t done, std::size_t total, int32_t pct) {
  std::array<char, hmi::format::PROGRESS_TEXT_SIZE> text{};
  hmi::format::progress_text(done, total, pct, text);
  return text.data();
}

inline std::string signal_text(std::optional<int> rssi_dbm) {
  std::array<char, hmi::format::SIGNAL_TEXT_SIZE> text{};
  hmi::format::signal_text(rssi_dbm, text);
  return text.data();
}

inline std::string net_row_signal_text(bool secured, int rssi_dbm) {
  std::array<char, hmi::format::ROW_SIGNAL_TEXT_SIZE> text{};
  hmi::format::net_row_signal_text(secured, rssi_dbm, text);
  return text.data();
}

} // namespace sut
