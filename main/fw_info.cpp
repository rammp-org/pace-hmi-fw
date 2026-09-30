#include "fw_info.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <sstream>
#include <thread>

#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_pthread.h"
#include "esp_timer.h"
#include "format.hpp"
#include "logger.hpp"
#include "psa/crypto.h"
#include "storage.hpp"

namespace {

espp::Logger logger({.tag = "fw_info", .level = espp::Logger::Verbosity::INFO});

constexpr char kReleasesFile[] = "fwinfo.txt"; // beside settings.txt; see fw_info.hpp
constexpr size_t kChunkBytes = 16 * 1024;
constexpr size_t kThreadStackBytes = 6 * 1024;

std::mutex info_mutex;
FwInfo info; // under info_mutex

std::optional<std::string> hash_running_image() {
  const esp_partition_t *part = esp_ota_get_running_partition();
  if (part == nullptr) {
    logger.error("No running partition");
    return std::nullopt;
  }
  // The image's own length -- header, segments, checksum and the appended
  // SHA-256 -- which is the length of the .bin that was flashed. The rest of
  // the partition is erased flash and belongs to no file.
  const esp_partition_pos_t pos = {.offset = part->address, .size = part->size};
  esp_image_metadata_t meta{};
  if (esp_err_t err = esp_image_get_metadata(&pos, &meta); err != ESP_OK) {
    logger.error("Could not read the image's layout: {}", esp_err_to_name(err));
    return std::nullopt;
  }
  if (psa_crypto_init() != PSA_SUCCESS) {
    logger.error("PSA crypto did not start");
    return std::nullopt;
  }
  // Internal RAM: flash reads land in it without a bounce buffer.
  std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[kChunkBytes]);
  if (!chunk) {
    logger.error("No memory to hash with");
    return std::nullopt;
  }

  const int64_t started_us = esp_timer_get_time();
  psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
  bool ok = psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS;
  for (uint32_t done = 0; ok && done < meta.image_len;) {
    const size_t n = std::min<size_t>(kChunkBytes, meta.image_len - done);
    ok = esp_partition_read(part, done, chunk.get(), n) == ESP_OK &&
         psa_hash_update(&op, chunk.get(), n) == PSA_SUCCESS;
    done += n;
  }
  std::array<uint8_t, 32> digest{};
  size_t digest_len = 0;
  ok = ok && psa_hash_finish(&op, digest.data(), digest.size(), &digest_len) == PSA_SUCCESS &&
       digest_len == digest.size();
  if (!ok) {
    psa_hash_abort(&op);
    logger.error("Hashing the image failed");
    return std::nullopt;
  }

  static constexpr char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(64);
  for (uint8_t byte : digest) {
    hex += kHex[byte >> 4];
    hex += kHex[byte & 0xF];
  }
  logger.info("Firmware SHA-256 {} ({} bytes, {} ms)", hex, meta.image_len,
              (esp_timer_get_time() - started_us) / 1000);
  return hex;
}

// This image's line in fwinfo.txt, if the PC ever recorded one.
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

} // namespace

void fw_info_start() {
  // esp_pthread_set_cfg is per calling task and sticks: put back what was
  // there, so threads app_main starts later do not inherit this one's.
  esp_pthread_cfg_t previous = esp_pthread_get_default_config();
  const bool had_previous = esp_pthread_get_cfg(&previous) == ESP_OK;
  esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
  cfg.stack_size = kThreadStackBytes;
  cfg.prio = 2; // under the UI, the stick and RTPS: nothing waits on this
  cfg.thread_name = "fw_hash";
  esp_pthread_set_cfg(&cfg);
  std::thread([] {
    FwInfo found;
    found.sha256 = hash_running_image();
    if (found.sha256) {
      found.release = find_release(*found.sha256);
      if (found.release) {
        logger.info("This is {} {} (checked {})",
                    found.release->prerelease ? "pre-release" : "release", found.release->tag,
                    found.release->checked);
      } else {
        logger.info("Not a published release: no line for this image in {}", kReleasesFile);
      }
    }
    found.done = true;
    std::lock_guard<std::mutex> lock(info_mutex);
    info = std::move(found);
  }).detach();
  if (had_previous) {
    esp_pthread_set_cfg(&previous);
  } else {
    const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&defaults);
  }
}

void fw_info_record_release(const std::string &sha256, const std::string &tag, bool prerelease) {
  if (find_release(sha256)) {
    return; // already there
  }
  std::string contents;
  {
    std::ifstream in(storage_path(kReleasesFile));
    contents.assign(std::istreambuf_iterator<char>(in), {});
  }
  if (!contents.empty() && contents.back() != '\n') {
    contents += '\n';
  }
  // When it was checked, if the clock knows (the MCB or the RTC set it).
  char checked[32] = "unknown";
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
  if (gmtime_r(&now, &utc) != nullptr && utc.tm_year + 1900 >= 2025) {
    std::strftime(checked, sizeof(checked), "%Y-%m-%dT%H:%M:%SZ", &utc);
  }
  contents +=
      fmt::format("{}  {}  {}  {}\n", sha256, tag, prerelease ? "prerelease" : "release", checked);
  storage_write(kReleasesFile, contents);
}

FwInfo fw_info() {
  std::lock_guard<std::mutex> lock(info_mutex);
  return info;
}
