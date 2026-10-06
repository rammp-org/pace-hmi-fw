/*
 * settings_spec.h - settings shown on the SettingsScreen (Settings in the menu).
 *
 * - A page is a title, a line of instructions, and every parameter that names
 *   it, in table order. More rows on a page = more X lines.
 * - Values are integers; `decimals` is display only (755, 1 -> "75.5").
 * - HMI-only: nothing here crosses the wire (see messages/joystick_message.hpp for that).
 */

#ifndef SETTINGS_SPEC_H
#define SETTINGS_SPEC_H

/* P(NAME, title, instructions)
   A page is one row of Settings' level in the burger menu (main.cpp, NavDest), except NET,
   which the InternetScreen draws. */
#define SETTINGS_PAGE_TABLE(P)                                                                     \
  P(DISPLAY, "Display & sound", "Left/right or -/+ to change. Saved automatically.")               \
  P(STICK, "Joystick & driving", "Left/right or -/+ to change. Saved automatically.")              \
  P(NET, "Internet", "Shown on the InternetScreen, not as rows.")

/* X(PAGE, NAME, short, label, min, max, step, decimals, unit, default)
   A row whose values are names rather than numbers (Theme, Menu slide...) is
   0..n-1 here; main.cpp's kSettingParamNames says what each value reads.
   Every row is saved in settings.txt under its NAME, lowercase (settings.cpp),
   and starts at `default` until it has been.
   DRIVE_SPEED scales the stick on its way to the MCB, in tenths: 10 = the stick as it
   is, 1 = as if it moved a tenth as far. The UI keys and the bars do not see it.
   NETWORK is what RTPS runs over, 0 = Ethernet (the W5500, the default), 1 = WiFi (the
   ESP32-C6; Ethernet anyway while no WiFi network is known). It is on a page of its own because the
   InternetScreen draws it as two choice buttons (internet_ui.cpp), not as a -/+ row. Read
   at boot, so a change takes a restart, which that screen offers. */
#define SETTINGS_PARAM_TABLE(X)                                                                    \
  X(DISPLAY, BRIGHTNESS, "D1", "Brightness", 5, 100, 5, 0, "%", 75)                                \
  X(DISPLAY, THEME, "D2", "Theme", 0, 1, 1, 0, "", 0)                                              \
  X(DISPLAY, MENU_SLIDE, "D3", "Menu slide", 0, 1, 1, 0, "", 0)                                    \
  X(DISPLAY, FLIP, "D4", "Flip screen", 0, 1, 1, 0, "", 0)                                         \
  X(STICK, STICK_SENSITIVITY, "J1", "Stick sensitivity", 1, 10, 1, 0, "", 9)                       \
  X(STICK, DRIVE_SPEED, "J2", "Speed sensitivity", 1, 10, 1, 1, "x", 10)                           \
  X(STICK, STICK_INVERT_X, "J3", "Stick left/right", 0, 1, 1, 0, "", 0)                            \
  X(STICK, STICK_INVERT_Y, "J4", "Stick fwd/back", 0, 1, 1, 0, "", 0)                              \
  X(STICK, STICK_SWAP, "J5", "Stick axes", 0, 1, 1, 0, "", 0)                                      \
  X(DISPLAY, SOUNDS, "D5", "Sounds", 0, 1, 1, 0, "", 1)                                            \
  X(NET, NETWORK, "N1", "Connection", 0, 1, 1, 0, "", 0)

/* SETTINGS_PAGE_SCREEN_BRIGHTNESS, ..., SETTINGS_PAGE_COUNT */
enum {
#define SETTINGS_PAGE_ENUM(name_, title_, text_) SETTINGS_PAGE_##name_,
  SETTINGS_PAGE_TABLE(SETTINGS_PAGE_ENUM)
#undef SETTINGS_PAGE_ENUM
      SETTINGS_PAGE_COUNT
};

/* SETTINGS_PARAM_BRIGHTNESS, ..., SETTINGS_PARAM_COUNT */
enum {
#define SETTINGS_PARAM_ENUM(page_, name_, ...) SETTINGS_PARAM_##name_,
  SETTINGS_PARAM_TABLE(SETTINGS_PARAM_ENUM)
#undef SETTINGS_PARAM_ENUM
      SETTINGS_PARAM_COUNT
};

/* SETTINGS_BRIGHTNESS_MIN / _MAX: the range, for whatever stores the value */
enum {
#define SETTINGS_PARAM_LIMITS(page_, name_, short_, label_, min_, max_, ...)                       \
  SETTINGS_##name_##_MIN = (min_), SETTINGS_##name_##_MAX = (max_),
  SETTINGS_PARAM_TABLE(SETTINGS_PARAM_LIMITS)
#undef SETTINGS_PARAM_LIMITS
};

#endif /* SETTINGS_SPEC_H */
