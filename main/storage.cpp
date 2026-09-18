#include "storage.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "esp_littlefs.h"
#include "esp_partition.h"

#include "file_system.hpp"
#include "logger.hpp"

namespace {
espp::Logger logger({.tag = "storage", .level = espp::Logger::Verbosity::INFO});

// Where `storage` was before the OTA layout (partitions.csv): now inside ota_1.
constexpr size_t kLegacyOffset = 0x810000;
constexpr size_t kLegacySize = 0x400000;
constexpr const char *kLegacyMount = "/legacy";
} // namespace

void storage_migrate_legacy() {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root = espp::FileSystem::get().get_root_path(); // mounts, formatting a new one
  if (fs::directory_iterator(root, ec) != fs::directory_iterator()) {
    return; // already has files: migrated, or never needed it
  }
  const esp_partition_t *ota1 =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
  if (ota1 == nullptr || kLegacyOffset < ota1->address ||
      kLegacyOffset + kLegacySize > ota1->address + ota1->size) {
    return;
  }
  // Once an update has been written to ota_1 the old files are gone, or worse, half there.
  uint8_t magic = 0;
  if (esp_partition_read(ota1, 0, &magic, 1) != ESP_OK || magic == 0xE9) {
    return;
  }
  // IDF will not register a partition overlapping ota_1, and littlefs only reads
  // through esp_partition_read, so a hand-made read-only one does.
  esp_partition_t legacy = *ota1;
  legacy.address = kLegacyOffset;
  legacy.size = kLegacySize;
  legacy.type = ESP_PARTITION_TYPE_DATA;
  legacy.subtype = ESP_PARTITION_SUBTYPE_DATA_LITTLEFS;
  legacy.readonly = true;
  strlcpy(legacy.label, "legacy_storage", sizeof(legacy.label));
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = kLegacyMount;
  conf.partition = &legacy;
  conf.format_if_mount_failed = false;
  conf.read_only = true;
  if (esp_vfs_littlefs_register(&conf) != ESP_OK) {
    logger.info("No files at the old storage offset to migrate");
    return;
  }
  int copied = 0;
  for (const auto &entry : fs::directory_iterator(kLegacyMount, ec)) {
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(in), {}};
    const std::string name = entry.path().filename().string();
    if (in.good() || in.eof()) {
      copied += storage_write(name, contents) ? 1 : 0;
      logger.info("Migrated {} ({} B) from the old storage partition", name, contents.size());
    }
  }
  esp_vfs_littlefs_unregister_partition(&legacy);
  logger.info("Storage migration: {} file(s) copied", copied);
}

std::string storage_path(std::string_view name) {
  espp::FileSystem::get(); // mounts on first use
  return (espp::FileSystem::get_root_path() / std::filesystem::path(name)).string();
}

bool storage_write(std::string_view name, std::string_view contents) {
  const std::string path = storage_path(name);
  const std::string temp = path + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.flush();
    if (!out) {
      logger.error("could not write {}", temp);
      return false;
    }
  }
  if (std::rename(temp.c_str(), path.c_str()) != 0) {
    logger.error("could not rename {} to {}", temp, path);
    return false;
  }
  return true;
}
