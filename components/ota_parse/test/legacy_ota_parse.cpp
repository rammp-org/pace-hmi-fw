// Verbatim copies of the parsers in main/github_ota.cpp and main/fw_info.cpp at dev_refactor
// 54347bb (line ranges on each function). Only what surrounds them is stubbed:
//   - cjson_in_psram() (github_ota.cpp:146-156) only routes cJSON's malloc to PSRAM: a no-op;
//   - the espp logger formats its arguments as the firmware's would (INFO is on), then drops
//     the text;
//   - esp_image_header_t / esp_image_segment_header_t / esp_app_desc_t are copied from ESP-IDF
//     v6.0 (bootloader_support/include/esp_app_format.h:81-120,
//     esp_app_format/include/esp_app_desc.h:26-41), sizes asserted;
//   - esp_app_get_description() reports this project's name (CMakeLists.txt:55);
//   - storage_path() points into a folder the test chooses.
// Do not edit the copied bodies: they are the oracle (CORE never-list).

#include "legacy_ota_parse.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

#include "cJSON.h"
#include "format.hpp"

// ---- stubs ------------------------------------------------------------------------------------

#define CONFIG_IDF_FIRMWARE_CHIP_ID 0x0012
#define ESP_IMAGE_HEADER_MAGIC 0xE9
#define ESP_APP_DESC_MAGIC_WORD (0xABCD5432)

struct __attribute__((packed)) esp_image_header_t {
  uint8_t magic;
  uint8_t segment_count;
  uint8_t spi_mode;
  uint8_t spi_speed : 4;
  uint8_t spi_size : 4;
  uint32_t entry_addr;
  uint8_t wp_pin;
  uint8_t spi_pin_drv[3];
  uint16_t chip_id; // esp_chip_id_t: a packed enum, 2 bytes (esp_app_format.h:33)
  uint8_t min_chip_rev;
  uint16_t min_chip_rev_full;
  uint16_t max_chip_rev_full;
  uint8_t reserved[4];
  uint8_t hash_appended;
};
static_assert(sizeof(esp_image_header_t) == 24);

struct esp_image_segment_header_t {
  uint32_t load_addr;
  uint32_t data_len;
};
static_assert(sizeof(esp_image_segment_header_t) == 8);

struct esp_app_desc_t {
  uint32_t magic_word;
  uint32_t secure_version;
  uint32_t reserv1[2];
  char version[32];
  char project_name[32];
  char time[16];
  char date[16];
  char idf_ver[32];
  uint8_t app_elf_sha256[32];
  uint16_t min_efuse_blk_rev_full;
  uint16_t max_efuse_blk_rev_full;
  uint8_t mmu_page_size;
  uint8_t reserv3[3];
  uint32_t reserv2[18];
};
static_assert(sizeof(esp_app_desc_t) == 256);

// github_ota.cpp:32-33
extern "C" const char kHmiConfirmsItsBoot[];
const char kHmiConfirmsItsBoot[] = "RAMMP-HMI:confirms-its-boot:v1";

namespace {

const esp_app_desc_t *esp_app_get_description() {
  static const esp_app_desc_t desc = [] {
    esp_app_desc_t d{};
    d.magic_word = ESP_APP_DESC_MAGIC_WORD;
    std::memcpy(d.project_name, legacy::kRunningProject, sizeof(legacy::kRunningProject));
    return d;
  }();
  return &desc;
}

struct Logger {
  template <typename... Args> void info(fmt::format_string<Args...> f, Args &&...args) {
    last = fmt::format(f, std::forward<Args>(args)...);
  }
  std::string last;
};
Logger logger;

std::string g_storage_dir = "."; // NOLINT: test stub state

std::string storage_path(std::string_view name) { return g_storage_dir + "/" + std::string(name); }

void cjson_in_psram() {}

constexpr char kAssetName[] = "rammp-hmi-p4.bin"; // github_ota.cpp:41
constexpr size_t kNotesMaxChars = 1800;           // github_ota.cpp:55
constexpr char kReleasesFile[] = "fwinfo.txt";    // fw_info.cpp:29
constexpr size_t kDescEnd =                       // github_ota.cpp:302-303
    sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

// ---- verbatim: main/github_ota.cpp:158-161 ----------------------------------------------------

std::string json_string(const cJSON *obj, const char *key) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

} // namespace

namespace legacy {

// ---- verbatim: main/github_ota.cpp:165-208 ----------------------------------------------------

std::string readable_notes(std::string_view body) {
  for (std::string_view cut : {"### ESP-IDF Size Report", "<!--"}) {
    if (const size_t at = body.find(cut); at != std::string_view::npos) {
      body = body.substr(0, at);
    }
  }
  std::string out;
  out.reserve(std::min(body.size(), kNotesMaxChars) + 8);
  for (size_t i = 0; i < body.size() && out.size() < kNotesMaxChars; i++) {
    const auto c = static_cast<unsigned char>(body[i]);
    if (c == '\r' || c == '`' || (c == '*' && i + 1 < body.size() && body[i + 1] == '*')) {
      i += c == '*' ? 1 : 0; // "**" is bold: drop both
      continue;
    }
    if (c < 0x80) {
      if (c == '\n' && out.size() >= 2 && out.back() == '\n' && out[out.size() - 2] == '\n') {
        continue; // at most one blank line
      }
      out += static_cast<char>(c);
      continue;
    }
    // UTF-8: a few punctuation marks have ASCII stand-ins, the rest is dropped.
    const std::string_view rest = body.substr(i);
    struct Swap {
      std::string_view utf8, ascii;
    };
    static constexpr Swap kSwaps[] = {{"—", "-"},  {"–", "-"},  {"‘", "'"},  {"’", "'"},
                                      {"“", "\""}, {"”", "\""}, {"→", "->"}, {"…", "..."}};
    const auto swap = std::find_if(std::begin(kSwaps), std::end(kSwaps),
                                   [&](const Swap &s) { return rest.starts_with(s.utf8); });
    if (swap != std::end(kSwaps)) {
      out += swap->ascii;
    }
    size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    i += len - 1;
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
    out.pop_back();
  }
  if (out.size() >= kNotesMaxChars) {
    out += "...";
  }
  return out;
}

// ---- verbatim: main/github_ota.cpp:210-247 ----------------------------------------------------

std::vector<GithubRelease> parse_releases(const std::string &json, std::string &error) {
  cjson_in_psram();
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_ParseWithLength(json.data(), json.size()), cJSON_Delete);
  if (!cJSON_IsArray(root.get())) {
    error = "GitHub's answer was not a release list";
    return {};
  }
  std::vector<GithubRelease> releases;
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, root.get()) {
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "draft"))) {
      continue;
    }
    GithubRelease r;
    r.tag = json_string(item, "tag_name");
    r.prerelease = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "prerelease"));
    r.published = json_string(item, "published_at").substr(0, 10);
    r.notes = readable_notes(json_string(item, "body"));
    const cJSON *asset = nullptr;
    cJSON_ArrayForEach(asset, cJSON_GetObjectItemCaseSensitive(item, "assets")) {
      if (json_string(asset, "name") != kAssetName) {
        continue;
      }
      r.url = json_string(asset, "browser_download_url");
      const cJSON *size = cJSON_GetObjectItemCaseSensitive(asset, "size");
      r.size = cJSON_IsNumber(size) ? static_cast<size_t>(size->valuedouble) : 0;
      const std::string digest = json_string(asset, "digest"); // "sha256:<hex>"
      if (digest.starts_with("sha256:") && digest.size() == 7 + 64) {
        r.sha256 = digest.substr(7);
      }
    }
    if (!r.tag.empty()) {
      releases.push_back(std::move(r));
    }
  }
  return releases;
}

// ---- verbatim: main/github_ota.cpp:305-323 ----------------------------------------------------

std::string check_header(const uint8_t *data) {
  esp_image_header_t header;
  esp_app_desc_t desc;
  std::memcpy(&header, data, sizeof(header));
  std::memcpy(&desc, data + sizeof(header) + sizeof(esp_image_segment_header_t), sizeof(desc));
  if (header.magic != ESP_IMAGE_HEADER_MAGIC || desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
    return "The file is not firmware";
  }
  if (header.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
    return "The file is firmware for another chip";
  }
  const esp_app_desc_t *running = esp_app_get_description();
  if (std::strncmp(desc.project_name, running->project_name, sizeof(desc.project_name)) != 0) {
    return fmt::format("The file is '{:.32s}', not this HMI's firmware", desc.project_name);
  }
  logger.info("Image: {} {:.32s}, built {:.16s} {:.16s}", desc.project_name, desc.version,
              desc.date, desc.time);
  return "";
}

// ---- verbatim: main/github_ota.cpp:388-396 (install()'s write_block, before esp_ota_write) ----

std::string first_block_check(const uint8_t *data, size_t fill) {
  bool header_checked = false;
  if (!header_checked) {
    if (fill < kDescEnd) {
      return "The file is too short to be firmware";
    }
    if (std::string err = check_header(data); !err.empty()) {
      return err;
    }
    header_checked = true;
  }
  return "";
}

// ---- verbatim: main/fw_info.cpp:94-112 --------------------------------------------------------

std::optional<FwRelease> find_release(const std::string &sha256) {
  std::ifstream in(storage_path(kReleasesFile));
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream fields(line);
    std::string hash;
    std::string tag;
    std::string kind;
    if (fields >> hash >> tag >> kind && hash == sha256) {
      std::string checked;
      fields >> checked;
      return FwRelease{tag, kind == "prerelease", checked};
    }
  }
  return std::nullopt;
}

// ---- verbatim: main/github_ota.cpp:369-371, 405-434 (the marker part of the read loop) -------

bool has_marker_after(const std::vector<std::string> &chunks) {
  const std::string_view marker(kHmiConfirmsItsBoot);
  bool has_marker = false;
  std::string carry; // the tail of the last chunk, for a marker across two
  for (const std::string &chunk : chunks) {
    const uint8_t *data = reinterpret_cast<const uint8_t *>(chunk.data());
    const size_t len = chunk.size();
    if (len == 0) {
      break; // esp_http_client_read() == 0 ends the loop (github_ota.cpp:413)
    }
    if (!has_marker) {
      carry.append(reinterpret_cast<const char *>(data), len);
      has_marker = carry.find(marker) != std::string::npos;
      carry.erase(0, carry.size() - std::min(carry.size(), marker.size() - 1));
    }
  }
  return has_marker;
}

void set_storage_dir(const std::string &dir) { g_storage_dir = dir; }

} // namespace legacy
