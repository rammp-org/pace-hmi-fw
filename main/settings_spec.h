/*
 * settings_spec.h - settings shown on the SpecificSettingScreen.
 *
 * - A page is a title, a line of instructions, and every parameter that names
 *   it, in table order. More rows on a page = more X lines.
 * - Values are integers; `decimals` is display only (755, 1 -> "75.5").
 * - HMI-only: nothing here crosses the wire (see messages/joystick_message.hpp for that).
 */

#ifndef SETTINGS_SPEC_H
#define SETTINGS_SPEC_H

/* P(NAME, title, instructions) */
#define SETTINGS_PAGE_TABLE(P)                                                                     \
  P(UI, "UI Settings", "Left/right or -/+ to change. Saved automatically.")

/* X(PAGE, NAME, short, label, min, max, step, decimals, unit, default)
   A row whose values are names rather than numbers (Theme, Menu slide...) is
   0..n-1 here; main.cpp's kSettingParamNames says what each value reads.
   Every row is saved in settings.txt under its NAME, lowercase (settings.cpp),
   and starts at `default` until it has been. */
#define SETTINGS_PARAM_TABLE(X)                                                                    \
  X(UI, BRIGHTNESS, "S1", "Brightness", 5, 100, 5, 0, "%", 75)                                     \
  X(UI, THEME, "S2", "Theme", 0, 1, 1, 0, "", 0)                                                   \
  X(UI, MENU_SLIDE, "S3", "Menu slide", 0, 1, 1, 0, "", 0)                                         \
  X(UI, FLIP, "S4", "Flip screen", 0, 1, 1, 0, "", 0)                                              \
  X(UI, STICK_SENSITIVITY, "S5", "Stick sensitivity", 1, 10, 1, 0, "", 9)                          \
  X(UI, STICK_INVERT_X, "S6", "Stick left/right", 0, 1, 1, 0, "", 0)                               \
  X(UI, STICK_INVERT_Y, "S7", "Stick fwd/back", 0, 1, 1, 0, "", 0)                                 \
  X(UI, STICK_SWAP, "S8", "Stick axes", 0, 1, 1, 0, "", 0)

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
