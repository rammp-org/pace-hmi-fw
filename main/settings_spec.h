#pragma once

/**
 * @file settings_spec.h
 * @brief The settings shown on the SettingsScreen (Settings in the menu), as typed spec tables
 *        (CS-TYP-03): SETTINGS_PAGES and SETTINGS_PARAMS.
 *
 * - A page is a title, a line of instructions, and every parameter that names it, in table
 *   order. More rows on a page = more rows here.
 * - Values are integers; `decimals` is display only (755, 1 -> "75.5").
 * - HMI-only: nothing here crosses the wire (see messages/joystick_message.hpp for that).
 * - Every row is saved in settings.txt under its `key` (settings.cpp) and starts at
 *   `default_value` until it has been. A key is the file format: never rename one.
 *
 * C++ only (constexpr std::array); every includer is C++.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

/// A page of the SettingsScreen. Unscoped and int-based because its enumerators are used as
/// int page numbers (setting_page_subject, kActuatorsPage = SETTINGS_PAGE_COUNT).
enum SettingsPage : int {
  SETTINGS_PAGE_DISPLAY,
  SETTINGS_PAGE_STICK,
  SETTINGS_PAGE_NET,
  SETTINGS_PAGE_COUNT
};

/// A setting. Unscoped and int-based because its enumerators are the int `param` of
/// settings_get()/settings_set() and index arrays in the UI.
enum SettingsParam : int {
  SETTINGS_PARAM_BRIGHTNESS,
  SETTINGS_PARAM_THEME,
  SETTINGS_PARAM_MENU_SLIDE,
  SETTINGS_PARAM_FLIP,
  SETTINGS_PARAM_STICK_SENSITIVITY,
  SETTINGS_PARAM_DRIVE_SPEED,
  SETTINGS_PARAM_STICK_INVERT_X,
  SETTINGS_PARAM_STICK_INVERT_Y,
  SETTINGS_PARAM_STICK_SWAP,
  SETTINGS_PARAM_SOUNDS,
  SETTINGS_PARAM_NETWORK,
  SETTINGS_PARAM_COUNT
};

/// One row of SETTINGS_PAGES.
struct SettingsPageSpec {
  SettingsPage id;          ///< must equal the row's index
  const char *title;        ///< shown at the top of the page
  const char *instructions; ///< the line under the title
};

/// One row of SETTINGS_PARAMS.
struct SettingsParamSpec {
  SettingsParam id;       ///< must equal the row's index
  SettingsPage page;      ///< the page the row is on
  const char *key;        ///< its name in settings.txt: a-z, 0-9 and '_', unique
  const char *short_name; ///< "D1": the row's short label
  const char *label;      ///< "Brightness"
  int min_value;          ///< lowest value; a load or a set clamps to it
  int max_value;          ///< highest value; a load or a set clamps to it
  int step;               ///< what one -/+ press adds
  uint8_t decimals;       ///< display only: 126 with 1 decimal shows "12.6"
  const char *unit;       ///< appended to the shown value; "" = none
  int default_value;      ///< the value until settings.txt says otherwise
};

/// A page is one row of Settings' level in the burger menu (main.cpp, NavDest), except NET,
/// which the InternetScreen draws.
inline constexpr std::array<SettingsPageSpec, SETTINGS_PAGE_COUNT> SETTINGS_PAGES{{
    {SETTINGS_PAGE_DISPLAY, "Display & sound", "Left/right or -/+ to change. Saved automatically."},
    {SETTINGS_PAGE_STICK, "Joystick & driving",
     "Left/right or -/+ to change. Saved automatically."},
    {SETTINGS_PAGE_NET, "Internet", "Shown on the InternetScreen, not as rows."},
}};

/// The settings, in SettingsParam order.
///
/// A row whose values are names rather than numbers (Theme, Menu slide...) is 0..n-1 here;
/// frag_settings_ui.inc's kSettingParamNames says what each value reads.
/// DRIVE_SPEED scales the stick on its way to the MCB, in tenths: 10 = the stick as it is,
/// 1 = as if it moved a tenth as far. The UI keys and the bars do not see it.
/// NETWORK is what RTPS runs over, 0 = Ethernet (the W5500, the default), 1 = WiFi (the
/// ESP32-C6; Ethernet anyway while no WiFi network is known). It is on a page of its own
/// because the InternetScreen draws it as two choice buttons (internet_ui.cpp), not as a -/+
/// row. Read at boot, so a change takes a restart, which that screen offers.
inline constexpr std::array<SettingsParamSpec, SETTINGS_PARAM_COUNT> SETTINGS_PARAMS{{
    // id, page, key, short, label, min, max, step, decimals, unit, default
    {SETTINGS_PARAM_BRIGHTNESS, SETTINGS_PAGE_DISPLAY, "brightness", "D1", "Brightness", 5, 100, 5,
     0, "%", 75},
    {SETTINGS_PARAM_THEME, SETTINGS_PAGE_DISPLAY, "theme", "D2", "Theme", 0, 1, 1, 0, "", 0},
    {SETTINGS_PARAM_MENU_SLIDE, SETTINGS_PAGE_DISPLAY, "menu_slide", "D3", "Menu slide", 0, 1, 1, 0,
     "", 0},
    {SETTINGS_PARAM_FLIP, SETTINGS_PAGE_DISPLAY, "flip", "D4", "Flip screen", 0, 1, 1, 0, "", 0},
    {SETTINGS_PARAM_STICK_SENSITIVITY, SETTINGS_PAGE_STICK, "stick_sensitivity", "J1",
     "Stick sensitivity", 1, 10, 1, 0, "", 9},
    {SETTINGS_PARAM_DRIVE_SPEED, SETTINGS_PAGE_STICK, "drive_speed", "J2", "Speed sensitivity", 1,
     10, 1, 1, "x", 10},
    {SETTINGS_PARAM_STICK_INVERT_X, SETTINGS_PAGE_STICK, "stick_invert_x", "J3", "Stick left/right",
     0, 1, 1, 0, "", 0},
    {SETTINGS_PARAM_STICK_INVERT_Y, SETTINGS_PAGE_STICK, "stick_invert_y", "J4", "Stick fwd/back",
     0, 1, 1, 0, "", 0},
    {SETTINGS_PARAM_STICK_SWAP, SETTINGS_PAGE_STICK, "stick_swap", "J5", "Stick axes", 0, 1, 1, 0,
     "", 0},
    {SETTINGS_PARAM_SOUNDS, SETTINGS_PAGE_DISPLAY, "sounds", "D5", "Sounds", 0, 1, 1, 0, "", 1},
    {SETTINGS_PARAM_NETWORK, SETTINGS_PAGE_NET, "network", "N1", "Connection", 0, 1, 1, 0, "", 0},
}};

// --- the ranges other code clamps with, from the table ---

inline constexpr int SETTINGS_BRIGHTNESS_MIN = SETTINGS_PARAMS[SETTINGS_PARAM_BRIGHTNESS].min_value;
inline constexpr int SETTINGS_BRIGHTNESS_MAX = SETTINGS_PARAMS[SETTINGS_PARAM_BRIGHTNESS].max_value;
inline constexpr int SETTINGS_STICK_SENSITIVITY_MIN =
    SETTINGS_PARAMS[SETTINGS_PARAM_STICK_SENSITIVITY].min_value;
inline constexpr int SETTINGS_STICK_SENSITIVITY_MAX =
    SETTINGS_PARAMS[SETTINGS_PARAM_STICK_SENSITIVITY].max_value;
inline constexpr int SETTINGS_DRIVE_SPEED_MIN =
    SETTINGS_PARAMS[SETTINGS_PARAM_DRIVE_SPEED].min_value;
inline constexpr int SETTINGS_DRIVE_SPEED_MAX =
    SETTINGS_PARAMS[SETTINGS_PARAM_DRIVE_SPEED].max_value;

// --- invariants (CS-TYP-03, TS-UNIT-04) ---

namespace settings_spec_detail {

/// A settings.txt key: non-empty, and only a-z, 0-9 and '_', so `in >> key` reads it whole.
constexpr bool is_valid_key(std::string_view key) {
  if (key.empty()) {
    return false;
  }
  return std::all_of(key.begin(), key.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

/// Each row's id is its index, so SETTINGS_PARAMS[SETTINGS_PARAM_X] is X's row.
constexpr bool rows_in_order() {
  for (std::size_t i = 0; i < SETTINGS_PAGES.size(); i++) {
    if (static_cast<std::size_t>(SETTINGS_PAGES[i].id) != i) {
      return false;
    }
  }
  for (std::size_t i = 0; i < SETTINGS_PARAMS.size(); i++) {
    if (static_cast<std::size_t>(SETTINGS_PARAMS[i].id) != i) {
      return false;
    }
  }
  return true;
}

/// Every row names a page that exists.
constexpr bool pages_valid() {
  return std::all_of(
      SETTINGS_PARAMS.begin(), SETTINGS_PARAMS.end(),
      [](const SettingsParamSpec &p) { return p.page >= 0 && p.page < SETTINGS_PAGE_COUNT; });
}

/// Every key is a valid settings.txt key.
constexpr bool keys_valid() {
  return std::all_of(
      SETTINGS_PARAMS.begin(), SETTINGS_PARAMS.end(),
      [](const SettingsParamSpec &p) { return p.key != nullptr && is_valid_key(p.key); });
}

/// No two rows share a key (the codec would load one value into both).
constexpr bool keys_unique() {
  for (std::size_t i = 0; i < SETTINGS_PARAMS.size(); i++) {
    for (std::size_t j = i + 1; j < SETTINGS_PARAMS.size(); j++) {
      if (std::string_view{SETTINGS_PARAMS[i].key} == std::string_view{SETTINGS_PARAMS[j].key}) {
        return false;
      }
    }
  }
  return true;
}

/// No two rows share a short name.
constexpr bool short_names_unique() {
  for (std::size_t i = 0; i < SETTINGS_PARAMS.size(); i++) {
    for (std::size_t j = i + 1; j < SETTINGS_PARAMS.size(); j++) {
      if (std::string_view{SETTINGS_PARAMS[i].short_name} ==
          std::string_view{SETTINGS_PARAMS[j].short_name}) {
        return false;
      }
    }
  }
  return true;
}

/// min <= default <= max and step > 0 on every row.
constexpr bool ranges_valid() {
  return std::all_of(
      SETTINGS_PARAMS.begin(), SETTINGS_PARAMS.end(), [](const SettingsParamSpec &p) {
        return p.min_value <= p.default_value && p.default_value <= p.max_value && p.step > 0;
      });
}

/// Every text field is set ("" is allowed, nullptr is not).
constexpr bool texts_present() {
  return std::all_of(SETTINGS_PAGES.begin(), SETTINGS_PAGES.end(),
                     [](const SettingsPageSpec &g) {
                       return g.title != nullptr && g.instructions != nullptr;
                     }) &&
         std::all_of(SETTINGS_PARAMS.begin(), SETTINGS_PARAMS.end(),
                     [](const SettingsParamSpec &p) {
                       return p.short_name != nullptr && p.label != nullptr && p.unit != nullptr;
                     });
}

} // namespace settings_spec_detail

static_assert(settings_spec_detail::rows_in_order(),
              "SETTINGS_PAGES and SETTINGS_PARAMS rows must be in enum order");
static_assert(settings_spec_detail::pages_valid(), "every setting must name a page that exists");
static_assert(settings_spec_detail::keys_valid(),
              "a settings.txt key is non-empty a-z, 0-9 and '_' (the codec splits on whitespace)");
static_assert(settings_spec_detail::keys_unique(), "settings.txt keys must be unique");
static_assert(settings_spec_detail::short_names_unique(), "short names must be unique");
static_assert(settings_spec_detail::ranges_valid(),
              "every setting needs min <= default <= max and a step > 0");
static_assert(settings_spec_detail::texts_present(), "every text field must be set");
