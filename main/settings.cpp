#include "settings.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <string>

#include "logger.hpp"
#include "storage.hpp"

namespace {

espp::Logger logger({.tag = "settings", .level = espp::Logger::Verbosity::INFO});

constexpr const char *kFileName = "settings.txt";

struct Param {
  const char *name; // the spec table's NAME; stored lowercase
  int min_value;
  int max_value;
  int value; // starts as the table's default
};

std::mutex mutex;
Param params[] = {
#define SETTINGS_STORE_ROW(page_, name_, short_, label_, min_, max_, step_, dec_, unit_, dflt_)    \
  {#name_, min_, max_, dflt_},
    SETTINGS_PARAM_TABLE(SETTINGS_STORE_ROW)
#undef SETTINGS_STORE_ROW
};
static_assert(sizeof(params) / sizeof(params[0]) == SETTINGS_PARAM_COUNT);

std::string key_of(const Param &p) {
  std::string key = p.name;
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return key;
}

std::string describe_locked() {
  std::string out;
  for (const Param &p : params) {
    out += fmt::format("{}{} {}", out.empty() ? "" : ", ", key_of(p), p.value);
  }
  return out;
}

void save_locked() {
  std::string text;
  for (const Param &p : params) {
    text += fmt::format("{} {}\n", key_of(p), p.value);
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
    for (Param &p : params) {
      if (key == key_of(p)) {
        p.value = std::clamp(value, p.min_value, p.max_value);
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
  return params[param].value;
}

void settings_set(int param, int value) {
  if (param < 0 || param >= SETTINGS_PARAM_COUNT) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex);
  Param &p = params[param];
  value = std::clamp(value, p.min_value, p.max_value);
  if (value != p.value) {
    p.value = value;
    save_locked();
  }
}
