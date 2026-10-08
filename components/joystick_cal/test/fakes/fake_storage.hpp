#pragma once
// Test-only stand-in for components/storage/src/storage.cpp (espp::FileSystem on the `storage`
// partition): storage_path() and storage_write() on a fresh folder under the host's temp directory.

#include <string>
#include <string_view>

namespace fake_storage {
/// The folder standing in for /storage. Made on first use, one per process.
const std::string &root();
/// Replaces `name` with `contents` directly (a test's setup, not a storage_write call).
void put(std::string_view name, std::string_view contents);
/// Removes `name` if it is there.
void remove(std::string_view name);
/// Makes every storage_write fail (return false, write nothing) until set back.
void set_write_fails(bool fails);
/// How many times storage_write was called, and what it was last given.
int write_calls();
const std::string &last_written();
} // namespace fake_storage
