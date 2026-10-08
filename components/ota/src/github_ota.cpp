#include "github_ota.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_flash_partitions.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_pthread.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "format.hpp"
#include "fw_info.hpp"
#include "logger.hpp"
#include "ota_parse/ota_parse.hpp"
#include "psa/crypto.h"

// In this image, and so in every image built from this code: an image that
// has it confirms its own boot (github_ota_boot_confirm), one without it
// predates that and is installed already confirmed. Referenced by the search
// below, and external so the compiler keeps it whole in .rodata.
extern "C" const char kHmiConfirmsItsBoot[];
const char kHmiConfirmsItsBoot[] = "RAMMP-HMI:confirms-its-boot:v1";

namespace {

espp::Logger logger({.tag = "github_ota", .level = espp::Logger::Verbosity::INFO});

constexpr char kReleasesUrl[] =
    "https://api.github.com/repos/rammp-org/pace-hmi-fw/releases?per_page=30";
constexpr char kAssetName[] = "rammp-hmi-p4.bin";
constexpr char kUserAgent[] = "pace-hmi-fw";
// One more image to offer before it is published (Kconfig HMI_OTA_TEST_URL). "" = none.
constexpr char kTestUrl[] = CONFIG_HMI_OTA_TEST_URL;
constexpr int kHttpTimeoutMs = 20000;
constexpr int kMaxRedirects = 5;
constexpr size_t kChunkBytes = 16 * 1024;
constexpr size_t kBlockBytes = 64 * 1024; // the flash's erase block
// TLS (certificate checks included) and the espp logger on top: the OTA
// thread's stack is internal RAM, since it writes flash.
constexpr size_t kInstallStackBytes = 12 * 1024;
// The OTA data partition holds two copies of the boot choice, a flash sector each.
constexpr uint32_t kOtaDataSector = 0x1000;

std::mutex status_mutex;
OtaStatus status; // under status_mutex

///////////////////////////////////////////////////////////////////////////////
// Progress

void set_stage(OtaStage stage, const std::string &message) {
  logger.info("{}", message);
  std::lock_guard<std::mutex> lock(status_mutex);
  status.stage = stage;
  status.message = message;
  status.log += message + "\n";
}

void set_done(size_t done) {
  std::lock_guard<std::mutex> lock(status_mutex);
  status.done = done;
}

///////////////////////////////////////////////////////////////////////////////
// HTTP

// A GET, opened, its redirects followed: GitHub answers a release asset with a
// 302 to its storage host. The caller reads the body and cleans up.
struct Http {
  esp_http_client_handle_t client = nullptr;
  int status = 0;
  int64_t length = -1;
  ~Http() {
    if (client != nullptr) {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
    }
  }
};

std::string open_get(Http &http, const char *url, bool api) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = kHttpTimeoutMs;
  config.user_agent = kUserAgent; // GitHub refuses requests without one
  config.buffer_size = 4096;
  config.buffer_size_tx = 2048; // the redirect's signed URL is ~900 characters
  config.crt_bundle_attach = esp_crt_bundle_attach;
  http.client = esp_http_client_init(&config);
  if (http.client == nullptr) {
    return "Could not start HTTPS (out of memory?)";
  }
  if (api) {
    esp_http_client_set_header(http.client, "Accept", "application/vnd.github+json");
    esp_http_client_set_header(http.client, "X-GitHub-Api-Version", "2022-11-28");
  }
  for (int hop = 0; hop <= kMaxRedirects; hop++) {
    if (esp_err_t err = esp_http_client_open(http.client, 0); err != ESP_OK) {
      logger.warn("Opening {}: {}", url, esp_err_to_name(err));
      return "No connection. Is the HMI online?";
    }
    http.length = esp_http_client_fetch_headers(http.client);
    http.status = esp_http_client_get_status_code(http.client);
    switch (http.status) {
    case 301:
    case 302:
    case 303:
    case 307:
    case 308:
      esp_http_client_flush_response(http.client, nullptr);
      // Changing host closes the connection; the next open makes a new one.
      esp_http_client_set_redirection(http.client);
      continue;
    case 200:
      return "";
    case 403:
    case 429:
      // 60 API requests an hour per address without a token.
      return "GitHub's limit is reached. Try in an hour.";
    default:
      return fmt::format("GitHub answered HTTP {}", http.status);
    }
  }
  return "Too many redirects";
}

///////////////////////////////////////////////////////////////////////////////
// The release list

// cJSON builds a node per value (~2,000 for the list), each a few dozen bytes:
// under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL, so malloc would put them all in
// internal RAM. The W5500 and SDIO drivers need that RAM, so put them in PSRAM.
void cjson_in_psram() {
  static std::once_flag once;
  std::call_once(once, [] {
    cJSON_Hooks hooks = {};
    hooks.malloc_fn = [](size_t size) {
      return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
    };
    hooks.free_fn = free;
    cJSON_InitHooks(&hooks);
  });
}

// The list's parsing is components/ota_parse; this puts its cJSON nodes in PSRAM and hands
// back the releases as the panel's type.
std::vector<GithubRelease> parse_releases(const std::string &json, std::string &error) {
  cjson_in_psram();
  std::vector<hmi::ota::Release> parsed = hmi::ota::parse_releases(json, kAssetName, error);
  std::vector<GithubRelease> releases;
  releases.reserve(parsed.size());
  std::transform(parsed.begin(), parsed.end(), std::back_inserter(releases),
                 [](hmi::ota::Release &r) {
                   return GithubRelease{.tag = std::move(r.tag),
                                        .prerelease = r.prerelease,
                                        .published = std::move(r.published),
                                        .notes = std::move(r.notes),
                                        .url = std::move(r.url),
                                        .size = r.size,
                                        .sha256 = std::move(r.sha256)};
                 });
  return releases;
}

///////////////////////////////////////////////////////////////////////////////
// Installing

// The OTA data sector entry the last esp_ota_set_boot_partition wrote, marked
// VALID: the bootloader then boots it without waiting for it to confirm. For
// images that predate github_ota_boot_confirm, which would otherwise be rolled
// back at their first reset. The CRC covers only ota_seq, so it still holds.
esp_err_t mark_next_boot_valid() {
  const esp_partition_t *otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (otadata == nullptr) {
    return ESP_ERR_NOT_FOUND;
  }
  std::array<esp_ota_select_entry_t, 2> entry{};
  std::optional<size_t> newest; // the sector with the higher valid ota_seq
  for (size_t i = 0; i < entry.size(); i++) {
    if (esp_partition_read(otadata, i * kOtaDataSector, &entry[i], sizeof(entry[i])) != ESP_OK) {
      return ESP_FAIL;
    }
    const bool valid =
        entry[i].ota_seq != UINT32_MAX &&
        entry[i].crc == esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<uint8_t *>(&entry[i].ota_seq),
                                         sizeof(entry[i].ota_seq));
    if (valid && (!newest || entry[i].ota_seq > entry[*newest].ota_seq)) {
      newest = i;
    }
  }
  if (!newest) {
    return ESP_ERR_INVALID_STATE;
  }
  entry[*newest].ota_state = ESP_OTA_IMG_VALID;
  // A reset between the erase and the write leaves this sector blank, and the
  // bootloader falls back to the other one: the image running now.
  esp_err_t err = esp_partition_erase_range(otadata, *newest * kOtaDataSector, kOtaDataSector);
  if (err == ESP_OK) {
    err = esp_partition_write(otadata, *newest * kOtaDataSector, &entry[*newest],
                              sizeof(entry[*newest]));
  }
  return err;
}

std::string hex(const uint8_t *bytes, size_t n) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < n; i++) {
    out += kHex[bytes[i] >> 4];
    out += kHex[bytes[i] & 0xF];
  }
  return out;
}

// components/ota_parse reads the image header, first segment header and app
// description from the bytes; these hold it to the layout ESP-IDF builds.
static_assert(sizeof(esp_image_header_t) == hmi::ota::kImageHeaderBytes);
static_assert(sizeof(esp_image_segment_header_t) == hmi::ota::kSegmentHeaderBytes);
static_assert(sizeof(esp_app_desc_t) == hmi::ota::kAppDescBytes);
static_assert(offsetof(esp_image_header_t, chip_id) == hmi::ota::kChipIdOffset);
static_assert(sizeof(esp_chip_id_t) == 2);
static_assert(ESP_IMAGE_HEADER_MAGIC == hmi::ota::kImageMagic);
static_assert(ESP_APP_DESC_MAGIC_WORD == hmi::ota::kAppDescMagic);
static_assert(offsetof(esp_app_desc_t, magic_word) == 0);
static_assert(offsetof(esp_app_desc_t, version) == hmi::ota::kDescVersion.offset);
static_assert(sizeof(esp_app_desc_t::version) == hmi::ota::kDescVersion.size);
static_assert(offsetof(esp_app_desc_t, project_name) == hmi::ota::kDescProjectName.offset);
static_assert(sizeof(esp_app_desc_t::project_name) == hmi::ota::kDescProjectName.size);
static_assert(offsetof(esp_app_desc_t, time) == hmi::ota::kDescTime.offset);
static_assert(sizeof(esp_app_desc_t::time) == hmi::ota::kDescTime.size);
static_assert(offsetof(esp_app_desc_t, date) == hmi::ota::kDescDate.offset);
static_assert(sizeof(esp_app_desc_t::date) == hmi::ota::kDescDate.size);
static_assert(CONFIG_IDF_FIRMWARE_CHIP_ID >= 0 && CONFIG_IDF_FIRMWARE_CHIP_ID <= UINT16_MAX);

// Whether the first block (`fill` bytes) is this project's image for this
// chip: "" if so, else why not. Before anything is written.
std::string check_first_block(const uint8_t *data, size_t fill) {
  const esp_app_desc_t *running = esp_app_get_description();
  const std::string_view running_project(
      running->project_name, strnlen(running->project_name, sizeof(running->project_name)));
  hmi::ota::AppDesc desc;
  std::string err = hmi::ota::check_first_block(std::span<const uint8_t>(data, fill),
                                                static_cast<uint16_t>(CONFIG_IDF_FIRMWARE_CHIP_ID),
                                                running_project, desc);
  if (err.empty()) {
    logger.info("Image: {} {}, built {} {}", desc.project_name, desc.version, desc.date, desc.time);
  }
  return err;
}

// What the download leaves for the checks after it.
struct Downloaded {
  size_t done = 0;                  // bytes received
  int64_t started_us = 0;           // when the transfer started
  std::array<uint8_t, 32> digest{}; // SHA-256 of what was received
  bool hashed = false;              // `digest` holds the whole SHA-256
};

// The transfer itself: reads into `block` and writes each full block (and the
// tail) to `ota`, hashing what arrives into `sha` and feeding it to `marker`;
// the first block is checked before anything is written. Returns why it
// stopped, or "" when all `total` bytes are written.
std::string receive(Http &http, esp_ota_handle_t ota, size_t total, uint8_t *block,
                    psa_hash_operation_t &sha, hmi::ota::MarkerSearch &marker, size_t &done) {
  size_t fill = 0; // bytes in `block`
  bool header_checked = false;
  // The first block holds the header: nothing is written before it is checked.
  auto write_block = [&]() -> std::string {
    if (!header_checked) {
      if (std::string err = check_first_block(block, fill); !err.empty()) {
        return err;
      }
      header_checked = true;
    }
    if (esp_err_t err = esp_ota_write(ota, block, fill); err != ESP_OK) {
      return fmt::format("Writing flash failed ({})", esp_err_to_name(err));
    }
    fill = 0;
    return "";
  };
  std::string failed;
  while (failed.empty()) {
    uint8_t *data = block + fill;
    const int n = esp_http_client_read(http.client, reinterpret_cast<char *>(data),
                                       static_cast<int>(kBlockBytes - fill));
    if (n < 0) {
      failed = "The download broke off";
      break;
    }
    if (n == 0) {
      if (!esp_http_client_is_complete_data_received(http.client) || done != total) {
        failed = fmt::format("The download stopped at {} of {} KB", done / 1024, total / 1024);
      } else if (fill > 0) {
        failed = write_block(); // the tail
      }
      break;
    }
    const size_t len = static_cast<size_t>(n);
    psa_hash_update(&sha, data, len);
    marker.feed(std::span<const uint8_t>(data, len));
    fill += len;
    done += len;
    if (fill == kBlockBytes) {
      failed = write_block();
    }
    set_done(done);
  }
  return failed;
}

// Streams the release's image from `http` into `ota` (receive), hashing it and
// looking for `marker` as it comes. Returns "" when all `total` bytes are
// written, else why not, with the write aborted.
std::string download(Http &http, esp_ota_handle_t ota, size_t total, hmi::ota::MarkerSearch &marker,
                     Downloaded &got) {
  if (psa_crypto_init() != PSA_SUCCESS) {
    esp_ota_abort(ota);
    return "PSA crypto did not start";
  }
  psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
  psa_hash_setup(&sha, PSA_ALG_SHA_256);

  // Written a whole block at a time: esp_ota_write erases what each write
  // covers, and a 64 KB-aligned 64 KB range is one block erase where the same
  // bytes in reads' sizes are sixteen sector erases (measured: 78 KB/s).
  std::unique_ptr<uint8_t, decltype(&free)> block(
      static_cast<uint8_t *>(heap_caps_malloc(kBlockBytes, MALLOC_CAP_SPIRAM)), free);
  if (!block) {
    esp_ota_abort(ota);
    psa_hash_abort(&sha);
    return "No memory for the download";
  }
  size_t done = 0; // bytes received
  const int64_t started_us = esp_timer_get_time();
  const std::string failed = receive(http, ota, total, block.get(), sha, marker, done);
  size_t digest_len = 0;
  got.hashed =
      psa_hash_finish(&sha, got.digest.data(), got.digest.size(), &digest_len) == PSA_SUCCESS &&
      digest_len == got.digest.size();
  got.done = done;
  got.started_us = started_us;
  if (!failed.empty()) {
    esp_ota_abort(ota);
    return failed;
  }
  return "";
}

// After the download: the image's own check, the digest against GitHub's, the
// next boot, and whether the new image confirms its own boot. Returns why it
// stopped, or "" when the image is written and verified.
std::string finish(const GithubRelease &release, const esp_partition_t *slot, esp_ota_handle_t ota,
                   const hmi::ota::MarkerSearch &marker, const Downloaded &got) {
  const int64_t ms = (esp_timer_get_time() - got.started_us) / 1000;
  set_stage(
      OtaStage::VERIFYING,
      fmt::format("Downloaded {} KB in {:.1f} s ({:.0f} KB/s). Checking...", got.done / 1024,
                  static_cast<double>(ms) / 1000.0,
                  ms > 0 ? static_cast<double>(got.done) / 1.024 / static_cast<double>(ms) : 0.0));

  // esp_ota_end checks the image itself: its segments and its own SHA-256.
  if (esp_err_t err = esp_ota_end(ota); err != ESP_OK) {
    return err == ESP_ERR_OTA_VALIDATE_FAILED
               ? "The image is damaged: it failed its own check"
               : fmt::format("Finishing the write failed ({})", esp_err_to_name(err));
  }
  const std::string sha256 = got.hashed ? hex(got.digest.data(), got.digest.size()) : "";
  if (!release.sha256.empty()) {
    if (sha256 != release.sha256) {
      return "The file is not the one GitHub published (SHA-256 differs)";
    }
    set_stage(OtaStage::VERIFYING, "SHA-256 matches GitHub's digest");
  }

  if (esp_err_t err = esp_ota_set_boot_partition(slot); err != ESP_OK) {
    return fmt::format("Could not select the new image ({})", esp_err_to_name(err));
  }
  if (marker.found()) {
    set_stage(OtaStage::VERIFYING,
              "It confirms its own boot: a reset before it does brings this firmware back");
  } else if (esp_err_t err = mark_next_boot_valid(); err == ESP_OK) {
    set_stage(OtaStage::VERIFYING,
              "An older release: installed as confirmed, it will not roll back by itself");
  } else {
    logger.warn("Could not mark the older image valid ({}): its first reset rolls it back",
                esp_err_to_name(err));
  }
  if (!sha256.empty() && sha256 == release.sha256) {
    // What About shows: this image is the release's file, byte for byte.
    fw_info_record_release(sha256, release.tag, release.prerelease);
  }
  return "";
}

// Everything between choosing the release and setting the next boot. Returns
// why it stopped, or "" when the image is written and verified.
std::string install(const GithubRelease &release) {
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  if (slot == nullptr) {
    return "No update slot: this board's partition table predates updates. Flash it once over USB.";
  }
  if (release.size > slot->size) {
    return fmt::format("The image ({} KB) is larger than the update slot", release.size / 1024);
  }

  set_stage(OtaStage::CONNECTING, release.url.starts_with("https://github.com/")
                                      ? "Connecting to GitHub..."
                                      : "Connecting to " + release.url);
  Http http;
  if (std::string err = open_get(http, release.url.c_str(), false); !err.empty()) {
    return err;
  }
  const size_t total = http.length > 0 ? static_cast<size_t>(http.length) : release.size;
  if (release.size != 0 && total != release.size) {
    return fmt::format("GitHub sent {} bytes, the release lists {}", total, release.size);
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex);
    status.total = total;
  }

  esp_ota_handle_t ota = 0;
  // Erased as it is written, rather than 4 MB up front with the socket idle.
  if (esp_err_t err = esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota); err != ESP_OK) {
    return err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE
               ? "This firmware has not confirmed its own boot yet. Try again in a minute."
               : fmt::format("Could not start writing ({})", esp_err_to_name(err));
  }
  set_stage(OtaStage::DOWNLOADING,
            fmt::format("Downloading {} ({} KB) to {}", release.tag, total / 1024, slot->label));

  hmi::ota::MarkerSearch marker(kHmiConfirmsItsBoot);
  Downloaded got;
  if (std::string err = download(http, ota, total, marker, got); !err.empty()) {
    return err;
  }
  return finish(release, slot, ota, marker, got);
}

void start_thread(const char *name, size_t stack, void (*fn)()) {
  // esp_pthread_set_cfg is per calling task and sticks: put back what was
  // there, so threads this task starts later do not inherit this one's.
  esp_pthread_cfg_t previous = esp_pthread_get_default_config();
  const bool had_previous = esp_pthread_get_cfg(&previous) == ESP_OK;
  esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
  cfg.stack_size = stack;
  cfg.prio = 3; // under the UI, the stick and RTPS
  cfg.thread_name = name;
  esp_pthread_set_cfg(&cfg);
  std::thread(fn).detach();
  if (had_previous) {
    esp_pthread_set_cfg(&previous);
  } else {
    const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&defaults);
  }
}

GithubRelease installing; // what the install thread works on; set before it starts

} // namespace

GithubReleases github_releases_fetch() {
  GithubReleases out;
  Http http;
  if (out.error = open_get(http, kReleasesUrl, true); !out.error.empty()) {
    logger.warn("Release list: {}", out.error);
    return out;
  }
  std::string json;
  json.reserve(http.length > 0 ? static_cast<size_t>(http.length) : 160 * 1024);
  std::unique_ptr<char, decltype(&free)> buf(
      static_cast<char *>(heap_caps_malloc(kChunkBytes, MALLOC_CAP_SPIRAM)), free);
  if (!buf) {
    out.error = "No memory for the release list";
    return out;
  }
  for (;;) {
    const int n = esp_http_client_read(http.client, buf.get(), kChunkBytes);
    if (n < 0) {
      out.error = "The release list broke off";
      return out;
    }
    if (n == 0) {
      break;
    }
    json.append(buf.get(), static_cast<size_t>(n)); // n > 0 here
  }
  out.releases = parse_releases(json, out.error);
  out.ok = out.error.empty();
  if (kTestUrl[0] != '\0') {
    GithubRelease test{};
    test.tag = "Test image";
    test.prerelease = true;
    test.notes = fmt::format("Not a release: whatever {} serves.", kTestUrl);
    test.url = kTestUrl;
    out.releases.insert(out.releases.begin(), std::move(test));
  }
  logger.info("Release list: {} releases ({} bytes of JSON)", out.releases.size(), json.size());
  return out;
}

bool github_ota_start(const GithubRelease &release) {
  {
    std::lock_guard<std::mutex> lock(status_mutex);
    if (status.stage == OtaStage::CONNECTING || status.stage == OtaStage::DOWNLOADING ||
        status.stage == OtaStage::VERIFYING || status.stage == OtaStage::DONE) {
      return false;
    }
    status = OtaStatus{};
    status.stage = OtaStage::CONNECTING;
    status.tag = release.tag;
  }
  installing = release;
  start_thread("github_ota", kInstallStackBytes, [] {
    const std::string failed = install(installing);
    if (failed.empty()) {
      set_stage(OtaStage::DONE, fmt::format("Installed {}. Restarting...", installing.tag));
    } else {
      set_stage(OtaStage::FAILED, failed);
    }
  });
  return true;
}

OtaStatus github_ota_status() {
  std::lock_guard<std::mutex> lock(status_mutex);
  return status;
}

void github_ota_boot_report() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK) {
    logger.info("Running from {} (no OTA state: flashed over USB)",
                running != nullptr ? running->label : "?");
    return;
  }
  logger.info("Running from {}: {}", running->label,
              state == ESP_OTA_IMG_PENDING_VERIFY ? "an update, not yet confirmed"
              : state == ESP_OTA_IMG_VALID        ? "confirmed"
                                                  : "as flashed over USB");
  if (const esp_partition_t *last = esp_ota_get_last_invalid_partition(); last != nullptr) {
    logger.warn("An update in {} did not confirm itself and was rolled back", last->label);
  }
}

void github_ota_boot_confirm() {
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return; // no OTA data: a USB flash, nothing to confirm
  }
  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    logger.info("Confirmed the updated firmware in {}: {}", running->label, esp_err_to_name(err));
  }
}
