/*
 * settings_spec.h - settings shown on the SpecificSettingScreen.
 *
 * - A page is a title, a line of instructions, and every parameter that names
 *   it, in table order. More rows on a page = more X lines.
 * - Values are integers; `decimals` is display only (755, 1 -> "75.5").
 * - HMI-only: nothing here crosses the wire (see rammp_rtps_spec.h for that).
 */

#ifndef SETTINGS_SPEC_H
#define SETTINGS_SPEC_H

/* P(NAME, title, instructions) */
#define SETTINGS_PAGE_TABLE(P)                                                                     \
  P(SCREEN_BRIGHTNESS, "SCREEN BRIGHTNESS",                                                        \
    "Left/right or -/+ to change. Saved automatically. Pull and hold to exit.")

/* X(PAGE, NAME, short, label, min, max, step, decimals, unit) */
#define SETTINGS_PARAM_TABLE(X)                                                                    \
  X(SCREEN_BRIGHTNESS, BRIGHTNESS, "S1", "Brightness", 5, 100, 5, 0, "%")

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
