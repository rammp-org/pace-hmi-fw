// The fake storage behind fakes/fake_storage.hpp (test only).

#include "fake_storage.hpp"

#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "storage.hpp"

namespace {

bool g_write_fails = false;
int g_write_calls = 0;
std::string g_last_written;

std::string full_path(std::string_view name) {
  return (std::filesystem::path(fake_storage::root()) / std::filesystem::path(name)).string();
}

} // namespace

namespace fake_storage {

const std::string &root() {
  static const std::string dir = [] {
    const auto p = std::filesystem::temp_directory_path() /
                   ("hmi_joystick_cal_" + std::to_string(static_cast<long>(::getpid())));
    std::filesystem::create_directories(p);
    return p.string();
  }();
  return dir;
}

void put(std::string_view name, std::string_view contents) {
  std::ofstream out(full_path(name), std::ios::binary | std::ios::trunc);
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

void remove(std::string_view name) {
  std::error_code ec;
  std::filesystem::remove(full_path(name), ec); // absent already is fine
}

void set_write_fails(bool fails) { g_write_fails = fails; }

int write_calls() { return g_write_calls; }

const std::string &last_written() { return g_last_written; }

} // namespace fake_storage

std::string storage_path(std::string_view name) { return full_path(name); }

bool storage_write(std::string_view name, std::string_view contents) {
  ++g_write_calls;
  if (g_write_fails) {
    return false;
  }
  g_last_written = std::string(contents);
  fake_storage::put(name, contents);
  return true;
}
