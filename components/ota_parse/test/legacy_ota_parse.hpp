#pragma once
// The OTA and firmware-info parsers as they were in main/ before they moved to
// components/ota_parse (dev_refactor 54347bb), copied verbatim into legacy_ota_parse.cpp
// with only the ESP-IDF calls around them stubbed (see that file). They are the oracle the
// goldens were recorded from, and the differential cases compare the component against.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fw_info.hpp"    // main/: FwRelease, unchanged
#include "github_ota.hpp" // main/: GithubRelease, unchanged

namespace legacy {

// main/github_ota.cpp:165-208
std::string readable_notes(std::string_view body);
// main/github_ota.cpp:210-247
std::vector<GithubRelease> parse_releases(const std::string &json, std::string &error);
// main/github_ota.cpp:300-323 (needs kDescEnd bytes: the caller, install() at :389, checks)
inline constexpr std::size_t kDescEnd = 24 + 8 + 256;
std::string check_header(const uint8_t *data);
// main/github_ota.cpp:386-396: install()'s write_block, the part before esp_ota_write
std::string first_block_check(const uint8_t *data, std::size_t fill);
// main/fw_info.cpp:93-112; reads the file storage_path("fwinfo.txt") names
std::optional<FwRelease> find_release(const std::string &sha256);
// main/github_ota.cpp:369-371 and :423-427: install()'s marker search over the chunks read
bool has_marker_after(const std::vector<std::string> &chunks);

// Test control: where the stubbed storage_path() puts fwinfo.txt, and the running image's
// project name the stubbed esp_app_get_description() reports.
void set_storage_dir(const std::string &dir);
inline constexpr char kRunningProject[] = "rammp-hmi-p4"; // CMakeLists.txt:55 project()
inline constexpr uint16_t kChipIdP4 = 0x0012;             // CONFIG_IDF_FIRMWARE_CHIP_ID, P4

} // namespace legacy
