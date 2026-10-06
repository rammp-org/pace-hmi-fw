#include "settings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>

#include "logger.hpp"
#include "storage.hpp"

namespace {

espp::Logger logger({.tag = "settings", .level = espp::Logger::Verbosity::INFO});

constexpr const char *kFileName = "settings.txt";

std::mutex mutex;
// Each setting's value, in SETTINGS_PARAMS order; starts as the table's default.
// constinit: ready before any dynamic initialiser could call a getter.
constinit std::array<int, SETTINGS_PARAM_COUNT> values = [] {
  std::array<int, SETTINGS_PARAM_COUNT> out{};
  for (std::size_t i = 0; i < out.size(); i++) {
    out[i] = SETTINGS_PARAMS[i].default_value;
  }
  return out;
}();

std::string describe_locked() {
  std::string out;
  for (std::size_t i = 0; i < values.size(); i++) {
    out += fmt::format("{}{} {}", out.empty() ? "" : ", ", SETTINGS_PARAMS[i].key, values[i]);
  }
  return out;
}

void save_locked() {
  std::string text;
  for (std::size_t i = 0; i < values.size(); i++) {
    text += fmt::format("{} {}\n", SETTINGS_PARAMS[i].key, values[i]);
  }
  if (storage_write(kFileName, text)) {
    logger.info("saved: {}", describe_locked());
  }
}

} // namespace

void settings_load() {
  std::ifstream in(storage_path(kFileName));
  std::lock_guard<std::mutex> lock(mutex);
  if (!in) {
    logger.info("no {} yet: {}", kFileName, describe_locked());
    return;
  }
  std::string key;
  int value = 0;
  while (in >> key >> value) {
    for (std::size_t i = 0; i < values.size(); i++) {
      const SettingsParamSpec &p = SETTINGS_PARAMS[i];
      if (key == p.key) {
        values[i] = std::clamp(value, p.min_value, p.max_value);
      }
    }
  }
  logger.info("loaded: {}", describe_locked());
}

int settings_get(int param) {
  if (param < 0 || param >= SETTINGS_PARAM_COUNT) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(mutex);
  return values[static_cast<std::size_t>(param)];
}

void settings_set(int param, int value) {
  if (param < 0 || param >= SETTINGS_PARAM_COUNT) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex);
  const SettingsParamSpec &p = SETTINGS_PARAMS[static_cast<std::size_t>(param)];
  int &stored = values[static_cast<std::size_t>(param)];
  value = std::clamp(value, p.min_value, p.max_value);
  if (value != stored) {
    stored = value;
    save_locked();
  }
}
