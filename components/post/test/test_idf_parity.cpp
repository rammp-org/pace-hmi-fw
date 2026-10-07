// L1: the enums in post/facts.hpp mirror ESP-IDF's, value for value (TS-UNIT-04: a copy of a
// table has a test that it matches). The component cannot include the IDF headers, so this
// reads them as text from the IDF the test is built with (POST_IDF_ROOT, from the Makefile:
// the tree UNITY_DIR lives in).

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "post/facts.hpp"
#include "test_case.hpp"

namespace {

std::string read_idf(const char *relative) {
  const std::string path = std::string(POST_IDF_ROOT) + "/" + relative;
  std::ifstream in(path);
  std::ostringstream text;
  text << in.rdbuf();
  if (!in.good() && text.str().empty()) {
    std::printf("cannot read %s\n", path.c_str());
  }
  return text.str();
}

// The body of `typedef enum { ... } <name>;`, or "" when the header has no such enum.
std::string enum_body(const std::string &text, const std::string &name) {
  const std::size_t end = text.find("} " + name + ";");
  if (end == std::string::npos) {
    return {};
  }
  const std::size_t open = text.rfind('{', end);
  return open == std::string::npos ? std::string{} : text.substr(open + 1, end - open - 1);
}

// Enumerators with the prefix, in order, with their values: explicit (`= 0x2U`) or implicit
// (previous + 1, from 0). Comments are dropped first.
std::vector<std::pair<std::string, uint64_t>> enumerators(std::string body,
                                                          const std::string &prefix) {
  body = std::regex_replace(body, std::regex(R"(/\*[\s\S]*?\*/|//[^\n]*)"), "");
  const std::regex item("(" + prefix +
                        R"re([A-Z0-9_]+)\s*(?:=\s*(0[xX][0-9a-fA-F]+|\d+)[uU]?)?)re");
  std::vector<std::pair<std::string, uint64_t>> out;
  uint64_t next = 0;
  for (auto it = std::sregex_iterator(body.begin(), body.end(), item); it != std::sregex_iterator();
       ++it) {
    const std::smatch &m = *it;
    const uint64_t value = m[2].matched ? std::stoull(m[2].str(), nullptr, 0) : next;
    out.emplace_back(m[1].str(), value);
    next = value + 1;
  }
  return out;
}

} // namespace

TEST_CASE("POST-030 ResetReason mirrors esp_reset_reason_t in the IDF the test is built with",
          "[post][idf]") {
  using hmi::post::ResetReason;
  const auto idf = enumerators(
      enum_body(read_idf("components/esp_system/include/esp_system.h"), "esp_reset_reason_t"),
      "ESP_RST_");
  const std::vector<std::pair<std::string, ResetReason>> ours{
      {"ESP_RST_UNKNOWN", ResetReason::UNKNOWN},
      {"ESP_RST_POWERON", ResetReason::POWERON},
      {"ESP_RST_EXT", ResetReason::EXT},
      {"ESP_RST_SW", ResetReason::SW},
      {"ESP_RST_PANIC", ResetReason::PANIC},
      {"ESP_RST_INT_WDT", ResetReason::INT_WDT},
      {"ESP_RST_TASK_WDT", ResetReason::TASK_WDT},
      {"ESP_RST_WDT", ResetReason::WDT},
      {"ESP_RST_DEEPSLEEP", ResetReason::DEEPSLEEP},
      {"ESP_RST_BROWNOUT", ResetReason::BROWNOUT},
      {"ESP_RST_SDIO", ResetReason::SDIO},
      {"ESP_RST_USB", ResetReason::USB},
      {"ESP_RST_JTAG", ResetReason::JTAG},
      {"ESP_RST_EFUSE", ResetReason::EFUSE},
      {"ESP_RST_PWR_GLITCH", ResetReason::PWR_GLITCH},
      {"ESP_RST_CPU_LOCKUP", ResetReason::CPU_LOCKUP},
  };
  TEST_ASSERT_EQUAL_size_t_MESSAGE(ours.size(), idf.size(),
                                   "esp_reset_reason_t gained or lost a reason: update ResetReason "
                                   "and its classification (REQ-POST-05)");
  for (std::size_t i = 0; i < ours.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(ours[i].first.c_str(), idf[i].first.c_str());
    TEST_ASSERT_EQUAL_UINT64(idf[i].second, static_cast<uint64_t>(ours[i].second));
  }
}

TEST_CASE("POST-031 OtaImageState mirrors esp_ota_img_states_t in the IDF the test is built with",
          "[post][idf]") {
  using hmi::post::OtaImageState;
  const auto idf = enumerators(
      enum_body(read_idf("components/bootloader_support/include/esp_flash_partitions.h"),
                "esp_ota_img_states_t"),
      "ESP_OTA_IMG_");
  const std::vector<std::pair<std::string, OtaImageState>> ours{
      {"ESP_OTA_IMG_NEW", OtaImageState::NEW},
      {"ESP_OTA_IMG_PENDING_VERIFY", OtaImageState::PENDING_VERIFY},
      {"ESP_OTA_IMG_VALID", OtaImageState::VALID},
      {"ESP_OTA_IMG_INVALID", OtaImageState::INVALID},
      {"ESP_OTA_IMG_ABORTED", OtaImageState::ABORTED},
      {"ESP_OTA_IMG_UNDEFINED", OtaImageState::UNDEFINED},
  };
  TEST_ASSERT_EQUAL_size_t(ours.size(), idf.size());
  for (std::size_t i = 0; i < ours.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(ours[i].first.c_str(), idf[i].first.c_str());
    TEST_ASSERT_EQUAL_UINT64(idf[i].second, static_cast<uint64_t>(ours[i].second));
  }
}
