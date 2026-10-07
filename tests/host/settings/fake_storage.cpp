// Fake of main/storage.cpp for the settings host app; see fake_storage.hpp.

#include "fake_storage.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>

#include <unistd.h>

#include "storage.hpp"

namespace fake_storage {
namespace {

State g_state;

// The temporary folder, removed when the app exits.
struct Root {
  std::string path;
  Root() {
    char pattern[] = "/tmp/hmi-settings-XXXXXX";
    const char *made = mkdtemp(pattern);
    if (made == nullptr) {
      std::perror("fake_storage: mkdtemp");
      std::abort(); // a test app with nowhere to put files cannot run
    }
    path = made;
  }
  ~Root() {
    std::error_code ec;
    (void)std::filesystem::remove_all(path, ec); // best effort: a leftover temp folder is harmless
  }
  Root(const Root &) = delete;
  Root &operator=(const Root &) = delete;
};

} // namespace

State &state() { return g_state; }

void reset() { g_state = State{}; }

const std::string &root() {
  static const Root r; // made on first use, so it exists before any static-init caller needs it
  return r.path;
}

} // namespace fake_storage

std::string storage_path(std::string_view name) {
  return fake_storage::root() + "/" + std::string(name);
}

bool storage_write(std::string_view name, std::string_view contents) {
  fake_storage::State &s = fake_storage::state();
  s.write_calls++;
  s.last_name = std::string(name);
  s.last_contents = std::string(contents);
  if (s.fail_writes) {
    return false;
  }
  std::ofstream out(storage_path(name), std::ios::binary | std::ios::trunc);
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  out.flush();
  return static_cast<bool>(out);
}

void storage_migrate_legacy() {}
