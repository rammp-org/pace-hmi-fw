#pragma once

/**
 * @file storage.hpp
 * @brief Files on the `storage` partition: espp::FileSystem (LittleFS),
 *        mounted at /storage. First use mounts it, formatting a blank one.
 */

#include <string>
#include <string_view>

/// Full path of `name` in the storage root.
std::string storage_path(std::string_view name);

/// Replace a file's contents. Written aside and renamed over the old one
/// (LittleFS renames atomically), so a reset mid-write leaves the previous
/// file, never half of one. Logs and returns false on failure.
bool storage_write(std::string_view name, std::string_view contents);

/// Once, on the first boot of the two-slot layout: copy the files the old
/// `storage` partition held (now inside ota_1) into the new one, so a board
/// keeps its joystick calibration. Does nothing once storage has any file, or
/// once ota_1 has held an app. Call before anything reads storage.
void storage_migrate_legacy();
