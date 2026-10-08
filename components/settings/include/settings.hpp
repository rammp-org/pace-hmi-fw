#pragma once

/**
 * @file settings.hpp
 * @brief User settings that survive a reboot, in /storage/settings.txt.
 *
 * One "key value" line each, in SETTINGS_PARAMS order, under each row's `key`
 * ("brightness", "theme", "stick_sensitivity"...). A missing file or key keeps
 * the value in RAM (the table's default at boot); an unknown key followed by a
 * number is skipped, so a file from an older or newer firmware still loads.
 * Today the first token that is not a number stops the load, silently, and an
 * out-of-range value is clamped without a report (pinned by tests/host/settings,
 * SET-036, SET-043). It sits beside joystick_cal.txt on the same LittleFS
 * partition. Any task; a setter writes the file only when the value changed.
 */

#include <cstdint>

#include "settings_spec.hpp"

// From the spec table. Never 0: a dark screen could not be turned back on.
constexpr int kBrightnessMinPercent = SETTINGS_BRIGHTNESS_MIN;
constexpr int kBrightnessMaxPercent = SETTINGS_BRIGHTNESS_MAX;
static_assert(kBrightnessMinPercent > 0, "the backlight must not be able to go fully dark");

/// Read the file. Call once from app_main, before the getters.
void settings_load();

/// A SETTINGS_PARAM_* value, within its table range.
int settings_get(int param);
/// Clamped to the table range; saved if it changed.
void settings_set(int param, int value);

/// UI theme index (ui_themes.h): the THEME row, 0 = UI_THEME_DEFAULT.
inline uint8_t settings_theme() { return static_cast<uint8_t>(settings_get(SETTINGS_PARAM_THEME)); }
inline void settings_set_theme(uint8_t value) { settings_set(SETTINGS_PARAM_THEME, value); }

/// Backlight %, kBrightnessMinPercent..kBrightnessMaxPercent.
inline int settings_brightness() { return settings_get(SETTINGS_PARAM_BRIGHTNESS); }
inline void settings_set_brightness(int percent) {
  settings_set(SETTINGS_PARAM_BRIGHTNESS, percent);
}
