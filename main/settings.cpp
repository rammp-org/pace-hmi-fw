#include "settings.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <string>

#include "logger.hpp"
#include "storage.hpp"

namespace {

espp::Logger logger({.tag = "settings", .level = espp::Logger::Verbosity::INFO});

constexpr const char *kFileName = "settings.txt";

std::mutex mutex;
uint8_t theme = 0;
int brightness = 75;

void save_locked() {
  if (storage_write(kFileName, fmt::format("theme {}\nbrightness {}\n", theme, brightness))) {
    logger.info("saved: theme {}, brightness {}%", theme, brightness);
  }
}

} // namespace

void settings_load() {
  std::ifstream in(storage_path(kFileName));
  std::lock_guard<std::mutex> lock(mutex);
  if (!in) {
    logger.info("no {} yet: theme {}, brightness {}%", kFileName, theme, brightness);
    return;
  }
  std::string key;
  int value = 0;
  while (in >> key >> value) {
    if (key == "theme") {
      theme = static_cast<uint8_t>(std::clamp(value, 0, 255));
    } else if (key == "brightness") {
      brightness = std::clamp(value, kBrightnessMinPercent, kBrightnessMaxPercent);
    }
  }
  logger.info("loaded: theme {}, brightness {}%", theme, brightness);
}

uint8_t settings_theme() {
  std::lock_guard<std::mutex> lock(mutex);
  return theme;
}

void settings_set_theme(uint8_t value) {
  std::lock_guard<std::mutex> lock(mutex);
  if (value != theme) {
    theme = value;
    save_locked();
  }
}

int settings_brightness() {
  std::lock_guard<std::mutex> lock(mutex);
  return brightness;
}

void settings_set_brightness(int percent) {
  percent = std::clamp(percent, kBrightnessMinPercent, kBrightnessMaxPercent);
  std::lock_guard<std::mutex> lock(mutex);
  if (percent != brightness) {
    brightness = percent;
    save_locked();
  }
}
