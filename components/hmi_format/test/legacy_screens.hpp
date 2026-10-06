#pragma once
// Characterisation oracle for the About, Firmware update and Internet screens' text helpers:
// verbatim copies of them as main/ has them at dev_refactor 54347bb, before they move to
// components/hmi_format. legacy_screens.cpp formats with fmt (espp's format.hpp, the one the
// firmware links) and parses with the C library's sscanf, so goldens_screens.hpp records what
// the firmware drew. Test-only code: nothing in the firmware links it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace legacy {

// main/about_ui.cpp:38-44 (with is_hex, :30-32)
bool names_a_tag(std::string_view version);
// main/about_ui.cpp:109-112 (show_sha: one line's text, before lv_label_set_text)
std::string sha_line(const std::string &hex, size_t from);
// main/about_ui.cpp:154-155 (fill_static: the device MAC's text)
std::string mac_text(const std::array<uint8_t, 6> &mac);

// main/update_ui.cpp:46-56
std::string day(const std::string &iso);
// main/update_ui.cpp:64
std::string megabytes(size_t bytes);
// main/update_ui.cpp:156 (list_status: the text for a list that is not empty)
std::string release_count_text(size_t count);
// main/update_ui.cpp:268 (run_refresh: the percentage)
int32_t progress_pct(size_t done, size_t total);
// main/update_ui.cpp:271-272 (run_refresh: the progress label while total > 0)
std::string progress_text(size_t done, size_t total, int32_t pct);

// main/internet_ui.cpp:123 (main_refresh: the signal value)
std::string signal_text(std::optional<int> rssi);
// main/internet_ui.cpp:277 (networks_fill: a network row's signal text)
std::string net_row_signal_text(bool secured, int rssi);

} // namespace legacy
