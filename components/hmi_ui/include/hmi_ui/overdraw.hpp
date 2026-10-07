#pragma once
// Overdraw: SquareLine gives every container an opaque background; these clear the fills
// nobody can see (src/overdraw.cpp has the rule).

#include <cstdint>

#include "lvgl.h"

namespace hmi::ui {

/// @brief Clears the redundant background fills under one screen; the screen keeps its own.
/// @param screen The screen.
/// @param overlay The flag that marks an overlay: it keeps its fill, subtree and all.
/// @return How many fills it cleared.
/// UI task (app_main at boot, a screen's *_ensure, the theme switch).
uint32_t strip_screen_overdraw(const lv_obj_t *screen, lv_obj_flag_t overlay);

/// @brief strip_screen_overdraw over every screen ui_init built; screens built on demand that
/// do not exist now are skipped.
/// @param overlay The flag that marks an overlay.
/// @return How many fills it cleared.
/// UI task (app_main at boot, the theme switch).
uint32_t strip_all_overdraw(lv_obj_flag_t overlay);

} // namespace hmi::ui
