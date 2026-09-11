#pragma once

/**
 * @file settings.hpp
 * @brief User settings that survive a reboot, in /storage/settings.txt.
 *
 * One "key value" line each. A missing file or key keeps the default; unknown
 * keys are ignored, so a file from an older or newer firmware still loads.
 * Any task; each setter writes the file only when the value changed.
 */

#include <cstdint>

constexpr int kBrightnessMinPercent = 5; // never fully dark: nothing could turn it back on
constexpr int kBrightnessMaxPercent = 100;

/// Read the file. Call once from app_main, before the getters.
void settings_load();

/// UI theme index (ui_themes.h). Default 0, UI_THEME_DEFAULT.
uint8_t settings_theme();
void settings_set_theme(uint8_t theme);

/// Backlight %, kBrightnessMinPercent..kBrightnessMaxPercent. Default 75.
int settings_brightness();
void settings_set_brightness(int percent);
